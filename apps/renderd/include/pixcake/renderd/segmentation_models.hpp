#pragma once

#include "pixcake/renderd/semantic_label_adapter.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace pixcake::renderd {

// MediaPipe SelfieMulticlass 256x256 (Apache-2.0).  Labels are the model's own
// embedded labels.txt, in output-channel order.  Model identity and SHA-256 are
// locked in third_party/models.lock.cmake.
inline constexpr std::array<const char*, 6> kMediaPipeSelfieMulticlassLabels = {
    "background", "hair", "body-skin", "face-skin", "clothes", "others"};
inline constexpr const char* kMediaPipeSelfieMulticlassModelId = "mediapipe.selfie_multiclass_256x256";
inline constexpr const char* kPortraitModelId = "pixcake.portrait";
// Versions of the three locked models (third_party/models.lock.cmake).
inline constexpr const char* kPortraitModelVersion =
    "selfie_multiclass_256x256 float32/1; blaze_face_full_range float16/1; bisenet_resnet18 v0.0.2";
inline constexpr const char* kMediaPipeSelfieMulticlassModelVersion = "float32/1";
inline constexpr std::uint32_t kMediaPipeSelfieMulticlassInput = 256;

// Mapping verified on sample images; see
// docs/superpowers/evidence/2026-11-06-segmentation-label-mapping.md.
std::vector<SemanticLabelRule> mediapipe_selfie_multiclass_rules();

// BiSeNet face parsing (yakhyo/face-parsing v0.0.2, ResNet18, CelebAMask-HQ
// classes), run on face crops.  Labels are prefixed so they never collide with
// MediaPipe's own "hair"/"skin"-like names; order is the model's channel order.
inline constexpr std::array<const char*, 19> kBiSeNetLabels = {
    "face_parsing/background", "face_parsing/skin",        "face_parsing/left_eyebrow",
    "face_parsing/right_eyebrow", "face_parsing/left_eye",   "face_parsing/right_eye",
    "face_parsing/eyeglasses", "face_parsing/left_ear",    "face_parsing/right_ear",
    "face_parsing/earring",    "face_parsing/nose",        "face_parsing/mouth",
    "face_parsing/upper_lip",  "face_parsing/lower_lip",   "face_parsing/neck",
    "face_parsing/necklace",   "face_parsing/cloth",       "face_parsing/hair",
    "face_parsing/hat"};
inline constexpr const char* kBiSeNetModelId = "yakhyo.face_parsing.bisenet_resnet18";
inline constexpr const char* kBiSeNetModelVersion = "v0.0.2";
inline constexpr std::uint32_t kBiSeNetInput = 512;
// Face crop: square around the detected face, side = longer box side x 2.0
// (sample check: 1.6 / 2.0 / 2.4 give the same masks).
inline constexpr double kBiSeNetCropExpand = 2.0;

// MediaPipe for the whole person plus BiSeNet inside face crops.
std::vector<SemanticLabelRule> portrait_rules();
// Every source label portrait_rules() reads, and the BiSeNet subset of it.
std::vector<std::string> portrait_source_labels();
std::vector<std::string> bisenet_source_labels();

}  // namespace pixcake::renderd
