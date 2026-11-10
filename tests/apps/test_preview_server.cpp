// renderd's preview server against the shell's client over a socketpair:
// source + request -> frame in shared memory; latest request wins; cancel;
// decode once per source; identity, document and file failures.
#include "pixcake/preview_channel/preview_client.hpp"
#include "pixcake/renderd/preview_server.hpp"

#include <sys/socket.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

using namespace pixcake;
using namespace pixcake::renderd;
using namespace pixcake::preview_channel;

// 24-bit BMP, every pixel grey 100.
void write_grey_bmp(const std::string& path, int w, int h) {
  const int row = (w * 3 + 3) / 4 * 4;
  const int size = 54 + row * h;
  std::vector<unsigned char> b(static_cast<std::size_t>(size), 0);
  auto put = [&](int at, int value, int bytes) {
    for (int i = 0; i < bytes; ++i) b[static_cast<std::size_t>(at + i)] = static_cast<unsigned char>(value >> (8 * i));
  };
  b[0] = 'B'; b[1] = 'M';
  put(2, size, 4); put(10, 54, 4); put(14, 40, 4); put(18, w, 4); put(22, h, 4);
  put(26, 1, 2); put(28, 24, 2); put(34, row * h, 4);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w * 3; ++x) b[static_cast<std::size_t>(54 + y * row + x)] = 100;
  }
  std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), size);
}

ipc::PreviewSourceMessage source_for(const EditDocument& document, const std::string& path) {
  ipc::PreviewSourceMessage source;
  source.image_id = document.image_id();
  source.editing_instance_id = document.editing_instance_id();
  source.document_revision = document.revision();
  source.source_path = path;
  source.document = document.encode();
  return source;
}

ipc::PreviewRequestMessage request_for(const EditDocument& document, std::uint64_t sequence,
                                       const std::string& exposure, std::uint32_t max_edge = 4000) {
  ipc::PreviewRequestMessage message;
  message.client_sequence = sequence;
  message.request.image_id = document.image_id();
  message.request.editing_instance_id = document.editing_instance_id();
  message.request.base_revision = document.revision();
  message.request.capability_id = "color.global";
  message.request.parameter_id = "exposure";
  message.request.display_value = exposure;
  message.request.max_edge = max_edge;
  return message;
}

}  // namespace

int main() {
  const std::string bmp = "/tmp/pixcake-preview-server-" + std::to_string(::getpid()) + ".bmp";
  write_grey_bmp(bmp, 64, 32);

  int pair[2];
  assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
  PreviewClient client{Channel{UniqueFd(pair[0])}};
  Channel server_side{UniqueFd(pair[1])};
  RenderService service;
  auto decoder = make_system_image_decoder();
  assert(decoder != nullptr);
  PreviewServer server(service, decoder.get());
  std::string diagnostic;
  PreviewFrame frame;

  const auto document = *EditDocument::create("image-1");

  // Nothing sent yet.
  assert(server.step(server_side, 0, diagnostic) == ResultCode::Cancelled);

  // A request before any source is answered with a reason, not a frame.
  assert(client.send_request(request_for(document, 1, "0"), diagnostic) == ResultCode::Ok);
  assert(server.step(server_side, 1000, diagnostic) == ResultCode::Ok);
  assert(client.receive(frame, 1000, diagnostic) == ResultCode::Ok);
  assert(frame.result.client_sequence == 1 && frame.result.code == ResultCode::StateConflict);
  assert(frame.rgba.empty() && !frame.result.diagnostic.empty());

  // Source then request: a frame in shared memory, brighter with exposure.
  assert(client.send_source(source_for(document, bmp), diagnostic) == ResultCode::Ok);
  assert(client.send_request(request_for(document, 2, "0"), diagnostic) == ResultCode::Ok);
  assert(server.step(server_side, 1000, diagnostic) == ResultCode::Ok);
  assert(client.receive(frame, 1000, diagnostic) == ResultCode::Ok);
  assert(frame.result.code == ResultCode::Ok && frame.result.client_sequence == 2);
  assert(frame.result.header.width == 64 && frame.result.header.height == 32 && frame.result.header.stride == 256);
  assert(frame.rgba.size() == 256u * 32u);
  const int base = frame.rgba[0];
  assert(base >= 99 && base <= 101 && frame.rgba[3] == 255);

  // Three slider moves arrive before renderd looks: only the last is rendered.
  assert(client.send_request(request_for(document, 3, "0.5"), diagnostic) == ResultCode::Ok);
  assert(client.send_request(request_for(document, 4, "0.75"), diagnostic) == ResultCode::Ok);
  assert(client.send_request(request_for(document, 5, "1"), diagnostic) == ResultCode::Ok);
  assert(server.step(server_side, 1000, diagnostic) == ResultCode::Ok);
  assert(client.receive(frame, 1000, diagnostic) == ResultCode::Ok);
  assert(frame.result.code == ResultCode::Ok && frame.result.client_sequence == 5);
  assert(frame.rgba[0] > base + 30);  // +1 EV doubles linear light: sRGB 100 -> 137
  assert(client.receive(frame, 50, diagnostic) == ResultCode::Cancelled);  // no result for 3 and 4
  assert(server.decodes() == 1);  // one decode for all of them

  // A cancelled request is not rendered.
  assert(client.send_request(request_for(document, 6, "2"), diagnostic) == ResultCode::Ok);
  ipc::PreviewCancelMessage cancel;
  cancel.client_sequence = 6;
  cancel.image_id = document.image_id();
  cancel.editing_instance_id = document.editing_instance_id();
  cancel.base_revision = document.revision();
  assert(client.send_cancel(cancel, diagnostic) == ResultCode::Ok);
  assert(server.step(server_side, 1000, diagnostic) == ResultCode::Ok);
  assert(client.receive(frame, 50, diagnostic) == ResultCode::Cancelled);

  // A smaller preview edge decodes again, downscaled by the decoder.
  assert(client.send_request(request_for(document, 7, "0", 16), diagnostic) == ResultCode::Ok);
  assert(server.step(server_side, 1000, diagnostic) == ResultCode::Ok);
  assert(client.receive(frame, 1000, diagnostic) == ResultCode::Ok);
  assert(frame.result.code == ResultCode::Ok && frame.result.header.width == 16 && frame.result.header.height == 8);
  assert(server.decodes() == 2);

  // Wrong revision / other image: conflict.
  auto stale = request_for(document, 8, "0");
  stale.request.base_revision += 1;
  assert(client.send_request(stale, diagnostic) == ResultCode::Ok);
  assert(server.step(server_side, 1000, diagnostic) == ResultCode::Ok);
  assert(client.receive(frame, 1000, diagnostic) == ResultCode::Ok);
  assert(frame.result.code == ResultCode::StateConflict && frame.rgba.empty());

  // A parameter that does not exist cannot be previewed.
  auto unknown = request_for(document, 9, "1");
  unknown.request.parameter_id = "no_such_parameter";
  assert(client.send_request(unknown, diagnostic) == ResultCode::Ok);
  assert(server.step(server_side, 1000, diagnostic) == ResultCode::Ok);
  assert(client.receive(frame, 1000, diagnostic) == ResultCode::Ok);
  assert(frame.result.client_sequence == 9);

  // Corrupt document bytes.
  auto corrupt = source_for(document, bmp);
  corrupt.document = {1, 2, 3};
  assert(client.send_source(corrupt, diagnostic) == ResultCode::Ok);
  assert(client.send_request(request_for(document, 10, "0"), diagnostic) == ResultCode::Ok);
  assert(server.step(server_side, 1000, diagnostic) == ResultCode::Ok);
  assert(client.receive(frame, 1000, diagnostic) == ResultCode::Ok);
  assert(frame.result.code == ResultCode::DocumentCorrupt);

  // The editing instance comes from the source message (it is not in the
  // document bytes): a copy edited as another instance renders fine.
  auto other_instance = source_for(document, bmp);
  other_instance.editing_instance_id = "copy-2";
  assert(client.send_source(other_instance, diagnostic) == ResultCode::Ok);
  auto for_copy = request_for(document, 100, "0");
  for_copy.request.editing_instance_id = "copy-2";
  assert(client.send_request(for_copy, diagnostic) == ResultCode::Ok);
  assert(server.step(server_side, 1000, diagnostic) == ResultCode::Ok);
  assert(client.receive(frame, 1000, diagnostic) == ResultCode::Ok);
  assert(frame.result.code == ResultCode::Ok);
  // ...and a request for the previous instance is now a conflict.
  assert(client.send_request(request_for(document, 101, "0"), diagnostic) == ResultCode::Ok);
  assert(server.step(server_side, 1000, diagnostic) == ResultCode::Ok);
  assert(client.receive(frame, 1000, diagnostic) == ResultCode::Ok);
  assert(frame.result.code == ResultCode::StateConflict);

  // Source identity differs from its document.
  auto mismatched = source_for(document, bmp);
  mismatched.document_revision += 5;
  assert(client.send_source(mismatched, diagnostic) == ResultCode::Ok);
  auto for_mismatch = request_for(document, 11, "0");
  for_mismatch.request.base_revision = mismatched.document_revision;
  assert(client.send_request(for_mismatch, diagnostic) == ResultCode::Ok);
  assert(server.step(server_side, 1000, diagnostic) == ResultCode::Ok);
  assert(client.receive(frame, 1000, diagnostic) == ResultCode::Ok);
  assert(frame.result.code == ResultCode::StateConflict);

  // Thumbnails queue behind the preview: a step answers the preview first,
  // then one thumbnail; every thumbnail is answered in later steps.
  {
    assert(client.send_source(source_for(document, bmp), diagnostic) == ResultCode::Ok);
    for (std::uint64_t id = 1; id <= 3; ++id) {
      ipc::ThumbnailRequestMessage thumbnail;
      thumbnail.request_id = id;
      thumbnail.image_id = "image-1";
      thumbnail.source_path = bmp;
      thumbnail.max_edge = 8;
      assert(client.send_thumbnail(thumbnail, diagnostic) == ResultCode::Ok);
    }
    assert(client.send_request(request_for(document, 200, "0"), diagnostic) == ResultCode::Ok);
    assert(server.step(server_side, 1000, diagnostic) == ResultCode::Ok);
    assert(client.receive(frame, 1000, diagnostic) == ResultCode::Ok);
    assert(!frame.thumbnail && frame.result.client_sequence == 200 && frame.result.code == ResultCode::Ok);
    assert(client.receive(frame, 1000, diagnostic) == ResultCode::Ok);
    assert(frame.thumbnail && frame.result.client_sequence == 1 && frame.result.header.width == 8);
    assert(server.queued_thumbnails() == 2);
    assert(server.step(server_side, 0, diagnostic) == ResultCode::Ok);
    assert(server.step(server_side, 0, diagnostic) == ResultCode::Ok);
    assert(client.receive(frame, 1000, diagnostic) == ResultCode::Ok && frame.result.client_sequence == 2);
    assert(client.receive(frame, 1000, diagnostic) == ResultCode::Ok && frame.result.client_sequence == 3);
    assert(server.step(server_side, 0, diagnostic) == ResultCode::Cancelled);
  }

  // The original went away.
  std::remove(bmp.c_str());
  assert(client.send_source(source_for(document, bmp), diagnostic) == ResultCode::Ok);
  assert(client.send_request(request_for(document, 12, "0"), diagnostic) == ResultCode::Ok);
  assert(server.step(server_side, 1000, diagnostic) == ResultCode::Ok);
  assert(client.receive(frame, 1000, diagnostic) == ResultCode::Ok);
  assert(frame.result.code == ResultCode::SourceMissing && frame.rgba.empty());

  // The shell closes: serve() ends normally.
  client = PreviewClient{};
  assert(server.serve(server_side, diagnostic) == ResultCode::Ok);
  return 0;
}
