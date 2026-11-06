#include "pixcake/history/history_log.hpp"

#include <utility>

namespace pixcake {

ResultCode HistoryLog::record_committed(const std::string& image_id, const EditDocument& document,
                                        std::string capability_id, std::string parameter_id,
                                        std::string display_value, bool batch_design_busy) {
  if (batch_design_busy || image_id.empty() || document.image_id() != image_id) {
    return ResultCode::StateConflict;
  }
  std::string history_id = document.editing_instance_id();
  auto existing = images_.find(history_id);
  if (existing == images_.end()) {
    existing = images_.find(image_id);
    if (existing != images_.end()) history_id = image_id;
  }
  if (existing != images_.end() && !existing->second.steps.empty() &&
      document.revision() <= existing->second.steps.back().revision) {
    return ResultCode::StateConflict;
  }

  HistoryStep step;
  step.revision = document.revision();
  step.capability_id = std::move(capability_id);
  step.parameter_id = std::move(parameter_id);
  step.display_value = std::move(display_value);
  step.snapshot = document.encode();

  auto& image = images_[history_id];
  image.steps.push_back(std::move(step));
  image.current = image.steps.size() - 1;
  return ResultCode::Ok;
}

ResultCode HistoryLog::select(const std::string& image_id, EditDocument& document, std::size_t index,
                              bool batch_design_busy) {
  if (batch_design_busy || image_id.empty()) {
    return ResultCode::StateConflict;
  }
  auto found = images_.find(document.editing_instance_id());
  if (found == images_.end()) found = images_.find(image_id);
  if (found == images_.end() || index >= found->second.steps.size()) {
    return ResultCode::StateConflict;
  }
  if (document.image_id() != image_id) {
    return ResultCode::StateConflict;
  }
  auto decoded = EditDocument::decode(found->second.steps[index].snapshot);
  if (!decoded) {
    return ResultCode::DocumentCorrupt;
  }
  const std::string instance = document.editing_instance_id();
  document = std::move(*decoded);
  document.set_editing_instance_id(instance);
  // Selecting an older content snapshot never rewinds the committed
  // document generation.  The next commit must use a new monotonic value.
  // Keep the document's committed generation monotonic across a history
  // branch.  Selecting older content changes payload only; the next commit
  // must advance beyond the newest recorded step.
  document.set_revision(found->second.steps.back().revision);
  found->second.current = index;
  return ResultCode::Ok;
}

ResultCode HistoryLog::reopen_folded(const std::string& image_id, const EditDocument& loaded) {
  if (image_id.empty() || loaded.image_id() != image_id) {
    return ResultCode::StateConflict;
  }
  HistoryStep step;
  step.revision = loaded.revision();
  step.label = kHistoryFoldedLabel;
  step.snapshot = loaded.encode();
  ImageSteps image;
  image.steps.push_back(std::move(step));
  image.current = 0;
  std::string history_id = loaded.editing_instance_id();
  if (images_.find(image_id) != images_.end()) history_id = image_id;
  images_[history_id] = std::move(image);
  return ResultCode::Ok;
}

const std::vector<HistoryStep>* HistoryLog::steps(const std::string& image_id) const {
  const auto found = images_.find(image_id);
  if (found == images_.end()) {
    return nullptr;
  }
  return &found->second.steps;
}

std::optional<std::size_t> HistoryLog::current_index(const std::string& image_id) const {
  const auto found = images_.find(image_id);
  if (found == images_.end()) {
    return std::nullopt;
  }
  return found->second.current;
}

}  // namespace pixcake
