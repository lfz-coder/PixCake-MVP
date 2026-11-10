#include "pixcake/renderd/render_service.hpp"

#include "pixcake/gl/native_backend.hpp"
#include "pixcake/support/checksum.hpp"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <unistd.h>

namespace {

pixcake::renderd::PreviewWork work(std::uint64_t sequence, std::uint8_t marker) {
  pixcake::renderd::PreviewWork value;
  value.client_sequence = sequence;
  value.request.image_id = "image-1";
  value.request.editing_instance_id = "edit-1";
  value.request.base_revision = 7;
  value.request.capability_id = "color.global";
  value.request.parameter_id = "exposure";
  value.request.display_value = "0";
  value.request.max_edge = 4000;
  value.description.image_id = "image-1";
  value.description.editing_instance_id = "edit-1";
  value.description.document_revision = 7;
  value.input.width = 2;
  value.input.height = 1;
  value.input.stride = 8;
  value.input.bytes = {marker, 2, 3, 255, 4, 5, 6, 255};
  return value;
}

}  // namespace

int main() {
  pixcake::renderd::RenderService service;
  std::string diagnostic;
  assert(service.submit(work(1, 1), diagnostic) == pixcake::ResultCode::Ok);
  assert(service.submit(work(2, 9), diagnostic) == pixcake::ResultCode::Ok);
  assert(service.has_pending());

  const auto latest = service.render_latest();
  assert(latest.code == pixcake::ResultCode::Ok);
  assert(latest.client_sequence == 2);
  assert(latest.header.width == 2);
  assert(latest.header.height == 1);
  assert(latest.rgba.front() == 9);
  assert(!service.has_pending());

  auto effect = work(3, 1);
  effect.description.operations.push_back({"color.global", "global", "pixcake.color.global", 1, {}, {},
                                           "normal", true, {}});
  assert(service.submit(std::move(effect), diagnostic) == pixcake::ResultCode::Ok);
  // The shaders draw the global colour tool when this machine can make an
  // offscreen context (else the reference does); every operation's outcome
  // is reported.  No values: the frame is unchanged.
  const auto rendered = service.render_latest();
  assert(rendered.code == pixcake::ResultCode::Ok);
  assert(rendered.client_sequence == 3);
  assert(rendered.rgba.size() == 8 && rendered.rgba.front() == 1);
  assert(rendered.statuses.size() == 1 && rendered.statuses.front().capability_id == "color.global");
  auto probe = pixcake::gl::native::create_context();
  const bool gpu = pixcake::gl::native::available() && probe.makeCurrent();
  if (gpu) probe.doneCurrent();
  assert(rendered.drawn_on_gpu == gpu);

  // Grain samples full-resolution coordinates: a half-size preview of a 16 px wide source shows,
  // at (x, y), the grain the full-resolution frame has at (2x, 2y).
  {
    const auto grey = [](std::uint64_t sequence, std::uint32_t width, std::uint32_t height) {
      auto value = work(sequence, 128);
      value.request.capability_id = "detail";
      value.request.parameter_id = "grain_amount";
      value.input.width = width;
      value.input.height = height;
      value.input.stride = width * 4;
      value.input.bytes.assign(static_cast<std::size_t>(width) * height * 4, 128);
      value.input.source_long_edge = 16;
      value.description.operations.push_back({"detail", "detail.grain", "pixcake.detail", 1,
                                              {{"grain_amount", "grain_amount", "100", 100.0},
                                               {"grain_size", "grain_size", "40", 40.0},
                                               {"grain_color", "grain_color", "0", 0.0}},
                                              {}, "normal", true, {}});
      return value;
    };
    assert(service.submit(grey(40, 16, 8), diagnostic) == pixcake::ResultCode::Ok);
    const auto full = service.render_latest();
    assert(service.submit(grey(41, 8, 4), diagnostic) == pixcake::ResultCode::Ok);
    const auto half = service.render_latest();
    assert(full.code == pixcake::ResultCode::Ok && half.code == pixcake::ResultCode::Ok);
    for (std::uint32_t y = 0; y < 4; ++y)
      for (std::uint32_t x = 0; x < 8; ++x)
        assert(half.rgba[(y * half.header.stride) + x * 4] == full.rgba[(2 * y * full.header.stride) + 2 * x * 4]);
  }

  auto unregistered = work(31, 1);
  unregistered.description.operations.push_back({"portrait.skin", "portrait", "pixcake.portrait.skin", 1, {}, {},
                                                 "normal", true, {}});
  assert(service.submit(std::move(unregistered), diagnostic) == pixcake::ResultCode::Ok);
  const auto skipped = service.render_latest();
  assert(skipped.code == pixcake::ResultCode::Ok);
  assert(skipped.statuses.front().status == pixcake::gpu::PassStatus::Unregistered);
  assert(!skipped.diagnostics.empty());

  auto too_large = work(4, 1);
  too_large.input.width = 4001;
  too_large.input.stride = 4001 * 4;
  too_large.input.bytes.resize(static_cast<std::size_t>(too_large.input.stride));
  too_large.request.max_edge = 4000;
  assert(service.submit(std::move(too_large), diagnostic) == pixcake::ResultCode::Ok);
  const auto no_resample = service.render_latest();
  assert(no_resample.code == pixcake::ResultCode::UnsupportedFormat);

  auto mismatch = work(5, 1);
  mismatch.description.document_revision = 8;
  assert(service.submit(std::move(mismatch), diagnostic) == pixcake::ResultCode::StateConflict);

  assert(service.submit(work(6, 1), diagnostic) == pixcake::ResultCode::Ok);
  // Q-25：取消直接用 kernel/ipc 的取消消息，服务不再另列一套字段。
  pixcake::ipc::PreviewCancelMessage stale;
  stale.client_sequence = 5;
  stale.image_id = "image-1";
  stale.editing_instance_id = "edit-1";
  stale.base_revision = 7;
  assert(service.cancel(stale, diagnostic) == pixcake::ResultCode::StateConflict);
  assert(service.has_pending());
  auto current = stale;
  current.client_sequence = 6;
  assert(service.cancel(current, diagnostic) == pixcake::ResultCode::Ok);
  assert(!service.has_pending());
  assert(service.cancel(current, diagnostic) == pixcake::ResultCode::StateConflict);
  assert(service.render_latest().code == pixcake::ResultCode::StateConflict);

  // A verified creative result is loaded by renderd and composited after the
  // retouch operations.  The fixture is 1x1 and is deterministically expanded
  // to the 2x1 source frame.
  {
    const std::string path = "/tmp/pixcake-renderd-generated-" + std::to_string(::getpid()) + ".result";
    const std::vector<std::uint8_t> pixels = {255, 0, 0, 255};
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
    auto generated = work(7, 10);
    generated.description.operations.clear();
    generated.description.layers.push_back({"generated-1", {}, 0, "normal", 100, false, false, false, 100, 0,
                                             true, {}, path, "", 1, 1});
    char checksum[17];
    std::snprintf(checksum, sizeof(checksum), "%016llx",
                  static_cast<unsigned long long>(pixcake::checksum_fnv1a64(pixels.data(), pixels.size())));
    generated.description.layers.back().pixel_checksum = checksum;
    const auto composited = service.render_now(7, "image-1", generated.description, generated.input, 4000);
    assert(composited.code == pixcake::ResultCode::Ok);
    assert(composited.rgba.size() == 8 && composited.rgba[0] == 255 && composited.rgba[1] == 0 &&
           composited.rgba[4] == 255 && composited.rgba[5] == 0);
    std::remove(path.c_str());
  }
  return 0;
}
