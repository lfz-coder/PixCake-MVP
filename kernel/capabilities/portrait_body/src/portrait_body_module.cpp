#include "pixcake/capabilities/portrait_body/portrait_body_module.hpp"

#include <utility>

namespace pixcake {

// 手、脚、全身美型（需求第 11.6 节）。
std::unique_ptr<CapabilityModule> make_portrait_body_module(CapabilityDescriptor descriptor) {
  ModuleTraits traits;
  traits.needs_detection = true;  // 架构第 6.7 节
  traits.may_break_image = true;  // 架构第 369、623 行：全身美型会坏片时相关滑杆不可用
  traits.generation_entries = {{"creative.slim_belly", "AI 瘦肚子"}, {"creative.add_abs", "AI 加腹肌"}};  // 需求第 584 行
  return std::make_unique<ParameterBackedModule>(std::move(descriptor), std::move(traits));
}

}  // namespace pixcake
