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
    EXPECT(style->maximum == 200.0);
    EXPECT(style->default_value == 100.0);
  }
  EXPECT(pixcake::validate_parameter_display_value("color.style", "amount", "200"));
  EXPECT(!pixcake::validate_parameter_display_value("color.style", "amount", "200.5"));
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
    if (mode.capability_id == "mask" || mode.capability_id == "export") continue;
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

int main() {
  const pixcake::CapabilityRegistry registry = pixcake::CapabilityRegistry::phase_one();
  const std::vector<pixcake::CapabilityDescriptor> descriptors = registry.descriptors();

  const std::vector<std::string> expected_ids = {
      "color.match", "color.style", "color.global", "color.local", "color.post",
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

  const pixcake::CapabilityDescriptor* detail = registry.find("detail");
  EXPECT(detail == nullptr);
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
