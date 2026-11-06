#include "pixcake/preview/preview_coordinator.hpp"

namespace pixcake::preview {

ResultCode PreviewCoordinator::submit(EditDocument& document, const PreviewRequest& request,
                                      const FastExportLatch* latch) {
  if (latch != nullptr && latch->enabled()) {
    return ResultCode::FastExportBusy;
  }
  if (request.image_id.empty() || request.editing_instance_id.empty() ||
      request.image_id != document.image_id() ||
      request.editing_instance_id != document.editing_instance_id() ||
      request.base_revision != document.revision() || request.max_edge == 0 || request.max_edge > 4000 ||
      request.capability_id.empty() || request.parameter_id.empty()) {
    return ResultCode::StateConflict;
  }
  if (find_parameter_mode(request.capability_id, request.parameter_id) == nullptr ||
      !validate_parameter_display_value(request.capability_id, request.parameter_id, request.display_value)) {
    return ResultCode::StateConflict;
  }
  const ResultCode updated = document.set_preview(request.capability_id, request.parameter_id,
                                                  request.display_value);
  if (updated != ResultCode::Ok) {
    return updated;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  PreviewSnapshot pending;
  pending.sequence = next_sequence_++;
  if (next_sequence_ == 0) next_sequence_ = 1;
  pending.request = request;
  pending_ = std::move(pending);
  return ResultCode::Ok;
}

PreviewTakeResult PreviewCoordinator::take_latest(const EditDocument& document,
                                                  const CapabilityRegistry& registry) {
  PreviewSnapshot pending;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pending_.has_value()) return {};
    pending = std::move(*pending_);
    pending_.reset();
  }
  if (pending.request.image_id != document.image_id() ||
      pending.request.editing_instance_id != document.editing_instance_id() ||
      pending.request.base_revision != document.revision()) {
    return {ResultCode::StateConflict, std::nullopt};
  }
  const auto compiled = render::compile(document, registry);
  if (compiled.code != ResultCode::Ok) {
    return {compiled.code, std::nullopt};
  }
  pending.description = compiled.description;
  return {ResultCode::Ok, std::move(pending)};
}

void PreviewCoordinator::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  pending_.reset();
}

std::uint64_t PreviewCoordinator::latest_sequence() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pending_ ? pending_->sequence : 0;
}

}  // namespace pixcake::preview
