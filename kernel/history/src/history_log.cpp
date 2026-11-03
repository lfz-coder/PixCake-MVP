#include "pixcake/history/history_log.hpp"

#include <utility>

namespace pixcake {

ResultCode HistoryLog::record_committed(const std::string& image_id, const EditDocument& document,
                                        std::string capability_id, std::string parameter_id,
                                        std::string display_value, bool batch_design_busy) {
  if (batch_design_busy || image_id.empty() || document.image_id() != image_id) {
    return ResultCode::StateConflict;
  }
  const auto existing = images_.find(image_id);
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

  auto& image = images_[image_id];
  image.steps.push_back(std::move(step));
  image.current = image.steps.size() - 1;
  return ResultCode::Ok;
}

ResultCode HistoryLog::select(const std::string& image_id, EditDocument& document, std::size_t index,
                              bool batch_design_busy) {
  if (batch_design_busy || image_id.empty()) {
    return ResultCode::StateConflict;
  }
  const auto found = images_.find(image_id);
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
  document = std::move(*decoded);
  found->second.current = index;
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
