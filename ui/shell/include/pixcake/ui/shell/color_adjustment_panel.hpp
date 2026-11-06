#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pixcake::ui::shell {

// The provider is the seam to the separately-owned kernel parameter and
// render-description contract. UI code consumes these values and never
// republishes ranges or effect formulas.
struct ColorAdjustmentParameter {
  std::string capability_id;
  std::string parameter_id;
  std::string label;
  double minimum = 0.0;
  double maximum = 0.0;
  double step = 1.0;
  double default_value = 0.0;
  double value = 0.0;
  bool enabled = true;
  std::string disabled_reason;
};

struct ColorAdjustmentGroup {
  std::string id;
  std::string label;
  std::vector<ColorAdjustmentParameter> parameters;
  bool enabled = true;
  bool registered_for_drawing = true;
  std::string disabled_reason;
};

struct ColorAdjustmentPreviewUpdate {
  std::string capability_id;
  std::string parameter_id;
  double value = 0.0;
  std::uint64_t base_revision = 0;
};

using ColorAdjustmentProvider = std::function<std::vector<ColorAdjustmentGroup>()>;
using ColorAdjustmentPreviewSink = std::function<void(const ColorAdjustmentPreviewUpdate&)>;

class ColorAdjustmentPanelModel {
 public:
  ColorAdjustmentPanelModel(ColorAdjustmentProvider provider,
                            ColorAdjustmentPreviewSink preview_sink = {});

  // Refreshes from the provider. The provider remains the source of truth for
  // labels, ranges, steps, defaults, enabled state and drawing registration.
  void refresh();

  const std::vector<ColorAdjustmentGroup>& groups() const;
  const ColorAdjustmentGroup* find_group(std::string_view group_id) const;
  const ColorAdjustmentParameter* find_parameter(std::string_view capability_id,
                                                  std::string_view parameter_id) const;

  // Preview changes are transient UI state. They are sent to the renderer and
  // do not commit a document revision.
  bool set_preview_value(std::string_view capability_id, std::string_view parameter_id,
                         double value, std::uint64_t base_revision);

  // Hiding a group or parameter is a comparison affordance only. It does not
  // alter provider values or document state.
  bool set_group_visible(std::string_view group_id, bool visible);
  bool group_visible(std::string_view group_id) const;

 private:
  ColorAdjustmentProvider provider_;
  ColorAdjustmentPreviewSink preview_sink_;
  std::vector<ColorAdjustmentGroup> groups_;
  std::vector<bool> visible_;
};

}  // namespace pixcake::ui::shell
