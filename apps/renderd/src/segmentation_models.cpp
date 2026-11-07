#include "pixcake/renderd/segmentation_models.hpp"

#include <algorithm>

namespace pixcake::renderd {

namespace {

SemanticLabelRule& rule_of(std::vector<SemanticLabelRule>& rules, SemanticPart part) {
  return *std::find_if(rules.begin(), rules.end(), [part](const auto& r) { return r.part == part; });
}

void claim(std::vector<SemanticLabelRule>& rules, SemanticPart part, SupportStatus status,
           std::vector<std::string> labels) {
  auto& rule = rule_of(rules, part);
  rule.status = status;
  rule.source_labels = std::move(labels);
  rule.diagnostic.clear();
}

}  // namespace

std::vector<SemanticLabelRule> mediapipe_selfie_multiclass_rules() {
  std::vector<SemanticLabelRule> rules;
  for (const auto& descriptor : semantic_part_catalog()) {
    SemanticLabelRule rule;
    rule.part = descriptor.part;
    rule.status = SupportStatus::Unsupported;
    rule.diagnostic = "MediaPipe SelfieMulticlass has no label for this part";
    rules.push_back(std::move(rule));
  }
  auto set = [&rules](SemanticPart part, SupportStatus status, std::vector<std::string> labels) {
    auto it = std::find_if(rules.begin(), rules.end(), [part](const auto& r) { return r.part == part; });
    it->status = status;
    it->source_labels = std::move(labels);
    it->diagnostic.clear();
  };
  auto reason = [&rules](SemanticPart part, const char* text) {
    auto it = std::find_if(rules.begin(), rules.end(), [part](const auto& r) { return r.part == part; });
    it->diagnostic = text;
  };

  set(SemanticPart::Hair, SupportStatus::Supported, {"hair"});
  set(SemanticPart::BodySkin, SupportStatus::Supported, {"body-skin"});
  // Sample check: face-skin covers brows, eyes, nose, lips and teeth.
  set(SemanticPart::Face, SupportStatus::Supported, {"face-skin"});
  set(SemanticPart::AllClothes, SupportStatus::Supported, {"clothes"});
  set(SemanticPart::AllPerson, SupportStatus::Derived, {"hair", "body-skin", "face-skin", "clothes", "others"});
  reason(SemanticPart::FaceSkin, "MediaPipe face-skin includes brows, eyes and lips; face skin needs face parsing");
  reason(SemanticPart::AllSkin, "body-skin + face-skin would include brows, eyes and lips");
  return rules;
}

std::vector<SemanticLabelRule> portrait_rules() {
  auto rules = mediapipe_selfie_multiclass_rules();
  for (auto& rule : rules) {
    if (rule.status == SupportStatus::Unsupported) rule.diagnostic = "the portrait models have no label for this part";
  }
  // Face skin includes the nose (user decision 2026-11-06); BiSeNet skin does not.
  claim(rules, SemanticPart::FaceSkin, SupportStatus::Derived, {"face_parsing/skin", "face_parsing/nose"});
  claim(rules, SemanticPart::AllSkin, SupportStatus::Derived,
        {"body-skin", "face_parsing/skin", "face_parsing/nose"});
  claim(rules, SemanticPart::Eyebrow, SupportStatus::Derived,
        {"face_parsing/left_eyebrow", "face_parsing/right_eyebrow"});
  claim(rules, SemanticPart::Lips, SupportStatus::Derived, {"face_parsing/upper_lip", "face_parsing/lower_lip"});
  claim(rules, SemanticPart::Ear, SupportStatus::Derived, {"face_parsing/left_ear", "face_parsing/right_ear"});
  rule_of(rules, SemanticPart::Neck).diagnostic =
      "face crops can cut the neck; MediaPipe body-skin includes it but not separately";
  rule_of(rules, SemanticPart::EyeWhite).diagnostic = "BiSeNet eye labels do not separate eye white and iris";
  rule_of(rules, SemanticPart::Iris).diagnostic = "BiSeNet eye labels do not separate eye white and iris";
  rule_of(rules, SemanticPart::Teeth).diagnostic = "BiSeNet mouth label does not separate teeth and oral cavity";
  rule_of(rules, SemanticPart::OralCavity).diagnostic =
      "BiSeNet mouth label does not separate teeth and oral cavity";
  rule_of(rules, SemanticPart::VeilOrHat).diagnostic = "BiSeNet hat gave false positives on sample images";
  rule_of(rules, SemanticPart::Jewelry).diagnostic = "earring/necklace labels do not cover all jewellery";
  return rules;
}

std::vector<std::string> portrait_source_labels() {
  std::vector<std::string> labels;
  for (const auto& rule : portrait_rules()) {
    for (const auto& label : rule.source_labels) {
      if (std::find(labels.begin(), labels.end(), label) == labels.end()) labels.push_back(label);
    }
  }
  return labels;
}

std::vector<std::string> bisenet_source_labels() {
  std::vector<std::string> labels;
  for (const auto& label : portrait_source_labels()) {
    if (label.rfind("face_parsing/", 0) == 0) labels.push_back(label);
  }
  return labels;
}

}  // namespace pixcake::renderd
