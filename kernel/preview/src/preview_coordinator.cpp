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
  if (request.mode != PreviewMode::Original) {
    if (find_parameter_mode(request.capability_id, request.parameter_id) == nullptr ||
        !validate_parameter_display_value(request.capability_id, request.parameter_id, request.display_value)) {
      return ResultCode::StateConflict;
    }
    const ResultCode updated = document.set_preview(request.capability_id, request.parameter_id,
                                                    request.display_value);
    if (updated != ResultCode::Ok) {
      return updated;
    }
  }

  std::optional<std::uint64_t> replaced_revision;
  std::lock_guard<std::mutex> lock(mutex_);
  if (pending_.has_value()) replaced_revision = pending_->request.base_revision;
  PreviewSnapshot pending;
  pending.sequence = next_sequence_++;
  if (next_sequence_ == 0) next_sequence_ = 1;
  pending.request = request;
  pending_ = std::move(pending);
  if (replaced_revision.has_value() && log_ != nullptr) {
    LogRecord record;
    record.module = "preview";
    record.event = "preview.merged";
    record.revision = *replaced_revision;
    record.document_id = request.editing_instance_id;
    record.capability_id = request.capability_id;
    record.deduplicated = true;
    log_->write(record);
  }
  return ResultCode::Ok;
}

void PreviewCoordinator::set_log(LogSink* log) {
  std::lock_guard<std::mutex> lock(mutex_);
  log_ = log;
}

PreviewTakeResult PreviewCoordinator::take_latest(const EditDocument& document,
                                                  const CapabilityRegistry& registry) {
  const PreviewTakeResult result = take_latest_steps(document, registry);
  LogSink* log = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    log = log_;
  }
  if (log != nullptr && (result.code != ResultCode::Ok || result.snapshot.has_value())) {
    LogRecord record;
    record.module = "preview";
    record.event = "preview.taken";
    record.code = result.code;
    record.message = result_code_label(result.code);
    record.revision = document.revision();
    record.document_id = document.editing_instance_id();
    if (result.snapshot.has_value()) {
      for (const auto& diagnostic : result.snapshot->description.diagnostics) {
        if (diagnostic.blocking) record.message += "；" + diagnostic.reason;
      }
    }
    log->write(record);
  }
  return result;
}

PreviewTakeResult PreviewCoordinator::take_latest_steps(const EditDocument& document,
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
  if (pending.request.mode == PreviewMode::Original) {
    pending.description.image_id = document.image_id();
    pending.description.editing_instance_id = document.editing_instance_id();
    pending.description.document_revision = document.revision();
  } else {
    const auto compiled = render::compile(document, registry);
    if (compiled.code != ResultCode::Ok) {
      return {compiled.code, std::nullopt};
    }
    pending.description = compiled.description;
  }
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
