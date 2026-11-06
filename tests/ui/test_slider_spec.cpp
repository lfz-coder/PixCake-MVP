#include "pixcake/capability/capability_registry.hpp"
#include "pixcake/ui/panels/slider_spec.hpp"

#include <cstdio>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* expression, int line) {
  if (!condition) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, expression);
    ++failures;
  }
}

#define EXPECT(value) expect(static_cast<bool>(value), #value, __LINE__)

using pixcake::ui::panels::refine_color_binding;
using pixcake::ui::panels::refine_crop_angle_binding;
using pixcake::ui::panels::refine_local_binding;
using pixcake::ui::panels::slider_spec;
using pixcake::ui::panels::SliderBinding;

void test_c38_exposure_spec_comes_from_parameter_mode() {
  const auto spec = slider_spec("曝光", refine_color_binding("曝光"));
  EXPECT(spec.bound);
  EXPECT(spec.minimum == -5.0);
  EXPECT(spec.maximum == 5.0);
  EXPECT(spec.step == 0.05);
  EXPECT(spec.decimals == 2);
  EXPECT(spec.tick_count == 200);
  EXPECT(spec.default_tick == 100);
  EXPECT(spec.default_text == "0.00");
  EXPECT(spec.accessible_name == "曝光，范围 -5.00 到 5.00，步进 0.05，默认 0.00");
}

void test_c36_temperature_spec() {
  const auto spec = slider_spec("色温", refine_color_binding("色温"));
  EXPECT(spec.bound);
  EXPECT(spec.tick_count == 200);
  EXPECT(spec.default_tick == 100);
  EXPECT(spec.default_text == "0");
  EXPECT(spec.accessible_name == "色温，范围 -100 到 100，步进 1，默认 0");
}

void test_c39_all_top_level_labels_match_kernel_modes() {
  const std::vector<std::pair<std::string, std::string>> labels = {
      {"色温", "temperature"}, {"色调", "tint"},          {"曝光", "exposure"},  {"对比度", "contrast"},
      {"亮度", "brightness"},  {"高光", "highlights"},    {"阴影", "shadows"},   {"白色", "whites"},
      {"黑色", "blacks"},      {"纹理", "texture"},       {"清晰度", "clarity"}, {"祛雾", "dehaze"},
      {"鲜艳度", "vibrance"},  {"饱和度", "saturation"}};
  for (const auto& [label, parameter] : labels) {
    const auto binding = refine_color_binding(label);
    EXPECT(binding.has_value());
    if (!binding) continue;
    EXPECT(binding->capability_id == "color.global");
    EXPECT(binding->parameter_id == parameter);
    const auto* mode = pixcake::find_parameter_mode("color.global", parameter);
    EXPECT(mode != nullptr);
    if (mode == nullptr) continue;
    const auto spec = slider_spec(label, binding);
    EXPECT(spec.bound);
    EXPECT(spec.minimum == mode->minimum);
    EXPECT(spec.maximum == mode->maximum);
    EXPECT(spec.step == mode->step);
    EXPECT(spec.default_value == mode->default_value);
  }
}

void test_c35_unbound_label_has_no_range() {
  EXPECT(!refine_color_binding("程度").has_value());
  EXPECT(!refine_color_binding("黑场").has_value());
  EXPECT(!refine_color_binding("色相-红色").has_value());
  const auto spec = slider_spec("黑场", std::nullopt);
  EXPECT(!spec.bound);
  EXPECT(spec.tick_count == 0);
  EXPECT(spec.default_tick == 0);
  EXPECT(spec.default_text == "—");
  EXPECT(spec.accessible_name == "黑场，范围待确认");
}

void test_c35_missing_kernel_parameter_is_unbound() {
  const auto spec = slider_spec("X", SliderBinding{"color.global", "no_such"});
  EXPECT(!spec.bound);
  EXPECT(spec.tick_count == 0);
  EXPECT(spec.accessible_name == "X，范围待确认");
}

void test_c35_local_feather_and_crop_angle_come_from_parameter_modes() {
  const auto feather = slider_spec("羽化", refine_local_binding("羽化"));
  EXPECT(feather.bound);
  EXPECT(feather.accessible_name == "羽化，范围 0 到 100，步进待确认，默认 0");
  EXPECT(!refine_local_binding("画笔").has_value());
  const auto angle = slider_spec("角度滑杆", refine_crop_angle_binding());
  EXPECT(angle.bound);
  EXPECT(angle.minimum == -45.0);
  EXPECT(angle.maximum == 45.0);
}

void test_b33_unconfirmed_step_and_default_are_labelled() {
  const auto angle = slider_spec("角度滑杆", refine_crop_angle_binding());
  EXPECT(angle.accessible_name == "角度滑杆，范围 -45 到 45，步进待确认，默认待确认");
  const auto exposure = slider_spec("曝光", refine_color_binding("曝光"));
  EXPECT(exposure.accessible_name == "曝光，范围 -5.00 到 5.00，步进 0.05，默认 0.00");
}

}  // namespace

int main() {
  test_c38_exposure_spec_comes_from_parameter_mode();
  test_c36_temperature_spec();
  test_c39_all_top_level_labels_match_kernel_modes();
  test_c35_unbound_label_has_no_range();
  test_c35_missing_kernel_parameter_is_unbound();
  test_c35_local_feather_and_crop_angle_come_from_parameter_modes();
  test_b33_unconfirmed_step_and_default_are_labelled();
  return failures == 0 ? 0 : 1;
}
