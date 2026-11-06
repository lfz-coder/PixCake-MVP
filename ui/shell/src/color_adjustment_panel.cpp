#include "pixcake/ui/shell/color_adjustment_panel.hpp"

#include <cmath>
#include <limits>

namespace pixcake::ui::shell {
namespace {

bool valid_value(const ColorAdjustmentParameter& parameter, double value) {
  if (!std::isfinite(value) || !std::isfinite(parameter.minimum) ||
      !std::isfinite(parameter.maximum) || !std::isfinite(parameter.step) ||
      parameter.step <= 0.0 || parameter.minimum > parameter.maximum) {
    return false;
  }
  if (value < parameter.minimum || value > parameter.maximum) return false;
  const double steps = (value - parameter.minimum) / parameter.step;
  return std::abs(steps - std::round(steps)) <= 1e-9;
}

}  // namespace

ColorAdjustmentPanelModel::ColorAdjustmentPanelModel(ColorAdjustmentProvider provider,
                                                     ColorAdjustmentPreviewSink preview_sink)
    : provider_(std::move(provider)), preview_sink_(std::move(preview_sink)) {
  refresh();
}

void ColorAdjustmentPanelModel::refresh() {
  groups_ = provider_ ? provider_() : std::vector<ColorAdjustmentGroup>{};
  visible_.assign(groups_.size(), true);
}

const std::vector<ColorAdjustmentGroup>& ColorAdjustmentPanelModel::groups() const { return groups_; }

const ColorAdjustmentGroup* ColorAdjustmentPanelModel::find_group(std::string_view group_id) const {
  for (const auto& group : groups_) {
    if (group.id == group_id) return &group;
  }
  return nullptr;
}

const ColorAdjustmentParameter* ColorAdjustmentPanelModel::find_parameter(
    std::string_view capability_id, std::string_view parameter_id) const {
  for (const auto& group : groups_) {
    for (const auto& parameter : group.parameters) {
      if (parameter.capability_id == capability_id && parameter.parameter_id == parameter_id) {
        return &parameter;
      }
    }
  }
  return nullptr;
}

bool ColorAdjustmentPanelModel::set_preview_value(std::string_view capability_id,
                                                   std::string_view parameter_id, double value,
                                                   std::uint64_t base_revision) {
  const auto* parameter = find_parameter(capability_id, parameter_id);
  if (parameter == nullptr || !parameter->enabled || !valid_value(*parameter, value)) return false;
  if (!preview_sink_) return true;
  preview_sink_(ColorAdjustmentPreviewUpdate{std::string(capability_id), std::string(parameter_id),
                                              value, base_revision});
  return true;
}

bool ColorAdjustmentPanelModel::set_group_visible(std::string_view group_id, bool visible) {
  for (std::size_t index = 0; index < groups_.size(); ++index) {
    if (groups_[index].id == group_id) {
      visible_[index] = visible;
      return true;
    }
  }
  return false;
}

bool ColorAdjustmentPanelModel::group_visible(std::string_view group_id) const {
  for (std::size_t index = 0; index < groups_.size(); ++index) {
    if (groups_[index].id == group_id) return visible_[index];
  }
  return false;
}

}  // namespace pixcake::ui::shell
