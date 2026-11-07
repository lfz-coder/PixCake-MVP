#include "pixcake/renderd/semantic_label_adapter.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace {

pixcake::renderd::RgbaInput input() {
  pixcake::renderd::RgbaInput image;
  image.source_format = pixcake::renderd::SourceFormat::Png;
  image.width = 2;
  image.height = 2;
  image.stride = 8;
  image.bytes.assign(16, 255);
  return image;
}

pixcake::renderd::SemanticLabelRule unsupported(pixcake::renderd::SemanticPart part,
                                                const char* reason) {
  pixcake::renderd::SemanticLabelRule rule;
  rule.part = part;
  rule.status = pixcake::renderd::SupportStatus::Unsupported;
  rule.diagnostic = reason;
  return rule;
}

std::vector<pixcake::renderd::SemanticLabelRule> rules_for_fixture() {
  using namespace pixcake::renderd;
  std::vector<SemanticLabelRule> rules;
  for (const auto& descriptor : semantic_part_catalog()) {
    rules.push_back(unsupported(descriptor.part, "fixture has no source label"));
  }
  auto set_rule = [&rules](SemanticPart part, SupportStatus status,
                           std::vector<std::string> labels) {
    auto it = std::find_if(rules.begin(), rules.end(),
                           [part](const auto& rule) { return rule.part == part; });
    assert(it != rules.end());
    it->status = status;
    it->source_labels = std::move(labels);
    it->diagnostic.clear();
  };
  set_rule(SemanticPart::Face, SupportStatus::Supported, {"face"});
  set_rule(SemanticPart::Lips, SupportStatus::Derived, {"upper_lip", "lower_lip"});
  return rules;
}

pixcake::renderd::RawSemanticMask raw(const char* label, std::vector<std::uint8_t> alpha) {
  return {label, 2, 2, 2, std::move(alpha)};
}

}  // namespace

int main() {
  using namespace pixcake::renderd;

  std::string diagnostic;
  SegmentationResult result;
  const auto code = build_semantic_segmentation_result(
      input(), "fixture.model", "fixture-1", rules_for_fixture(),
      {raw("face", {255, 0, 0, 0}), raw("upper_lip", {0, 64, 0, 0}),
       raw("lower_lip", {0, 0, 128, 0})},
      result, diagnostic);
  assert(code == pixcake::ResultCode::Ok);
  assert(diagnostic.empty());
  assert(result.model_id == "fixture.model");
  assert(result.masks.size() == 2);

  const auto lips = std::find_if(result.masks.begin(), result.masks.end(),
                                 [](const auto& mask) { return mask.part == SemanticPart::Lips; });
  assert(lips != result.masks.end());
  assert(lips->alpha == std::vector<std::uint8_t>({0, 64, 128, 0}));

  const auto unsupported_count = std::count_if(
      result.capabilities.begin(), result.capabilities.end(),
      [](const auto& capability) { return capability.status == SupportStatus::Unsupported; });
  assert(unsupported_count == 23);

  auto missing_source_rules = rules_for_fixture();
  auto face = std::find_if(missing_source_rules.begin(), missing_source_rules.end(),
                           [](const auto& rule) { return rule.part == SemanticPart::Face; });
  assert(face != missing_source_rules.end());
  face->source_labels = {"missing"};
  result = {};
  assert(build_semantic_segmentation_result(input(), "fixture.model", "fixture-1",
                                            missing_source_rules,
                                            {raw("face", {255, 0, 0, 0})}, result,
                                            diagnostic) == pixcake::ResultCode::UnsupportedFormat);
  // A failed build must not leave a partial result behind.
  assert(result.masks.empty() && result.capabilities.empty() && result.model_id.empty());

  auto malformed = rules_for_fixture();
  result = {};
  assert(build_semantic_segmentation_result(input(), "fixture.model", "fixture-1", malformed,
                                            {RawSemanticMask{"face", 2, 2, 1, {255}}}, result,
                                            diagnostic) == pixcake::ResultCode::ImageCorrupt);

  // Raw masks with row padding (stride > width) are read row by row; padding
  // bytes must not leak into the packed project mask.
  result = {};
  assert(build_semantic_segmentation_result(
             input(), "fixture.model", "fixture-1", rules_for_fixture(),
             {RawSemanticMask{"face", 2, 2, 4, {10, 20, 99, 99, 30, 40, 99, 99}},
              RawSemanticMask{"upper_lip", 2, 2, 3, {1, 0, 77, 0, 200, 77}},
              RawSemanticMask{"lower_lip", 2, 2, 2, {0, 5, 0, 100}}},
             result, diagnostic) == pixcake::ResultCode::Ok);
  const auto padded_face = std::find_if(result.masks.begin(), result.masks.end(),
                                        [](const auto& mask) { return mask.part == SemanticPart::Face; });
  assert(padded_face != result.masks.end());
  assert(padded_face->stride == 2);
  assert(padded_face->alpha == std::vector<std::uint8_t>({10, 20, 30, 40}));
  const auto padded_lips = std::find_if(result.masks.begin(), result.masks.end(),
                                        [](const auto& mask) { return mask.part == SemanticPart::Lips; });
  assert(padded_lips != result.masks.end());
  assert(padded_lips->alpha == std::vector<std::uint8_t>({1, 5, 0, 200}));

  const auto all_raw = std::vector<RawSemanticMask>{raw("face", {255, 0, 0, 0}),
                                                    raw("upper_lip", {0, 64, 0, 0}),
                                                    raw("lower_lip", {0, 0, 128, 0})};
  auto build_with = [&](const std::vector<SemanticLabelRule>& rules,
                        const std::vector<RawSemanticMask>& masks) {
    SegmentationResult out;
    std::string message;
    const auto result_code =
        build_semantic_segmentation_result(input(), "fixture.model", "fixture-1", rules, masks, out, message);
    if (result_code != pixcake::ResultCode::Ok) {
      assert(!message.empty());
      assert(out.masks.empty() && out.capabilities.empty() && out.model_id.empty());
    }
    return result_code;
  };
  auto find_rule = [](std::vector<SemanticLabelRule>& rules, SemanticPart part) {
    auto it = std::find_if(rules.begin(), rules.end(), [part](const auto& rule) { return rule.part == part; });
    assert(it != rules.end());
    return it;
  };

  // Duplicate raw label from the model.
  auto duplicate_raw = all_raw;
  duplicate_raw.push_back(raw("face", {0, 0, 0, 255}));
  assert(build_with(rules_for_fixture(), duplicate_raw) == pixcake::ResultCode::StateConflict);

  // Raw mask with an empty label.
  auto empty_label_raw = all_raw;
  empty_label_raw.push_back(raw("", {0, 0, 0, 0}));
  assert(build_with(rules_for_fixture(), empty_label_raw) == pixcake::ResultCode::ImageCorrupt);

  // Raw mask whose size does not match the input image.
  auto wrong_size_raw = all_raw;
  wrong_size_raw.push_back(RawSemanticMask{"hair", 1, 2, 1, {0, 0}});
  assert(build_with(rules_for_fixture(), wrong_size_raw) == pixcake::ResultCode::ImageCorrupt);

  // Duplicate and empty source labels inside one rule.
  auto duplicate_source = rules_for_fixture();
  find_rule(duplicate_source, SemanticPart::Lips)->source_labels = {"upper_lip", "upper_lip"};
  assert(build_with(duplicate_source, all_raw) == pixcake::ResultCode::StateConflict);
  auto empty_source = rules_for_fixture();
  find_rule(empty_source, SemanticPart::Lips)->source_labels = {"upper_lip", ""};
  assert(build_with(empty_source, all_raw) == pixcake::ResultCode::StateConflict);

  // Supported rule that also carries an unsupported diagnostic.
  auto supported_with_reason = rules_for_fixture();
  find_rule(supported_with_reason, SemanticPart::Face)->diagnostic = "should be empty";
  assert(build_with(supported_with_reason, all_raw) == pixcake::ResultCode::StateConflict);

  // Unsupported rule that names a source label, or has no reason.
  auto unsupported_with_label = rules_for_fixture();
  find_rule(unsupported_with_label, SemanticPart::Hair)->source_labels = {"face"};
  assert(build_with(unsupported_with_label, all_raw) == pixcake::ResultCode::StateConflict);
  auto unsupported_without_reason = rules_for_fixture();
  find_rule(unsupported_without_reason, SemanticPart::Hair)->diagnostic.clear();
  assert(build_with(unsupported_without_reason, all_raw) == pixcake::ResultCode::StateConflict);

  // Rule table must cover all 25 parts exactly once.
  auto missing_rule = rules_for_fixture();
  missing_rule.pop_back();
  assert(build_with(missing_rule, all_raw) == pixcake::ResultCode::StateConflict);
  auto duplicate_rule = rules_for_fixture();
  duplicate_rule.back() = duplicate_rule.front();
  assert(build_with(duplicate_rule, all_raw) == pixcake::ResultCode::StateConflict);

  // Missing model identity.
  {
    SegmentationResult out;
    std::string message;
    assert(build_semantic_segmentation_result(input(), "", "fixture-1", rules_for_fixture(), all_raw, out,
                                              message) == pixcake::ResultCode::StateConflict);
    assert(build_semantic_segmentation_result(input(), "fixture.model", "", rules_for_fixture(), all_raw, out,
                                              message) == pixcake::ResultCode::StateConflict);
  }

  // Input image whose pixel buffer is shorter than stride * height.
  {
    auto short_input = input();
    short_input.bytes.resize(15);
    SegmentationResult out;
    std::string message;
    assert(build_semantic_segmentation_result(short_input, "fixture.model", "fixture-1", rules_for_fixture(),
                                              all_raw, out, message) == pixcake::ResultCode::StateConflict);
  }

  // Unreferenced raw labels are allowed and never become project parts.
  auto extra_raw = all_raw;
  extra_raw.push_back(raw("eyeglasses", {255, 255, 255, 255}));
  result = {};
  assert(build_semantic_segmentation_result(input(), "fixture.model", "fixture-1", rules_for_fixture(), extra_raw,
                                            result, diagnostic) == pixcake::ResultCode::Ok);
  assert(result.masks.size() == 2);
  return 0;
}
