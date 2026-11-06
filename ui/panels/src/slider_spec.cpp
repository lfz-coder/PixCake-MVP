#include "pixcake/ui/panels/slider_spec.hpp"

#include "pixcake/capability/capability_registry.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <utility>

namespace pixcake::ui::panels {
namespace {

constexpr char kGlobalColor[] = "color.global";

struct LabelBinding {
  std::string_view label;
  std::string_view parameter_id;
};

constexpr std::array<LabelBinding, 14> kTopLevelColor = {{
    {"色温", "temperature"}, {"色调", "tint"},          {"曝光", "exposure"},  {"对比度", "contrast"},
    {"亮度", "brightness"},  {"高光", "highlights"},    {"阴影", "shadows"},   {"白色", "whites"},
    {"黑色", "blacks"},      {"纹理", "texture"},       {"清晰度", "clarity"}, {"祛雾", "dehaze"},
    {"鲜艳度", "vibrance"},  {"饱和度", "saturation"},
}};

int decimals_for(double step) {
  for (int places = 0; places <= 4; ++places) {
    const double scaled = step * std::pow(10.0, places);
    if (std::abs(scaled - std::round(scaled)) < 1e-9) return places;
  }
  return 4;
}

std::string format_value(double value, int decimals) {
  const double scale = std::pow(10.0, decimals);
  double rounded = std::round(value * scale) / scale;
  if (rounded == 0.0) rounded = 0.0;  // 不输出 -0
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, rounded);
  return buffer;
}

}  // namespace

std::optional<SliderBinding> refine_color_binding(std::string_view label) {
  for (const auto& entry : kTopLevelColor) {
    if (entry.label == label) {
      return SliderBinding{kGlobalColor, std::string(entry.parameter_id)};
    }
  }
  return std::nullopt;
}

std::optional<SliderBinding> refine_local_binding(std::string_view label) {
  if (label == "羽化") return SliderBinding{"mask", "feather"};
  return std::nullopt;
}

SliderBinding refine_crop_angle_binding() { return SliderBinding{"crop", "angle"}; }

SliderSpec slider_spec(std::string label, const std::optional<SliderBinding>& binding) {
  SliderSpec spec;
  spec.label = std::move(label);
  const ParameterMode* mode =
      binding ? find_parameter_mode(binding->capability_id, binding->parameter_id) : nullptr;
  if (mode == nullptr || mode->step <= 0.0 || mode->maximum < mode->minimum) {
    spec.default_text = "—";
    spec.accessible_name = spec.label + "，范围待确认";
    return spec;
  }
  spec.bound = true;
  spec.minimum = mode->minimum;
  spec.maximum = mode->maximum;
  spec.step = mode->step;
  spec.default_value = mode->default_value;
  spec.decimals = decimals_for(mode->step);
  spec.tick_count = static_cast<int>(std::lround((mode->maximum - mode->minimum) / mode->step));
  spec.default_tick = static_cast<int>(std::lround((mode->default_value - mode->minimum) / mode->step));
  spec.default_text = format_value(mode->default_value, spec.decimals);
  // 参数模式标明步进或默认值没有来源时，只写“待确认”，不把内部取值当成事实展示。
  const std::string step_text =
      mode->step_confirmed ? "步进 " + format_value(mode->step, spec.decimals) : std::string("步进待确认");
  const std::string default_text =
      mode->default_confirmed ? "默认 " + spec.default_text : std::string("默认待确认");
  spec.accessible_name = spec.label + "，范围 " + format_value(mode->minimum, spec.decimals) + " 到 " +
                         format_value(mode->maximum, spec.decimals) + "，" + step_text + "，" + default_text;
  return spec;
}

}  // namespace pixcake::ui::panels
