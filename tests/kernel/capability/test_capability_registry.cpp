#include "pixcake/capability/capability_registry.hpp"

#include <cstdio>
#include <string>
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

  std::vector<pixcake::PipelineStep> duplicate = pipeline;
  duplicate.push_back(pixcake::PipelineStep{"duplicate", "color.match", true});
  EXPECT(!pixcake::validate_phase_one_pipeline(duplicate, &error));
  EXPECT(error == "能力在管线中重复: color.match");
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
  test_pipeline();

  if (g_failures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  return 0;
}
