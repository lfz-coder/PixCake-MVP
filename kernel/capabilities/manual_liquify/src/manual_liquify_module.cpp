#include "pixcake/capabilities/manual_liquify/manual_liquify_module.hpp"

#include <utility>

namespace pixcake {

// 液化（需求第 13.3 节）。人像液化的锚点也写在这里（架构第 7.3 节）。
std::unique_ptr<CapabilityModule> make_manual_liquify_module(CapabilityDescriptor descriptor) {
  // 没有写明依赖检测、素材或纯色背景，也没有跳到创意的入口（架构第 7.2、7.5 节）。
  return std::make_unique<ParameterBackedModule>(std::move(descriptor), ModuleTraits{});
}

}  // namespace pixcake
