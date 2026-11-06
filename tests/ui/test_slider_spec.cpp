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
using pixcake::ui::panels::refine_subgroup_binding;
using pixcake::ui::panels::slider_spec;
using pixcake::ui::panels::SliderBinding;

void test_c38_b27_exposure_spec_comes_from_parameter_mode() {
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
  // 颜色分级的“平衡”只用滑杆读到过范围，没有用数值框核对，仍未注册。
  EXPECT(!refine_color_binding("平衡").has_value() && !refine_subgroup_binding("平衡").has_value());
  EXPECT(!refine_subgroup_binding("锐化").has_value());
  EXPECT(!refine_subgroup_binding("红原色-色相").has_value());
  const auto spec = slider_spec("平衡", std::nullopt);
  EXPECT(!spec.bound);
  EXPECT(spec.tick_count == 0);
  EXPECT(spec.default_tick == 0);
  EXPECT(spec.default_text == "—");
  EXPECT(spec.accessible_name == "平衡，范围待确认");
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

// 子分组滑杆（色阶、HSL、可选颜色、颜色分级）：显示名到内核参数的绑定，范围只来自参数模式。
void test_c46_subgroup_labels_bind_to_kernel_modes() {
  struct Case { const char* label; const char* parameter; };
  const Case cases[] = {{"黑场", "levels_rgb_input_black"},     {"灰场", "levels_rgb_input_gray"},
                        {"白场", "levels_rgb_input_white"},     {"输出黑场", "levels_rgb_output_black"},
                        {"输出白场", "levels_rgb_output_white"}, {"色相-红色", "hsl_hue_red"},
                        {"饱和度-浅绿色", "hsl_saturation_aqua"}, {"明亮度-洋红色", "hsl_luminance_magenta"},
                        {"红色-青色", "selective_red_cyan"},     {"中间色（灰色）-黑色", "selective_neutral_black"},
                        {"黑色-洋红", "selective_black_magenta"}, {"阴影亮度", "grading_shadows_luminance"},
                        {"中间调色相", "grading_midtones_hue"},  {"全局调饱和度", "grading_global_saturation"},
                        {"混合", "grading_blending"},           {"色相偏移", "color_editor_hue_shift"},
                        {"饱和度偏移", "color_editor_saturation_shift"},
                        {"明亮度偏移", "color_editor_luminance_shift"}};
  for (const Case& item : cases) {
    const auto binding = refine_subgroup_binding(item.label);
    EXPECT(binding.has_value());
    if (!binding) continue;
    EXPECT(binding->capability_id == "color.global" && binding->parameter_id == item.parameter);
    EXPECT(slider_spec(item.label, binding).bound);
  }
  // 曲线等子分组里同名的“阴影”“高光”不能绑到顶层影调参数，顶层表也不认子分组的显示名。
  EXPECT(!refine_subgroup_binding("阴影").has_value());
  EXPECT(!refine_subgroup_binding("高光").has_value());
  EXPECT(!refine_subgroup_binding("饱和度").has_value());
  EXPECT(refine_color_binding("饱和度")->parameter_id == "saturation");
  EXPECT(!refine_color_binding("黑场").has_value());
  EXPECT(!refine_color_binding("色相-红色").has_value());
  const auto gray = slider_spec("灰场", refine_subgroup_binding("灰场"));
  EXPECT(gray.accessible_name == "灰场，范围 -1.00 到 1.00，步进待确认，默认 0.00");
  EXPECT(slider_spec("黑场", refine_subgroup_binding("黑场")).accessible_name == "黑场，范围 0 到 255，步进 1，默认 0");
  EXPECT(slider_spec("色相-红色", refine_subgroup_binding("色相-红色")).accessible_name ==
         "色相-红色，范围 -100 到 100，步进 1，默认 0");
  EXPECT(slider_spec("中间调色相", refine_subgroup_binding("中间调色相")).accessible_name ==
         "中间调色相，范围 0 到 359，步进待确认，默认 0");
}

// Q-11：色彩面板的行、显示名与绑定来自同一张表；后期调色复用其分组标题。
void test_q11_color_panel_rows_come_from_one_table() {
  const auto rows = pixcake::ui::panels::refine_color_rows();
  int bound = 0;
  int pending = 0;
  for (const auto& row : rows) {
    if (row.kind != pixcake::ui::panels::PanelRowKind::Slider) continue;
    const auto spec = slider_spec(row.label, row.binding);
    if (spec.bound) {
      ++bound;
    } else {
      ++pending;
      EXPECT(pixcake::ui::panels::slider_range_pending(spec.accessible_name));
    }
    EXPECT(pixcake::ui::panels::slider_label_of(spec.accessible_name) == row.label);
  }
  // 顶层影调 14 + 色阶 5 + HSL 24 + 可选颜色 36 + 颜色分级 13 + 色彩编辑器偏移 3。
  EXPECT(bound == 95);
  EXPECT(pending > 0);
  const auto titles = pixcake::ui::panels::refine_post_color_titles();
  EXPECT(titles.size() == 17 && titles.front() == "全图调整" && titles.back() == "创意色彩");
  EXPECT(std::string(pixcake::ui::panels::refine_color_panel_name()) == "色彩调节");
}

int main() {
  test_q11_color_panel_rows_come_from_one_table();
  test_c46_subgroup_labels_bind_to_kernel_modes();
  test_c38_b27_exposure_spec_comes_from_parameter_mode();
  test_c36_temperature_spec();
  test_c39_all_top_level_labels_match_kernel_modes();
  test_c35_unbound_label_has_no_range();
  test_c35_missing_kernel_parameter_is_unbound();
  test_c35_local_feather_and_crop_angle_come_from_parameter_modes();
  test_b33_unconfirmed_step_and_default_are_labelled();
  return failures == 0 ? 0 : 1;
}
