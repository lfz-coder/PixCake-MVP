#include "pixcake/capability/capability_registry.hpp"

#include <unordered_set>
#include <utility>

namespace pixcake {
namespace {

constexpr char kRecomputeReason[] = "同步时按每张图重新计算，不是把数值原样抄过去";

std::vector<CapabilityDescriptor> phase_one_descriptors() {
  return {
      {"color.match", "AI 色彩", "AI 追色", true, true, false, kRecomputeReason},
      {"color.style", "AI 色彩", "AI 色彩风格", true, false, false, kRecomputeReason},
      {"color.global", "色彩", "全图色彩工具", true, true, true, ""},
      {"color.local", "色彩", "局部调色", true, true, true, ""},
      {"color.post", "色彩", "后期调色", true, true, true, ""},
      {"portrait.blemish", "人像", "祛除瑕疵", true, true, true, ""},
      {"portrait.skin", "人像", "皮肤调整", true, true, true, ""},
      {"portrait.reshape", "人像", "面部重塑", true, true, true, ""},
      {"portrait.feature", "人像", "表情、牙齿、眼睛、妆容、头发", true, true, true, ""},
      {"portrait.body", "人像", "手、脚、全身美型", true, true, true, ""},
      {"image.background", "图像", "背景调整", true, true, true, ""},
      {"image.idphoto", "图像", "证件照换背景", true, true, true, ""},
      {"image.sky", "图像", "换天空", true, true, true, ""},
      {"clothes", "衣物", "衣物美化", true, true, true, ""},
      {"pet", "宠物", "宠物美化", true, false, true, ""},
      {"crop", "裁剪", "裁剪旋转", true, true, true, ""},
      {"manual.edge", "手动", "祛边", true, false, true, ""},
      {"manual.liquify", "手动", "液化", true, false, true, ""},
      {"manual.heal", "手动", "污点修复、修补、仿制", true, false, true, ""},
  };
}

}  // namespace

CapabilityRegistry::CapabilityRegistry(std::vector<CapabilityDescriptor> descriptors)
    : descriptors_(std::move(descriptors)) {}

CapabilityRegistry CapabilityRegistry::phase_one() {
  return CapabilityRegistry(phase_one_descriptors());
}

const CapabilityDescriptor* CapabilityRegistry::find(std::string_view id) const {
  for (const auto& descriptor : descriptors_) {
    if (descriptor.id == id) {
      return &descriptor;
    }
  }
  return nullptr;
}

const std::vector<CapabilityDescriptor>& CapabilityRegistry::descriptors() const { return descriptors_; }

std::vector<PipelineStep> phase_one_pipeline() {
  return {
      {"detail.geometry", "detail", false},
      {"detail.denoise", "detail", false},
      {"color.match", "color.match", true},
      {"color.style", "color.style", true},
      {"color.global", "color.global", true},
      {"color.local", "color.local", true},
      {"portrait.blemish", "portrait.blemish", true},
      {"portrait.skin", "portrait.skin", true},
      {"portrait.reshape", "portrait.reshape", true},
      {"portrait.feature", "portrait.feature", true},
      {"portrait.body", "portrait.body", true},
      {"image.background", "image.background", true},
      {"image.idphoto", "image.idphoto", true},
      {"image.sky", "image.sky", true},
      {"clothes", "clothes", true},
      {"pet", "pet", true},
      {"color.post", "color.post", true},
      {"manual.liquify", "manual.liquify", true},
      {"manual.heal", "manual.heal", true},
      {"manual.edge", "manual.edge", true},
      {"crop", "crop", true},
  };
}

bool validate_phase_one_pipeline(const std::vector<PipelineStep>& pipeline, std::string* error) {
  if (error != nullptr) {
    error->clear();
  }
  std::unordered_set<std::string> seen;
  for (const auto& step : pipeline) {
    if (step.capability_id == "detail") {
      continue;
    }
    if (!seen.insert(step.capability_id).second) {
      if (error != nullptr) {
        *error = "能力在管线中重复: " + step.capability_id;
      }
      return false;
    }
  }
  return true;
}

}  // namespace pixcake
