// Export charging against the real exportd (test build rules, docs/superpowers/
// evidence/2026-11-07-export-credit-observation.md): a refined export charges
// one sheet per picture, by picture content -- the same picture re-encoded is
// not charged again, a flipped one is; originals and free-tool exports never
// charge; with no sheets left a new picture is refused before it is queued.
#include "pixcake/editdoc/edit_document.hpp"
#include "pixcake/export_session/export_charge.hpp"
#include "pixcake/export_session/export_session.hpp"
#include "pixcake/persist/file_system.hpp"
#include "pixcake/platform/mac_image_decoder.hpp"
#include "pixcake/platform/mac_image_encoder.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>

using namespace pixcake;
using namespace pixcake::export_session;

namespace {

struct Session final : SessionState {
  bool signed_in() const override { return true; }
};

struct PathAccess final : SourceAccess {
  ResolvedSource resolve(const std::vector<std::uint8_t>& bookmark) override {
    ResolvedSource out;
    out.absolute_path.assign(bookmark.begin(), bookmark.end());
    struct stat info {};
    out.code = ::stat(out.absolute_path.c_str(), &info) == 0 ? ResultCode::Ok : ResultCode::SourceMissing;
    return out;
  }
};

ResultCode path_bookmark(const std::string& path, std::vector<std::uint8_t>& out) {
  out.assign(path.begin(), path.end());
  return ResultCode::Ok;
}

// A wavy pattern on a ramp, mirrored when `flip`; RGBA rows.
std::vector<std::uint8_t> pattern(int w, int h, bool flip) {
  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * h * 4);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const int u = flip ? w - 1 - x : x;
      const int v = static_cast<int>(128 + 60 * std::sin(u / 3.0) * std::cos(y / 5.0) + u);
      std::uint8_t* p = rgba.data() + (static_cast<std::size_t>(y) * w + x) * 4;
      p[0] = static_cast<std::uint8_t>(v);
      p[1] = static_cast<std::uint8_t>(v / 2 + 20);
      p[2] = static_cast<std::uint8_t>(255 - v);
      p[3] = 255;
    }
  }
  return rgba;
}

void write_image(const std::string& path, platform::MacEncodeFormat format, double quality,
                 const std::vector<std::uint8_t>& rgba, int w, int h) {
  platform::MacEncodeRequest request;
  request.format = format;
  request.quality = quality;
  std::vector<std::uint8_t> encoded;
  std::string diagnostic;
  assert(platform::mac_encode_image(request, w, h, w * 4, rgba.data(), rgba.size(), encoded, diagnostic) ==
         ResultCode::Ok);
  std::ofstream(path, std::ios::binary)
      .write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
}

// Writes a document with one committed value (none when `capability` is null)
// to `path` and returns its bytes.
std::vector<std::uint8_t> document_at(const std::string& path, const std::string& image_id, const char* capability,
                                      const char* parameter, const char* value) {
  PosixFileSystem files;
  auto document = EditDocument::create(image_id);
  if (capability) {
    assert(document->set_preview(capability, parameter, value) == ResultCode::Ok);
    assert(document->commit_preview(0, capability, parameter, phase_one_version_limits(), files, path) ==
           ResultCode::Ok);
  }
  return document->encode();
}

// Waits until no task is queued or running.
std::vector<ExportRow> settle(ExportSession& exports) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
  for (;;) {
    exports.read_progress();
    auto rows = exports.rows();
    bool busy = false;
    for (const auto& row : rows) busy = busy || (row.status != "成功" && row.status != "失败" && row.status != "取消");
    if (!busy) {
      std::string diagnostic;
      exports.settle(diagnostic);
      return rows;
    }
    assert(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
}

}  // namespace

int main() {
  const std::string root = std::string(PIXCAKE_TEST_WORK_DIR) + "/export-charge-" + std::to_string(::getpid());
  ::mkdir(root.c_str(), 0700);
  const std::string shoot = root + "/shoot";
  ::mkdir(shoot.c_str(), 0700);
  const int w = 64;
  const int h = 48;
  write_image(shoot + "/p.png", platform::MacEncodeFormat::Png, 1.0, pattern(w, h, false), w, h);
  write_image(shoot + "/p-copy.jpg", platform::MacEncodeFormat::Jpeg, 0.7, pattern(w, h, false), w, h);
  write_image(shoot + "/q.png", platform::MacEncodeFormat::Png, 1.0, pattern(w, h, true), w, h);

  // The picture identity: re-encoding keeps it, a mirror does not.
  std::string p_id, copy_id, q_id, diagnostic;
  assert(picture_fingerprint(shoot + "/p.png", p_id, diagnostic) == ResultCode::Ok);
  assert(picture_fingerprint(shoot + "/p-copy.jpg", copy_id, diagnostic) == ResultCode::Ok);
  assert(picture_fingerprint(shoot + "/q.png", q_id, diagnostic) == ResultCode::Ok);
  std::printf("fingerprints\n  %s\n  %s\n  %s\n", p_id.c_str(), copy_id.c_str(), q_id.c_str());
  assert(p_id.rfind("pic-64x48-", 0) == 0 && same_picture(p_id, copy_id) && !same_picture(p_id, q_id));

  // Classification by the parameter snapshot.
  const std::string scratch = root + "/scratch.pxedit";
  assert(classify_export(document_at(scratch, "x", nullptr, nullptr, nullptr)) == ExportEffect::Original);
  ::unlink(scratch.c_str());
  assert(classify_export(document_at(scratch, "x", "color.global", "exposure", "1.00")) == ExportEffect::Free);
  ::unlink(scratch.c_str());
  assert(classify_export(document_at(scratch, "x", "color.global", "exposure", "0.00")) == ExportEffect::Original);
  ::unlink(scratch.c_str());
  assert(classify_export(document_at(scratch, "x", "color.post", "temperature", "20")) == ExportEffect::Free);
  ::unlink(scratch.c_str());
  assert(classify_export(document_at(scratch, "x", "color.local", "temperature", "20")) == ExportEffect::Refined);

  Session session;
  PathAccess access;
  std::unique_ptr<project_session::ProjectSession> projects;
  assert(project_session::ProjectSession::open(root + "/data", session, access, path_bookmark, projects,
                                               diagnostic) == ResultCode::Ok);
  const auto imported = projects->import_new_project({shoot});
  assert(imported.code == ResultCode::Ok && imported.photo_ids.size() == 3);
  std::map<std::string, std::int64_t> photo;
  for (const std::int64_t id : imported.photo_ids) {
    std::optional<PhotoRow> row;
    assert(projects->load_photo(id, row) == ResultCode::Ok && row);
    photo[row->file_name] = id;
  }
  const auto refine = [&](const std::string& name) {
    const std::string id = project_session::ProjectSession::document_id(photo[name]);
    document_at(projects->document_directory() + "/" + id + ".pxedit", id, "color.local", "temperature", "20");
  };
  refine("p.png");
  refine("p-copy.jpg");
  refine("q.png");

  ExportOptions options;
  options.exportd_executable = PIXCAKE_EXPORTD_EXE;
  options.log_directory = root + "/logs";
  options.data_directory = root + "/data";
  options.opening_sheets = 1;
  std::unique_ptr<ExportSession> exports;
  assert(ExportSession::open(*projects, session, options, exports, diagnostic) == ResultCode::Ok);
  assert(exports->remaining_sheets() == 1);

  // The dialog's estimate: three refined, two pictures.
  const ExportEstimate all = exports->estimate({photo["p.png"], photo["p-copy.jpg"], photo["q.png"]});
  assert(all.refined == 3 && all.free == 0 && all.original == 0 && all.sheets == 2);

  ExportRequest request;
  request.folder = root + "/out";
  ::mkdir(request.folder.c_str(), 0700);
  std::vector<std::string> notices;

  // First refined export of p: one sheet.
  request.photo_ids = {photo["p.png"]};
  assert(exports->submit(request, notices, diagnostic) == ResultCode::Ok && notices.empty());
  auto rows = settle(*exports);
  assert(rows[0].status == "成功");
  assert(exports->remaining_sheets() == 0);
  assert(exports->estimate({photo["p.png"]}).sheets == 0);

  // p again and its re-encoded copy: the same picture, nothing charged.
  request.photo_ids = {photo["p.png"], photo["p-copy.jpg"]};
  assert(exports->submit(request, notices, diagnostic) == ResultCode::Ok && notices.empty());
  rows = settle(*exports);
  assert(rows[0].status == "成功" && rows[1].status == "成功");
  assert(exports->remaining_sheets() == 0);

  // The mirrored picture is new: no sheet left, refused before it is queued.
  request.photo_ids = {photo["q.png"]};
  assert(exports->submit(request, notices, diagnostic) == ResultCode::StateConflict);
  assert(notices.size() == 1 && notices[0] == "q.png: 剩余张数不足");
  assert(exports->rows().size() == 3);

  // ...but with only free tools it exports, free.
  const std::string q_document = project_session::ProjectSession::document_id(photo["q.png"]);
  ::unlink((projects->document_directory() + "/" + q_document + ".pxedit").c_str());
  document_at(projects->document_directory() + "/" + q_document + ".pxedit", q_document, "color.global", "exposure",
              "0.50");
  const ExportEstimate free = exports->estimate({photo["q.png"]});
  assert(free.free == 1 && free.sheets == 0);
  assert(exports->submit(request, notices, diagnostic) == ResultCode::Ok && notices.empty());
  rows = settle(*exports);
  assert(rows[0].status == "成功" && exports->remaining_sheets() == 0);

  // The ledger survives a restart and the opening balance is not credited twice.
  exports.reset();
  assert(ExportSession::open(*projects, session, options, exports, diagnostic) == ResultCode::Ok);
  assert(exports->remaining_sheets() == 0 && exports->estimate({photo["p-copy.jpg"]}).sheets == 0);

  std::printf("export charge ok\n");
  return 0;
}
