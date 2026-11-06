#include "pixcake/capabilities/crop/crop_module.hpp"

#include <utility>

namespace pixcake {

// 裁剪旋转（需求第 12.6 节）。裁剪作为输出变换，排在管线最后（架构第 7.4 节）。
std::unique_ptr<CapabilityModule> make_crop_module(CapabilityDescriptor descriptor) {
  ModuleTraits traits;
  traits.generation_entries = {{"creative.outpaint", "AI 背景扩图"}};  // 需求第 640 行
  return std::make_unique<ParameterBackedModule>(std::move(descriptor), std::move(traits));
}

}  // namespace pixcake
