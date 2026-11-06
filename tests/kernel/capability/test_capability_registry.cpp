#include "pixcake/capability/capability_registry.hpp"

#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

int g_failures = 0;

void expect(bool condition, const char* expression, int line) {
  if (!condition) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, expression);
    ++g_failures;
  }
}

#define EXPECT(condition) expect(static_cast<bool>(condition), #condition, __LINE__)

void test_parameter_modes() {
  const auto& modes = pixcake::phase_one_parameter_modes();
  EXPECT(!modes.empty());
  const auto* exposure = pixcake::find_parameter_mode("color.global", "exposure");
  EXPECT(exposure != nullptr);
  if (exposure != nullptr) {
    EXPECT(exposure->minimum == -5.0);
    EXPECT(exposure->maximum == 5.0);
    EXPECT(exposure->step == 0.05);
    EXPECT(exposure->default_value == 0.0);
  }
  const auto* style = pixcake::find_parameter_mode("color.style", "amount");
  EXPECT(style != nullptr);
  if (style != nullptr) {
    EXPECT(style->minimum == 0.0);
    EXPECT(style->maximum == 100.0);
    EXPECT(style->default_value == 100.0);
  }
  EXPECT(pixcake::validate_parameter_display_value("color.style", "amount", "100"));
  EXPECT(!pixcake::validate_parameter_display_value("color.style", "amount", "100.5"));
  EXPECT(pixcake::validate_parameter_display_value("mask", "feather", "100"));
  EXPECT(!pixcake::validate_parameter_display_value("mask", "feather", "100.5"));
  EXPECT(pixcake::validate_parameter_display_value("crop", "angle", "-45"));
  EXPECT(!pixcake::validate_parameter_display_value("crop", "angle", "45.5"));
  EXPECT(pixcake::validate_parameter_display_value("portrait.feature", "eye_glint_rotation", "-100"));
  EXPECT(!pixcake::validate_parameter_display_value("portrait.feature", "eye_glint_rotation", "101"));
}

void test_c05_profile_is_an_enum_parameter() {
  const auto* profile = pixcake::find_parameter_mode("color.global", "profile");
  EXPECT(profile != nullptr);
  if (profile != nullptr) {
    EXPECT(profile->choices == std::vector<std::string>({"standard", "texture", "monochrome", "basic"}));
    EXPECT(profile->default_choice == "standard");
  }
  EXPECT(pixcake::validate_parameter_display_value("color.global", "profile", "monochrome"));
  EXPECT(!pixcake::validate_parameter_display_value("color.global", "profile", "黑白"));
  EXPECT(!pixcake::validate_parameter_display_value("color.global", "profile", "1"));
}

void test_c31_parameter_modes_use_registered_capabilities() {
  const auto registry = pixcake::CapabilityRegistry::phase_one();
  for (const auto& mode : pixcake::phase_one_parameter_modes()) {
    if (mode.capability_id == "mask" || mode.capability_id == "export" || mode.capability_id == "layer") continue;
    if (registry.find(mode.capability_id) == nullptr) {
      std::fprintf(stderr, "unregistered capability in parameter mode: %s/%s\n", mode.capability_id.c_str(),
                   mode.parameter_id.c_str());
      EXPECT(registry.find(mode.capability_id) != nullptr);
    }
  }
  EXPECT(pixcake::find_parameter_mode("color.global", "color_temperature") == nullptr);
  EXPECT(pixcake::find_parameter_mode("color.global", "color_tint") == nullptr);
  EXPECT(pixcake::find_parameter_mode("pet", "eye_glint_vertical") != nullptr);
  EXPECT(pixcake::find_parameter_mode("image.background", "light_uniform_brightness") != nullptr);
}

void test_c05_monochrome_disables_vibrance_and_saturation() {
  std::string profile = "monochrome";
  const auto lookup = [&profile](std::string_view capability, std::string_view parameter) -> std::optional<std::string> {
    if (capability == "color.global" && parameter == "profile") return profile;
    return std::nullopt;
  };
  EXPECT(pixcake::parameter_disabled_reason("color.global", "vibrance", lookup) ==
         std::optional<std::string>("黑白配置文件不调节鲜艳度和饱和度"));
  EXPECT(pixcake::parameter_disabled_reason("color.global", "saturation", lookup).has_value());
  EXPECT(!pixcake::parameter_disabled_reason("color.global", "contrast", lookup).has_value());
  profile = "standard";
  EXPECT(!pixcake::parameter_disabled_reason("color.global", "vibrance", lookup).has_value());
  const auto none = [](std::string_view, std::string_view) -> std::optional<std::string> { return std::nullopt; };
  EXPECT(!pixcake::parameter_disabled_reason("color.global", "vibrance", none).has_value());
}

void test_b33_unconfirmed_step_checks_range_only() {
  const auto* feather = pixcake::find_parameter_mode("mask", "feather");
  EXPECT(feather != nullptr && !feather->step_confirmed && feather->default_confirmed);
  const auto* angle = pixcake::find_parameter_mode("crop", "angle");
  EXPECT(angle != nullptr && !angle->step_confirmed && !angle->default_confirmed);
  const auto* exposure = pixcake::find_parameter_mode("color.global", "exposure");
  EXPECT(exposure != nullptr && exposure->step_confirmed && exposure->default_confirmed);
  EXPECT(pixcake::validate_parameter_display_value("mask", "feather", "12.5"));
  EXPECT(!pixcake::validate_parameter_display_value("mask", "feather", "100.5"));
  EXPECT(!pixcake::validate_parameter_display_value("color.global", "exposure", "0.03"));
}

void test_pipeline() {
  const std::vector<pixcake::PipelineStep> pipeline = pixcake::phase_one_pipeline();
  const std::vector<std::string> expected = {
      "detail.geometry", "detail.denoise", "color.match", "color.style", "color.global",
      "color.local", "portrait.blemish", "portrait.skin", "portrait.reshape", "portrait.feature",
      "portrait.body", "image.background", "image.idphoto", "image.sky", "clothes", "pet",
      "color.post", "manual.liquify", "manual.heal", "manual.edge", "crop"};
  EXPECT(pipeline.size() == expected.size());
  for (std::size_t index = 0; index < expected.size() && index < pipeline.size(); ++index) {
    EXPECT(pipeline[index].step_id == expected[index]);
  }

  EXPECT(pipeline[0].capability_id == "detail");
  EXPECT(!pipeline[0].registered_for_drawing);
  EXPECT(pipeline[1].capability_id == "detail");
  EXPECT(!pipeline[1].registered_for_drawing);
  EXPECT(pipeline[2].capability_id == "color.match");
  EXPECT(pipeline.back().capability_id == "crop");

  std::string error;
  EXPECT(pixcake::validate_phase_one_pipeline(pipeline, &error));
  EXPECT(error.empty());
  const auto order = pixcake::phase_one_pipeline_order(&error);
  EXPECT(error.empty());
  EXPECT(order.size() == pipeline.size());
  if (order.size() == pipeline.size()) {
    EXPECT(order[2] == "color.match");
    EXPECT(order[3] == "color.style");
    EXPECT(order[4] == "color.global");
  }

  std::vector<pixcake::PipelineStep> duplicate = pipeline;
  duplicate.push_back(pixcake::PipelineStep{"duplicate", "color.match", true});
  EXPECT(!pixcake::validate_phase_one_pipeline(duplicate, &error));
  EXPECT(error == "能力在管线中重复: color.match");

  std::vector<pixcake::PipelineStep> cycle = pipeline;
  cycle[0].dependencies = {"crop"};
  EXPECT(!pixcake::validate_phase_one_pipeline(cycle, &error));
  EXPECT(error == "管线存在依赖环路");
}

}  // namespace

// 来源：需求 §10.3 第 433 行（输入色阶黑灰白三个、输出色阶黑白两个）和架构附录 B「色阶」读数；
// 2026-11-05 在 test 版上实测：下排黑场、白场为 0–255 整数，灰场 -1.00 到 1.00；上排两框为 0–255 整数。
void test_c42_levels_rgb_modes() {
  struct Row { const char* id; double minimum; double maximum; double default_value; bool step_confirmed; };
  const Row rows[] = {{"levels_rgb_input_black", 0, 255, 0, true},  {"levels_rgb_input_gray", -1, 1, 0, false},
                      {"levels_rgb_input_white", 0, 255, 255, true}, {"levels_rgb_output_black", 0, 255, 0, true},
                      {"levels_rgb_output_white", 0, 255, 255, true}};
  for (const Row& row : rows) {
    const auto* mode = pixcake::find_parameter_mode("color.global", row.id);
    EXPECT(mode != nullptr);
    if (mode == nullptr) continue;
    EXPECT(mode->minimum == row.minimum && mode->maximum == row.maximum && mode->default_value == row.default_value);
    EXPECT(mode->step_confirmed == row.step_confirmed && mode->default_confirmed);
  }
  EXPECT(pixcake::validate_parameter_display_value("color.global", "levels_rgb_input_white", "255"));
  EXPECT(!pixcake::validate_parameter_display_value("color.global", "levels_rgb_input_white", "256"));
  EXPECT(!pixcake::validate_parameter_display_value("color.global", "levels_rgb_input_black", "-1"));
  EXPECT(!pixcake::validate_parameter_display_value("color.global", "levels_rgb_input_black", "1.5"));
  EXPECT(pixcake::validate_parameter_display_value("color.global", "levels_rgb_input_gray", "0.5"));
  EXPECT(!pixcake::validate_parameter_display_value("color.global", "levels_rgb_input_gray", "1.01"));
  // 红、绿、蓝通道没有实测，不注册。
  EXPECT(pixcake::find_parameter_mode("color.global", "levels_red_input_black") == nullptr);
}

// 来源：2026-11-05 用户给出的范围（色相、饱和度、明亮度偏移 -100 到 100，步长 1，默认 0）；
// 色相行 8 个颜色的范围和整数步进由另一会话在 test 版上读回确认。
void test_c43_hsl_modes_and_monochrome_switch() {
  const char* colors[] = {"red", "orange", "yellow", "green", "aqua", "blue", "purple", "magenta"};
  const char* channels[] = {"hue", "saturation", "luminance"};
  std::size_t found = 0;
  const auto monochrome = [](std::string_view capability, std::string_view parameter) -> std::optional<std::string> {
    if (capability == "color.global" && parameter == "profile") return std::string("monochrome");
    return std::nullopt;
  };
  const auto standard = [](std::string_view, std::string_view) -> std::optional<std::string> { return std::nullopt; };
  for (const char* channel : channels) {
    for (const char* color : colors) {
      const std::string id = std::string("hsl_") + channel + "_" + color;
      const auto* mode = pixcake::find_parameter_mode("color.global", id);
      EXPECT(mode != nullptr);
      if (mode == nullptr) continue;
      ++found;
      EXPECT(mode->minimum == -100.0 && mode->maximum == 100.0 && mode->step == 1.0 && mode->default_value == 0.0);
      EXPECT(mode->step_confirmed && mode->default_confirmed);
      EXPECT(pixcake::parameter_disabled_reason("color.global", id, monochrome).has_value());
      EXPECT(!pixcake::parameter_disabled_reason("color.global", id, standard).has_value());
    }
  }
  EXPECT(found == 24);
  EXPECT(pixcake::validate_parameter_display_value("color.global", "hsl_hue_red", "-100"));
  EXPECT(!pixcake::validate_parameter_display_value("color.global", "hsl_hue_red", "101"));
  EXPECT(!pixcake::validate_parameter_display_value("color.global", "hsl_hue_red", "0.5"));
}

// 来源：2026-11-05 用户给出的范围（这些颜色滑杆 -100 到 100，步长 1）；目标色和滑杆名取自 test 版画面。
void test_c44_selective_color_modes() {
  const char* targets[] = {"red", "yellow", "green", "cyan", "blue", "magenta", "white", "neutral", "black"};
  const char* sliders[] = {"cyan", "magenta", "yellow", "black"};
  std::size_t found = 0;
  for (const char* target : targets) {
    for (const char* slider : sliders) {
      const auto* mode = pixcake::find_parameter_mode("color.global", std::string("selective_") + target + "_" + slider);
      EXPECT(mode != nullptr);
      if (mode == nullptr) continue;
      ++found;
      EXPECT(mode->minimum == -100.0 && mode->maximum == 100.0 && mode->step == 1.0 && mode->default_value == 0.0);
      EXPECT(mode->step_confirmed && mode->default_confirmed);
    }
  }
  EXPECT(found == 36);
  const auto* method = pixcake::find_parameter_mode("color.global", "selective_mode");
  EXPECT(method != nullptr);
  if (method != nullptr) {
    EXPECT(method->choices == std::vector<std::string>({"relative", "absolute"}));
    EXPECT(method->default_choice == "relative");
  }
  EXPECT(pixcake::validate_parameter_display_value("color.global", "selective_mode", "absolute"));
  EXPECT(!pixcake::validate_parameter_display_value("color.global", "selective_mode", "绝对"));
  EXPECT(pixcake::find_parameter_mode("color.global", "selective_gray_cyan") == nullptr);
}

// 来源：2026-11-05 test 版实测（数值框键盘输入，越界被拒）：四个模式（阴影、中间调、高光、全局）下
// 明度 -100–100、饱和度 0–100、色相 0–359、默认 0；混合 0–100、默认 50。整数以外的输入没有测，步进待确认。
void test_c45_color_grading_modes() {
  const char* zones[] = {"shadows", "midtones", "highlights", "global"};
  for (const char* zone : zones) {
    const auto* luminance = pixcake::find_parameter_mode("color.global", std::string("grading_") + zone + "_luminance");
    const auto* saturation = pixcake::find_parameter_mode("color.global", std::string("grading_") + zone + "_saturation");
    const auto* hue = pixcake::find_parameter_mode("color.global", std::string("grading_") + zone + "_hue");
    EXPECT(luminance != nullptr && saturation != nullptr && hue != nullptr);
    if (luminance == nullptr || saturation == nullptr || hue == nullptr) continue;
    EXPECT(luminance->minimum == -100.0 && luminance->maximum == 100.0 && luminance->default_value == 0.0);
    EXPECT(saturation->minimum == 0.0 && saturation->maximum == 100.0 && saturation->default_value == 0.0);
    EXPECT(hue->minimum == 0.0 && hue->maximum == 359.0 && hue->default_value == 0.0);
    EXPECT(!luminance->step_confirmed && !saturation->step_confirmed && !hue->step_confirmed);
  }
  const auto* blending = pixcake::find_parameter_mode("color.global", "grading_blending");
  EXPECT(blending != nullptr && blending->minimum == 0.0 && blending->maximum == 100.0 && blending->default_value == 50.0);
  EXPECT(pixcake::validate_parameter_display_value("color.global", "grading_global_hue", "359"));
  EXPECT(!pixcake::validate_parameter_display_value("color.global", "grading_global_hue", "360"));
  EXPECT(!pixcake::validate_parameter_display_value("color.global", "grading_global_saturation", "-1"));
  // 平衡只用滑杆读到过 -100 到 100，没有用数值框核对，不注册。
  EXPECT(pixcake::find_parameter_mode("color.global", "grading_balance") == nullptr);
}

// 契约 §4 第 74、413 行：颗粒属于 `detail`（降噪、锐化、噪点、杂色、颗粒、镜头、透视、校准）。
// 自定义工具栏里“颗粒”与“白平衡”同级，是参数引用（需求 §8.2、架构第 633 行），不是独立能力。
// 范围 0..100 步长 1 由用户 2026-11-05 给出，默认值取自 test 版画面。
void test_c31_q02_grain_belongs_to_detail() {
  const auto registry = pixcake::CapabilityRegistry::phase_one();
  const auto* detail = registry.find("detail");
  EXPECT(detail != nullptr);
  if (detail != nullptr) EXPECT(detail->module_label == "色彩" && detail->label == "细节");
  EXPECT(registry.find("grain") == nullptr);
  struct Row { const char* id; double default_value; };
  const Row rows[] = {{"grain_amount", 0}, {"grain_shadows", 30}, {"grain_midtones", 50}, {"grain_highlights", 15},
                      {"grain_size", 10},  {"grain_roughness", 15}, {"grain_color", 10}};
  for (const Row& row : rows) {
    const auto* mode = pixcake::find_parameter_mode("detail", row.id);
    EXPECT(mode != nullptr);
    if (mode == nullptr) continue;
    EXPECT(mode->minimum == 0.0 && mode->maximum == 100.0 && mode->step == 1.0);
    EXPECT(mode->default_value == row.default_value && mode->step_confirmed && mode->default_confirmed);
  }
  EXPECT(pixcake::find_parameter_mode("grain", "amount") == nullptr);
  EXPECT(!pixcake::validate_parameter_display_value("detail", "grain_amount", "101"));
  std::string error;
  EXPECT(pixcake::validate_phase_one_pipeline(pixcake::phase_one_pipeline(), &error));
}

// AI 色彩风格程度按官方 0..100（2026-11-05 用户裁定，test 版实测上限 100）；
// AI 追色程度按需求原文 0..200（2026-11-05 用户裁定恢复，追色没有实测）。默认值都按需求 100。
void test_b33_c48_amount_ranges() {
  const auto* style = pixcake::find_parameter_mode("color.style", "amount");
  EXPECT(style != nullptr && style->minimum == 0.0 && style->maximum == 100.0 && style->default_value == 100.0);
  EXPECT(!pixcake::validate_parameter_display_value("color.style", "amount", "101"));
  const auto* match = pixcake::find_parameter_mode("color.match", "amount");
  EXPECT(match != nullptr && match->minimum == 0.0 && match->maximum == 200.0 && match->default_value == 100.0);
  EXPECT(pixcake::validate_parameter_display_value("color.match", "amount", "200"));
  EXPECT(!pixcake::validate_parameter_display_value("color.match", "amount", "201"));
}

// 来源：官方《基础调色功能说明》（wiki.pixcakeai.com/guide/9398.html）：色彩编辑器色相、饱和度、明亮度偏移
// 均为 -100 到 100，默认 0；手册没写步进，标待确认。参数标识由本项目选定（契约只冻结能力标识）。
void test_c49_color_editor_offsets() {
  for (const char* id : {"color_editor_hue_shift", "color_editor_saturation_shift", "color_editor_luminance_shift"}) {
    const auto* mode = pixcake::find_parameter_mode("color.global", id);
    EXPECT(mode != nullptr);
    if (mode == nullptr) continue;
    EXPECT(mode->minimum == -100.0 && mode->maximum == 100.0 && mode->default_value == 0.0);
    EXPECT(!mode->step_confirmed && mode->default_confirmed);
    EXPECT(pixcake::validate_parameter_display_value("color.global", id, "-100"));
    EXPECT(!pixcake::validate_parameter_display_value("color.global", id, "100.5"));
  }
  // 色彩平滑、亮度平滑、色彩均匀度没有官方数字也没有实测，不注册。
  EXPECT(pixcake::find_parameter_mode("color.global", "color_editor_color_smoothing") == nullptr);
}

// C-05：黑白档把 HSL 换成黑白组（需求第 419 行）。黑白混合 8 色 -100..100、整数、默认 0，
// 色彩编辑器在黑白档下从面板消失（2026-11-06 test 版实测，证据 2026-11-06-monochrome-mixer-measurement.md）。
void test_c05_monochrome_switches_hsl_to_mixer() {
  std::string profile = "monochrome";
  const auto lookup = [&profile](std::string_view capability, std::string_view parameter) -> std::optional<std::string> {
    if (capability == "color.global" && parameter == "profile") return profile;
    return std::nullopt;
  };
  const char* colors[] = {"red", "orange", "yellow", "green", "aqua", "blue", "purple", "magenta"};
  for (const char* color : colors) {
    const std::string id = std::string("bw_mix_") + color;
    const auto* mode = pixcake::find_parameter_mode("color.global", id);
    EXPECT(mode != nullptr);
    if (mode == nullptr) continue;
    EXPECT(mode->minimum == -100.0 && mode->maximum == 100.0 && mode->step == 1.0 && mode->default_value == 0.0);
    EXPECT(mode->step_confirmed && mode->default_confirmed);
    for (const char* other : {"standard", "texture", "basic"}) {
      profile = other;
      EXPECT(pixcake::parameter_disabled_reason("color.global", id, lookup).has_value());
    }
    profile = "monochrome";
    EXPECT(!pixcake::parameter_disabled_reason("color.global", id, lookup).has_value());
  }
  // 没有已提交的配置文件时按默认“标准”判断：黑白混合不可编辑。
  const auto none = [](std::string_view, std::string_view) -> std::optional<std::string> { return std::nullopt; };
  EXPECT(pixcake::parameter_disabled_reason("color.global", "bw_mix_red", none).has_value());
  EXPECT(!pixcake::validate_parameter_display_value("color.global", "bw_mix_red", "101"));
  EXPECT(!pixcake::validate_parameter_display_value("color.global", "bw_mix_red", "0.5"));
  for (const char* id : {"color_editor_hue_shift", "color_editor_saturation_shift", "color_editor_luminance_shift"}) {
    profile = "monochrome";
    EXPECT(pixcake::parameter_disabled_reason("color.global", id, lookup).has_value());
    profile = "standard";
    EXPECT(!pixcake::parameter_disabled_reason("color.global", id, lookup).has_value());
  }
}

// Q-06：图层不透明度、蒙版羽化、祛路人选区羽化的范围只来自参数模式（契约 C-28、边界 B-33）。
// `layer` 与 `mask` 是参数模式归属方，不是契约 §4 能力，所以不在描述表里。
void test_q06_layer_ranges_come_from_parameter_modes() {
  const auto* opacity = pixcake::find_parameter_mode("layer", "opacity");
  EXPECT(opacity != nullptr && opacity->minimum == 0.0 && opacity->maximum == 100.0 && opacity->step == 1.0);
  const auto* selection = pixcake::find_parameter_mode("mask", "selection_feather");
  EXPECT(selection != nullptr && selection->minimum == 0.0 && selection->maximum == 1000.0 && selection->step == 1.0);
  const auto* feather = pixcake::find_parameter_mode("mask", "feather");
  EXPECT(feather != nullptr && feather->minimum == 0.0 && feather->maximum == 100.0);
  EXPECT(pixcake::validate_parameter_display_value("layer", "opacity", "100"));
  EXPECT(!pixcake::validate_parameter_display_value("layer", "opacity", "101"));
  EXPECT(pixcake::validate_parameter_display_value("mask", "selection_feather", "1000"));
  EXPECT(!pixcake::validate_parameter_display_value("mask", "selection_feather", "1001"));
  EXPECT(pixcake::CapabilityRegistry::phase_one().find("layer") == nullptr);
}

// 创意图层蒙版的密度与羽化（2026-11-06 test 版实测，架构附录 B“创意图层蒙版”）。
void test_q06_layer_mask_modes_match_measurement() {
  const auto* density = pixcake::find_parameter_mode("mask", "density");
  EXPECT(density != nullptr && density->minimum == 0.0 && density->maximum == 100.0 && density->step == 1.0 &&
         density->default_value == 100.0 && density->step_confirmed && density->default_confirmed);
  const auto* feather = pixcake::find_parameter_mode("mask", "layer_feather");
  EXPECT(feather != nullptr && feather->minimum == 0.0 && feather->maximum == 1000.0 && feather->step == 1.0 &&
         feather->default_value == 0.0 && feather->step_confirmed && feather->default_confirmed);
  EXPECT(!pixcake::validate_parameter_display_value("mask", "density", "101"));
  EXPECT(!pixcake::validate_parameter_display_value("mask", "density", "55.5"));
  EXPECT(pixcake::validate_parameter_display_value("mask", "layer_feather", "1000"));
  EXPECT(!pixcake::validate_parameter_display_value("mask", "layer_feather", "1001"));
}

// 2026-11-05 用户给出：只有颗粒效果不为 0，下面各项才可以调节。没有值时按默认值 0 判断。
void test_q02_grain_sub_parameters_need_nonzero_amount() {
  std::optional<std::string> amount;
  const auto lookup = [&amount](std::string_view capability, std::string_view parameter) -> std::optional<std::string> {
    if (capability == "detail" && parameter == "grain_amount") return amount;
    return std::nullopt;
  };
  for (const char* id : {"grain_shadows", "grain_midtones", "grain_highlights", "grain_size", "grain_roughness",
                         "grain_color"}) {
    amount.reset();
    EXPECT(pixcake::parameter_disabled_reason("detail", id, lookup).has_value());
    amount = "0";
    EXPECT(pixcake::parameter_disabled_reason("detail", id, lookup).has_value());
    amount = "1";
    EXPECT(!pixcake::parameter_disabled_reason("detail", id, lookup).has_value());
  }
  amount.reset();
  EXPECT(!pixcake::parameter_disabled_reason("detail", "grain_amount", lookup).has_value());
}

int main() {
  test_q02_grain_sub_parameters_need_nonzero_amount();
  test_q06_layer_ranges_come_from_parameter_modes();
  test_q06_layer_mask_modes_match_measurement();
  test_c05_monochrome_switches_hsl_to_mixer();
  test_c49_color_editor_offsets();
  test_c31_q02_grain_belongs_to_detail();
  test_b33_c48_amount_ranges();
  test_c42_levels_rgb_modes();
  test_c43_hsl_modes_and_monochrome_switch();
  test_c44_selective_color_modes();
  test_c45_color_grading_modes();
  const pixcake::CapabilityRegistry registry = pixcake::CapabilityRegistry::phase_one();
  const std::vector<pixcake::CapabilityDescriptor> descriptors = registry.descriptors();

  const std::vector<std::string> expected_ids = {
      "color.match", "color.style", "color.global", "color.local", "color.post", "detail",
      "portrait.blemish", "portrait.skin", "portrait.reshape", "portrait.feature", "portrait.body",
      "image.background", "image.idphoto", "image.sky", "clothes", "pet", "crop",
      "manual.edge", "manual.liquify", "manual.heal"};
  EXPECT(descriptors.size() == expected_ids.size());
  for (std::size_t index = 0; index < expected_ids.size() && index < descriptors.size(); ++index) {
    EXPECT(descriptors[index].id == expected_ids[index]);
    EXPECT(descriptors[index].registered_for_drawing);
  }

  const pixcake::CapabilityDescriptor* feature = registry.find("portrait.feature");
  EXPECT(feature != nullptr);
  if (feature != nullptr) {
    EXPECT(feature->module_label == "人像");
    EXPECT(feature->label == "表情、牙齿、眼睛、妆容、头发");
    EXPECT(feature->can_use_in_preset);
    EXPECT(feature->can_batch_sync);
    EXPECT(feature->batch_sync_blocked_reason.empty());
  }

  const pixcake::CapabilityDescriptor* match = registry.find("color.match");
  EXPECT(match != nullptr);
  if (match != nullptr) {
    EXPECT(match->module_label == "AI 色彩");
    EXPECT(match->label == "AI 追色");
    EXPECT(match->registered_for_drawing);
    EXPECT(match->can_use_in_preset);
    EXPECT(!match->can_batch_sync);
    EXPECT(match->batch_sync_blocked_reason ==
           "同步时按每张图重新计算，不是把数值原样抄过去");
  }

  // 契约 §4 第 74 行：`detail` 是本阶段注册的能力（第四轮复审 C-31/Q-02）。
  const pixcake::CapabilityDescriptor* detail = registry.find("detail");
  EXPECT(detail != nullptr);
  EXPECT(registry.find("color.match.local") == nullptr);
  EXPECT(registry.find("portrait.eyes") == nullptr);
  EXPECT(pixcake::validate_parameter_display_value("color.global", "exposure", "1.50"));
  EXPECT(!pixcake::validate_parameter_display_value("color.global", "exposure", "1.001"));
  EXPECT(!pixcake::validate_parameter_display_value("color.global", "exposure", "1.03"));
  EXPECT(pixcake::validate_parameter_display_value("color.global", "contrast", "1"));
  EXPECT(!pixcake::validate_parameter_display_value("color.global", "contrast", "1.5"));
  EXPECT(pixcake::validate_parameter_display_value("color.global", "temperature", "-100"));
  EXPECT(!pixcake::validate_parameter_display_value("color.global", "temperature", "100.5"));
  EXPECT(!pixcake::validate_parameter_display_value("color.global", "future", "1"));
  const auto style_order = pixcake::phase_one_capability_order("color.style");
  const auto global_order = pixcake::phase_one_capability_order("color.global");
  EXPECT(style_order.has_value());
  EXPECT(global_order.has_value());
  if (style_order && global_order) EXPECT(*style_order < *global_order);
  test_parameter_modes();
  test_c05_profile_is_an_enum_parameter();
  test_c31_parameter_modes_use_registered_capabilities();
  test_b33_unconfirmed_step_checks_range_only();
  test_c05_monochrome_disables_vibrance_and_saturation();
  test_pipeline();

  if (g_failures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  return 0;
}
