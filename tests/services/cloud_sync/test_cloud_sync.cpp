#include "pixcake/cloud_sync/sync_engine.hpp"
#include "pixcake/project_session/project_session.hpp"
#include "pixcake/editdoc/edit_document.hpp"
#include "pixcake/layerdoc/layer_document.hpp"
#include "pixcake/net/curl_http_transport.hpp"
#include "pixcake/persist/atomic_file.hpp"
#include "pixcake/preview_channel/preview_session.hpp"
#include "pixcake/platform/mac_image_decoder.hpp"
#include "asgi_transport.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>

using namespace pixcake;
using namespace pixcake::cloud_sync;
using namespace pixcake::project_session;
namespace fs = std::filesystem;
namespace {
struct Session : SessionState { bool in = true; bool signed_in() const override { return in; } };
struct Access : SourceAccess {
  ResolvedSource resolve(const std::vector<std::uint8_t>& bytes) override {
    ResolvedSource result; result.absolute_path.assign(bytes.begin(), bytes.end());
    result.code = fs::is_regular_file(result.absolute_path) ? ResultCode::Ok : ResultCode::SourceMissing; return result;
  }
};
ResultCode bookmark(const std::string& path, std::vector<std::uint8_t>& bytes) {
  bytes.assign(path.begin(), path.end()); return ResultCode::Ok;
}
void bmp(const fs::path& path) {
  constexpr unsigned width = 768, height = 1024, size = 54 + width * height * 3;
  std::vector<std::uint8_t> bytes(size);
  for (std::size_t i = 54; i < bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>(i * 31 + i / 1000);
  const auto put = [&](unsigned offset, unsigned value, unsigned count) {
    for (unsigned i = 0; i < count; ++i) bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
  };
  bytes[0] = 'B'; bytes[1] = 'M'; put(2, size, 4); put(10, 54, 4); put(14, 40, 4);
  put(18, width, 4); put(22, height, 4); put(26, 1, 2); put(28, 24, 2); put(34, size - 54, 4);
  std::ofstream output(path, std::ios::binary); output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}
struct FaultTransport : net::HttpTransport {
  net::HttpTransport* real = nullptr;
  bool offline = false, lost_commit = false, lost_abort = false, corrupt_download = false;
  int uploads = 0, fail_after = -1;
  std::string last_request;
  net::HttpResponse send(const net::HttpRequest& request, std::chrono::milliseconds timeout) override {
    if (request.url.find("/chunks/") == std::string::npos) last_request.assign(request.body.begin(), request.body.end());
    const bool uploading = request.method == "POST" && request.url.find("/chunks/") != std::string::npos;
    if (offline || !real || (uploading && fail_after >= 0 && uploads >= fail_after)) {
      net::HttpResponse response; response.transport_code = ResultCode::NetworkFailed; return response;
    }
    auto reply = real->send(request, timeout);
    if (uploading && reply.status_code == 204) ++uploads;
    if (lost_commit && request.url.find("/commit") != std::string::npos && reply.status_code == 200) {
      lost_commit = false; reply = {}; reply.transport_code = ResultCode::NetworkFailed;
    }
    if (lost_abort && request.method == "DELETE" && reply.status_code == 204) {
      lost_abort = false; reply = {}; reply.transport_code = ResultCode::NetworkFailed;
    }
    if (corrupt_download && request.method == "GET" && request.url.find("/chunks/") != std::string::npos && !reply.body.empty()) {
      corrupt_download = false; reply.body[0] ^= 0xff;
    }
    return reply;
  }
};
std::unique_ptr<ProjectSession> open(const fs::path& root, Session& session, Access& access) {
  std::unique_ptr<ProjectSession> projects; std::string diagnostic;
  assert(ProjectSession::open(root.string(), session, access, bookmark, projects, diagnostic) == ResultCode::Ok); return projects;
}
void assert_project(ProjectSession& projects, std::int64_t id, const fs::path& original_root, bool render) {
  std::vector<PhotoRow> photos; assert(projects.list_photos(id, photos) == ResultCode::Ok && photos.size() == 2);
  assert(photos.front().id > 0 && photos.back().id < 0);
  assert(photos.front().rating == 4 && photos.front().flag == kFlagPick && photos.front().color_label == kColorRed);
  for (const auto& photo : photos) {
    std::string source, diagnostic; assert(projects.resolve_photo(photo.id, source, diagnostic) == ResultCode::Ok);
    assert(source.find(original_root.string()) != 0);
    std::vector<std::uint8_t> bytes; assert(projects.document_bytes(photo.id, bytes) == ResultCode::Ok);
    auto document = EditDocument::decode(bytes); assert(document && document->image_id() == projects.image_id(photo.id));
    assert(document->committed_parameter("color.global", "exposure") == "1.00");
  }
  PosixFileSystem files;
  std::vector<std::uint8_t> layer_bytes;
  assert(files.read_file(projects.layer_document_path(photos.front().id), layer_bytes).code == ResultCode::Ok);
  const auto layer = LayerDocument::decode(layer_bytes); assert(layer && layer->generations().size() == 1);
  assert(layer->source_image_id() == projects.image_id(photos.front().id));
  const auto pixel = layer->generations().front().pixel_file;
  assert(pixel.find(original_root.string()) != 0 && fs::is_regular_file(pixel));
  assert(fs::file_size(pixel) == 64);
  std::string source, source_diagnostic;
  assert(projects.resolve_photo(photos.front().id, source, source_diagnostic) == ResultCode::Ok);
  platform::MacDecodedImage decoded;
  assert(platform::mac_decode_image(source, {320, 10000, 10000, 100000000}, decoded, source_diagnostic) == ResultCode::Ok);
  assert(decoded.source_width == 768 && decoded.source_height == 1024 && !decoded.rgba.empty());
  if (!render) return;
  // Actually reopen the downloaded original and its edited parameters through
  // the production preview session/renderd process, not only a metadata query.
  preview_channel::PreviewSessionOptions options;
  options.renderd_executable = PIXCAKE_RENDERD_EXE;
  options.document_directory = projects.document_directory();
  options.document_id = projects.document_name(photos.front().id); options.max_edge = 320;
  options.log_directory = (fs::path(projects.data_directory()) / "preview-logs").string();
  fs::create_directories(options.log_directory);
  std::string path, diagnostic; assert(projects.resolve_photo(photos.front().id, path, diagnostic) == ResultCode::Ok);
  std::unique_ptr<preview_channel::PreviewSession> preview;
  assert(preview_channel::PreviewSession::open(path, options, preview, diagnostic) == ResultCode::Ok);
  assert(preview->document().committed_parameter("color.global", "exposure") == "1.00");
  assert(preview->refresh_preview(diagnostic) == ResultCode::Ok);
  preview_channel::PreviewFrame frame;
  assert(preview->wait_latest_frame(frame, 15000, diagnostic) == ResultCode::Ok);
}
}
int main(int argc, char** argv) {
  const bool asgi = argc == 4 && std::string(argv[1]) == "--asgi";
  const bool tls = argc == 4 && !asgi;
  assert(argc == 1 || tls || asgi);
  char pattern[] = "/tmp/pixcake-sync-XXXXXX"; const char* made = ::mkdtemp(pattern); assert(made);
  const fs::path root = made;
  Session session; Access access; PosixFileSystem files;
  auto a = open(root / "a", session, access); auto b = open(root / "b", session, access);
  bmp(root / "source.bmp");
  const auto imported = a->import_new_project({(root / "source.bmp").string()}, "Sync project");
  assert(imported.code == ResultCode::Ok && imported.photo_ids.size() == 1);
  const auto id = imported.photo_ids.front();
  auto edit = EditDocument::create(a->image_id(id));
  assert(edit->set_preview("color.global", "exposure", "1.00") == ResultCode::Ok);
  assert(edit->commit_preview(0, "color.global", "exposure", phase_one_version_limits(), files,
    a->document_directory() + "/" + a->document_name(id) + ".pxedit") == ResultCode::Ok);
  std::int64_t copy = 0; std::string diagnostic;
  assert(a->create_virtual_copy(id, copy, diagnostic) == ResultCode::Ok);
  assert(a->set_rating({id}, 4) == ResultCode::Ok && a->set_flag({id}, kFlagPick) == ResultCode::Ok);
  assert(a->set_color({id}, kColorRed) == ResultCode::Ok);
  const auto pixel = root / "generated.rgba";
  assert(atomic_replace(files, pixel.string(), std::vector<std::uint8_t>(64, 0x3f)) == ResultCode::Ok);
  auto layer = LayerDocument::create(a->image_id(id), 1);
  assert(layer->place_result("result-test", "新图层", "tool-a", {}, pixel.string(), "fixture-checksum", 2, 2) == ResultCode::Ok);
  assert(layer->save(files, a->layer_document_path(id)) == ResultCode::Ok);
  ProjectSnapshot snapshot; assert(a->describe_snapshot(imported.project_id, snapshot) == ResultCode::Ok);
  std::string token(43, 'x'), space = "sp_" + std::string(32, 'a');
  const std::string origin = tls ? argv[1] : "https://sync.example.test";
  std::unique_ptr<AsgiTransport> bridge;
  if (asgi) { bridge = std::make_unique<AsgiTransport>(argv[2], argv[3]); token = bridge->token; space = bridge->space; }
  if (tls) { std::ifstream fixture(argv[3]); std::getline(fixture, token); std::getline(fixture, space); assert(fixture.good()); }
  net::CurlHttpTransport real(origin, tls ? argv[2] : "");
  FaultTransport transport; transport.real = tls ? static_cast<net::HttpTransport*>(&real) : bridge.get();
  std::atomic_bool cancelled{false};
  SyncEngine engine_a(a->data_directory(), origin, transport, cancelled);
  assert(engine_a.prepare_upload(snapshot, imported.project_id, space) == ResultCode::Ok);
  transport.offline = true;
  assert(engine_a.run(token).code == ResultCode::NetworkFailed && engine_a.pending());
  // Private capabilities and absolute filesystem paths must not enter the wire.
  assert(transport.last_request.find(root.string()) == std::string::npos);
  assert(transport.last_request.find("source_access") == std::string::npos);
  if (!tls && !asgi) {
    assert(engine_a.prepare_upload(snapshot, imported.project_id, space) == ResultCode::StateConflict);
    assert(engine_a.discard(token) == ResultCode::NetworkFailed && engine_a.pending());
    fs::create_directory(root / "cancel");
    SyncEngine cancel_engine((root / "cancel").string(), origin, transport, cancelled);
    cancelled = true;
    assert(cancel_engine.prepare_upload(snapshot, imported.project_id, space) == ResultCode::Cancelled && !cancel_engine.pending());
    a.reset(); b.reset(); fs::remove_all(root); std::cout << "Snapshot preparation and offline journal passed.\n"; return 0;
  }
  transport.offline = false; transport.fail_after = 1;
  assert(engine_a.run(token).code == ResultCode::NetworkFailed && transport.uploads == 1);
  transport.fail_after = -1; transport.lost_commit = true;
  SyncEngine resumed(a->data_directory(), origin, transport, cancelled);
  assert(resumed.run(token).code == ResultCode::NetworkFailed);
  auto uploaded = resumed.run(token); assert(uploaded.code == ResultCode::Ok && uploaded.revision == 1);
  assert(a->mark_uploaded(imported.project_id) == ResultCode::Ok);
  assert(resumed.complete(imported.project_id) == ResultCode::Ok && !resumed.pending());
  // Force different local IDs to exercise document/image relocation.
  const auto existing = b->import_new_project({(root / "source.bmp").string()}, "Existing local data");
  assert(existing.code == ResultCode::Ok);
  SyncEngine engine_b(b->data_directory(), origin, transport, cancelled);
  assert(engine_b.prepare_download(uploaded.project_id, space, 1) == ResultCode::Ok);
  transport.corrupt_download = true;
  assert(engine_b.run(token).code == ResultCode::NetworkFailed);
  SyncEngine downloaded_again(b->data_directory(), origin, transport, cancelled);
  auto downloaded = downloaded_again.run(token); assert(downloaded.code == ResultCode::Ok);
  std::int64_t local_b = 0;
  session.in = false;
  assert(b->restore_snapshot(downloaded.snapshot, downloaded.owned_root, downloaded.receipt, local_b) == ResultCode::StateConflict);
  session.in = true;
  auto damaged = downloaded.snapshot; damaged.photos.front().rating = 99;
  assert(b->restore_snapshot(damaged, downloaded.owned_root, downloaded.receipt, local_b) == ResultCode::StateConflict);
  std::vector<ProjectRow> projects;
  assert(b->list_projects(projects) == ResultCode::Ok && projects.size() == 1);
  // Fail after the first photo's document writes: the index and newly written
  // documents roll back together, leaving the pre-existing project untouched.
  const auto duplicate_source = fs::path(downloaded.owned_root) / "f99999998";
  fs::copy_file(downloaded.snapshot.photos.front().source_file, duplicate_source);
  damaged = downloaded.snapshot;
  auto bad_second = damaged.photos.front(); bad_second.source_file = duplicate_source.string(); bad_second.rating = 99;
  damaged.photos.push_back(std::move(bad_second));
  assert(b->restore_snapshot(damaged, downloaded.owned_root, downloaded.receipt, local_b) == ResultCode::StateConflict);
  const auto orphan_path = b->document_directory() + "/photo-2.pxedit";
  assert(!fs::exists(orphan_path) && b->list_projects(projects) == ResultCode::Ok && projects.size() == 1);
  fs::remove(duplicate_source);
  // Simulate power loss after a new document is durable but before SQLite
  // commits: retry may adopt identical bytes, never overwrite differing edits.
  std::vector<std::uint8_t> portable_edit;
  assert(files.read_file(downloaded.snapshot.photos.front().items.front().edit_file, portable_edit).code == ResultCode::Ok);
  const auto orphan = EditDocument::decode(portable_edit)->copy_for_image("photo-2");
  assert(orphan->write_snapshot(files, orphan_path) == ResultCode::Ok);
  assert(b->restore_snapshot(downloaded.snapshot, downloaded.owned_root, downloaded.receipt, local_b) == ResultCode::Ok);
  const auto restored_id = local_b;
  assert(b->restore_snapshot(downloaded.snapshot, downloaded.owned_root, downloaded.receipt, local_b) == ResultCode::Ok);
  assert(local_b == restored_id && b->list_projects(projects) == ResultCode::Ok && projects.size() == 2);
  assert(downloaded_again.complete(local_b) == ResultCode::Ok);
  assert_project(*b, local_b, root / "a", tls);
  ProjectSnapshot from_b; assert(b->describe_snapshot(local_b, from_b) == ResultCode::Ok);
  assert(engine_b.prepare_upload(from_b, local_b, space) == ResultCode::Ok);
  assert(engine_a.prepare_upload(snapshot, imported.project_id, space) == ResultCode::Ok);
  auto updated = engine_a.run(token); assert(updated.code == ResultCode::Ok && updated.revision == 2);
  assert(engine_a.complete(imported.project_id) == ResultCode::Ok);
  auto conflict = engine_b.run(token); assert(conflict.code == ResultCode::StateConflict && conflict.conflict && engine_b.pending());
  assert(engine_b.keep_both(token) == ResultCode::Ok);
  auto separate = engine_b.run(token);
  assert(separate.code == ResultCode::Ok && separate.revision == 1 && separate.project_id != uploaded.project_id);
  assert(engine_b.complete(local_b) == ResultCode::Ok);
  // A clear request survives offline/restart, and a lost DELETE response must
  // never turn it back into a resumed upload or remove a published revision.
  assert(engine_a.prepare_upload(snapshot, imported.project_id, space) == ResultCode::Ok);
  transport.fail_after = transport.uploads;
  assert(engine_a.run(token).code == ResultCode::NetworkFailed);
  transport.fail_after = -1; transport.offline = true;
  assert(engine_a.discard(token) == ResultCode::NetworkFailed && engine_a.pending());
  transport.offline = false; transport.lost_abort = true;
  SyncEngine clearing(a->data_directory(), origin, transport, cancelled);
  assert(clearing.run(token).code == ResultCode::NetworkFailed && clearing.pending());
  const auto cleared = clearing.run(token);
  assert(cleared.code == ResultCode::Ok && cleared.discarded && !clearing.pending());
  assert(engine_b.prepare_download(uploaded.project_id, space, 2) == ResultCode::Ok);
  assert(engine_b.run(token).code == ResultCode::Ok);
  assert(engine_b.discard(token) == ResultCode::Ok && !engine_b.pending());
  a.reset(); b.reset(); fs::remove_all(root);
  std::cout << (tls ? "HTTPS" : "Pipe/ASGI") << " two-library sync passed: originals, edits, copies, layers, restart, corruption, conflict.\n";
}
