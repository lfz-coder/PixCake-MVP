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

namespace {

constexpr std::array<LabelBinding, 5> kLevels = {{
    {"黑场", "levels_rgb_input_black"},
    {"灰场", "levels_rgb_input_gray"},
    {"白场", "levels_rgb_input_white"},
    {"输出黑场", "levels_rgb_output_black"},
    {"输出白场", "levels_rgb_output_white"},
}};

// 颜色分级：“平衡”只用滑杆读到过范围，没有用数值框核对，不在表里。
constexpr std::array<LabelBinding, 13> kGrading = {{
    {"阴影亮度", "grading_shadows_luminance"},
    {"阴影饱和度", "grading_shadows_saturation"},
    {"阴影色相", "grading_shadows_hue"},
    {"中间调亮度", "grading_midtones_luminance"},
    {"中间调饱和度", "grading_midtones_saturation"},
    {"中间调色相", "grading_midtones_hue"},
    {"高光亮度", "grading_highlights_luminance"},
    {"高光饱和度", "grading_highlights_saturation"},
    {"高光色相", "grading_highlights_hue"},
    {"全局亮度", "grading_global_luminance"},
    {"全局调饱和度", "grading_global_saturation"},
    {"全局调色相", "grading_global_hue"},
    {"混合", "grading_blending"},
}};

constexpr std::array<LabelBinding, 3> kHslChannels = {{{"色相", "hue"}, {"饱和度", "saturation"}, {"明亮度", "luminance"}}};
constexpr std::array<LabelBinding, 8> kHslColors = {{{"红色", "red"},   {"橙色", "orange"}, {"黄色", "yellow"},
                                                     {"绿色", "green"}, {"浅绿色", "aqua"},  {"蓝色", "blue"},
                                                     {"紫色", "purple"}, {"洋红色", "magenta"}}};
constexpr std::array<LabelBinding, 9> kSelectiveTargets = {{{"红色", "red"},     {"黄色", "yellow"},
                                                            {"绿色", "green"},   {"青色", "cyan"},
                                                            {"蓝色", "blue"},    {"洋红色", "magenta"},
                                                            {"白色", "white"},   {"中间色（灰色）", "neutral"},
                                                            {"黑色", "black"}}};
constexpr std::array<LabelBinding, 4> kSelectiveSliders = {{{"青色", "cyan"}, {"洋红", "magenta"}, {"黄色", "yellow"}, {"黑色", "black"}}};

// "<前缀显示名>-<后缀显示名>"，两段都要在表里。
template <std::size_t A, std::size_t B>
std::optional<std::string> pair_parameter(std::string_view label, const std::array<LabelBinding, A>& first,
                                          const std::array<LabelBinding, B>& second, std::string_view prefix) {
  const auto dash = label.find('-');
  if (dash == std::string_view::npos) return std::nullopt;
  const std::string_view left = label.substr(0, dash);
  const std::string_view right = label.substr(dash + 1);
  const auto find_in = [](const auto& table, std::string_view name) -> const LabelBinding* {
    for (const auto& entry : table) {
      if (entry.label == name) return &entry;
    }
    return nullptr;
  };
  const LabelBinding* a = find_in(first, left);
  const LabelBinding* b = find_in(second, right);
  if (a == nullptr || b == nullptr) return std::nullopt;
  return std::string(prefix) + std::string(a->parameter_id) + "_" + std::string(b->parameter_id);
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

// 只给色阶、颜色分级、HSL、可选颜色的子分组滑杆用。曲线等子分组里同名的“阴影”“高光”
// 不在这些表里，不会被绑到顶层影调参数。
std::optional<SliderBinding> refine_subgroup_binding(std::string_view label) {
  for (const auto& entry : kLevels) {
    if (entry.label == label) return SliderBinding{kGlobalColor, std::string(entry.parameter_id)};
  }
  for (const auto& entry : kGrading) {
    if (entry.label == label) return SliderBinding{kGlobalColor, std::string(entry.parameter_id)};
  }
  // HSL：“色相-红色”等，显示名带模式（色相、饱和度、明亮度）和颜色。
  if (const auto hsl = pair_parameter(label, kHslChannels, kHslColors, "hsl_")) {
    return SliderBinding{kGlobalColor, *hsl};
  }
  // 可选颜色：“红色-青色”等，显示名带目标色和滑杆名。
  if (const auto selective = pair_parameter(label, kSelectiveTargets, kSelectiveSliders, "selective_")) {
    return SliderBinding{kGlobalColor, *selective};
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
