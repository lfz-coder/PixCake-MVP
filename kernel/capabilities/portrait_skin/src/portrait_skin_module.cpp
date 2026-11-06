#include "pixcake/capabilities/portrait_skin/portrait_skin_module.hpp"

#include <utility>

namespace pixcake {

// 皮肤调整（需求第 11.3 节）。
std::unique_ptr<CapabilityModule> make_portrait_skin_module(CapabilityDescriptor descriptor) {
  ModuleTraits traits;
  traits.needs_detection = true;  // 架构第 6.7 节
  traits.generation_entries = {{"creative.face_redraw", "AI 面部重绘"}};  // 需求第 525、746 行
  return std::make_unique<ParameterBackedModule>(std::move(descriptor), std::move(traits));
}

}  // namespace pixcake
