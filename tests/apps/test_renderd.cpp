#include "pixcake/renderd/render_service.hpp"

#include <cassert>

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
  const auto unsupported = service.render_latest();
  assert(unsupported.code == pixcake::ResultCode::UnsupportedFormat);

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
  return 0;
}
