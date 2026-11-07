#include "pixcake/renderd/segmentation_models.hpp"
#include "pixcake/renderd/semantic_label_adapter.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using namespace pixcake::renderd;

const SemanticLabelRule& rule_for(const std::vector<SemanticLabelRule>& rules, SemanticPart part) {
  const auto it = std::find_if(rules.begin(), rules.end(), [part](const auto& r) { return r.part == part; });
  assert(it != rules.end());
  return *it;
}

const SemanticMask* mask_for(const SegmentationResult& result, SemanticPart part) {
  const auto it = std::find_if(result.masks.begin(), result.masks.end(),
                               [part](const auto& m) { return m.part == part; });
  return it == result.masks.end() ? nullptr : &*it;
}

}  // namespace

int main() {
  const auto rules = mediapipe_selfie_multiclass_rules();
  assert(rules.size() == semantic_part_catalog().size());

  // Mapping recorded in docs/superpowers/evidence/2026-11-06-segmentation-label-mapping.md.
  assert(rule_for(rules, SemanticPart::Hair).source_labels == std::vector<std::string>{"hair"});
  assert(rule_for(rules, SemanticPart::BodySkin).source_labels == std::vector<std::string>{"body-skin"});
  // face-skin covers brows, eyes, nose, lips and teeth: it is the whole face, not face skin.
  assert(rule_for(rules, SemanticPart::Face).source_labels == std::vector<std::string>{"face-skin"});
  assert(rule_for(rules, SemanticPart::FaceSkin).status == SupportStatus::Unsupported);
  assert(rule_for(rules, SemanticPart::AllSkin).status == SupportStatus::Unsupported);
  assert(rule_for(rules, SemanticPart::AllClothes).source_labels == std::vector<std::string>{"clothes"});
  assert(rule_for(rules, SemanticPart::AllPerson).status == SupportStatus::Derived);
  assert(rule_for(rules, SemanticPart::UpperClothes).status == SupportStatus::Unsupported);
  assert(rule_for(rules, SemanticPart::Jewelry).status == SupportStatus::Unsupported);
  assert(rule_for(rules, SemanticPart::OtherObjects).status == SupportStatus::Unsupported);
  const auto supported = std::count_if(rules.begin(), rules.end(), [](const auto& r) {
    return r.status != SupportStatus::Unsupported;
  });
  assert(supported == 5);

  // The table goes through the generic adapter with the model's own label names.
  RgbaInput input;
  input.source_format = SourceFormat::Png;
  input.width = 3;
  input.height = 1;
  input.stride = 12;
  input.bytes.assign(12, 200);
  auto raw = [](const char* label, std::vector<std::uint8_t> alpha) {
    return RawSemanticMask{label, 3, 1, 3, std::move(alpha)};
  };
  std::vector<RawSemanticMask> masks;
  for (const auto& label : kMediaPipeSelfieMulticlassLabels) masks.push_back(raw(label, {0, 0, 0}));
  masks[1].alpha = {200, 0, 0};  // hair
  masks[3].alpha = {0, 255, 0};  // face-skin
  masks[5].alpha = {0, 0, 90};   // others
  SegmentationResult result;
  std::string diagnostic;
  assert(build_semantic_segmentation_result(input, kMediaPipeSelfieMulticlassModelId,
                                            kMediaPipeSelfieMulticlassModelVersion, rules, masks, result,
                                            diagnostic) == pixcake::ResultCode::Ok);
  assert(result.masks.size() == 5);
  assert(mask_for(result, SemanticPart::Face)->alpha == std::vector<std::uint8_t>({0, 255, 0}));
  assert(mask_for(result, SemanticPart::AllPerson)->alpha == std::vector<std::uint8_t>({200, 255, 90}));
  assert(mask_for(result, SemanticPart::FaceSkin) == nullptr);

  // Background is never a source label: dropping it does not fail the mapping.
  masks.erase(masks.begin());
  assert(build_semantic_segmentation_result(input, kMediaPipeSelfieMulticlassModelId,
                                            kMediaPipeSelfieMulticlassModelVersion, rules, masks, result,
                                            diagnostic) == pixcake::ResultCode::Ok);

  // Portrait = MediaPipe for the whole person + BiSeNet face parsing inside face crops.
  {
    const auto portrait = portrait_rules();
    assert(portrait.size() == semantic_part_catalog().size());
    assert(kBiSeNetLabels.size() == 19);
    assert(std::string(kBiSeNetLabels[1]) == "face_parsing/skin");
    assert(std::string(kBiSeNetLabels[10]) == "face_parsing/nose");
    // Face skin includes the nose (user decision 2026-11-06).
    assert(rule_for(portrait, SemanticPart::FaceSkin).source_labels ==
           (std::vector<std::string>{"face_parsing/skin", "face_parsing/nose"}));
    assert(rule_for(portrait, SemanticPart::AllSkin).source_labels ==
           (std::vector<std::string>{"body-skin", "face_parsing/skin", "face_parsing/nose"}));
    assert(rule_for(portrait, SemanticPart::Eyebrow).source_labels ==
           (std::vector<std::string>{"face_parsing/left_eyebrow", "face_parsing/right_eyebrow"}));
    assert(rule_for(portrait, SemanticPart::Lips).source_labels ==
           (std::vector<std::string>{"face_parsing/upper_lip", "face_parsing/lower_lip"}));
    assert(rule_for(portrait, SemanticPart::Ear).source_labels ==
           (std::vector<std::string>{"face_parsing/left_ear", "face_parsing/right_ear"}));
    // The MediaPipe parts are unchanged.
    assert(rule_for(portrait, SemanticPart::Face).source_labels == std::vector<std::string>{"face-skin"});
    assert(rule_for(portrait, SemanticPart::Hair).source_labels == std::vector<std::string>{"hair"});
    // Not claimed: crops can cut the neck; eyes/mouth labels are too coarse; hat had false positives.
    for (SemanticPart part : {SemanticPart::Neck, SemanticPart::EyeWhite, SemanticPart::Iris, SemanticPart::Teeth,
                              SemanticPart::OralCavity, SemanticPart::VeilOrHat, SemanticPart::Jewelry}) {
      assert(rule_for(portrait, part).status == SupportStatus::Unsupported);
      assert(!rule_for(portrait, part).diagnostic.empty());
    }
    const auto claimed = std::count_if(portrait.begin(), portrait.end(), [](const auto& r) {
      return r.status != SupportStatus::Unsupported;
    });
    assert(claimed == 10);
    for (const auto& label : portrait_source_labels()) {
      const bool from_mediapipe = std::find_if(kMediaPipeSelfieMulticlassLabels.begin(),
                                               kMediaPipeSelfieMulticlassLabels.end(), [&](const char* l) {
                                                 return label == l;
                                               }) != kMediaPipeSelfieMulticlassLabels.end();
      assert(from_mediapipe || label.rfind("face_parsing/", 0) == 0);
    }
    // 5 MediaPipe labels (hair, body-skin, face-skin, clothes, others) + 8 BiSeNet labels.
    assert(portrait_source_labels().size() == 13);
    assert(bisenet_source_labels().size() == 8);
  }
  return 0;
}
