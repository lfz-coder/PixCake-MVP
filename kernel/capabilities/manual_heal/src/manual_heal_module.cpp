#include "pixcake/capabilities/manual_heal/manual_heal_module.hpp"

#include <utility>

namespace pixcake {

// 污点修复、修补、仿制（需求第 13.4 节）。
std::unique_ptr<CapabilityModule> make_manual_heal_module(CapabilityDescriptor descriptor) {
  // 没有写明依赖检测、素材或纯色背景，也没有跳到创意的入口（架构第 7.2、7.5 节）。
  return std::make_unique<ParameterBackedModule>(std::move(descriptor), ModuleTraits{});
}

}  // namespace pixcake
