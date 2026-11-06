#include "pixcake/capabilities/color_match/color_match_module.hpp"

#include <utility>

namespace pixcake {

// AI 追色（需求第 10.1 节）。程度 0–200。精准控制与单一来源（契约 C-33）尚未实现。
std::unique_ptr<CapabilityModule> make_color_match_module(CapabilityDescriptor descriptor) {
  // 没有写明依赖检测、素材或纯色背景，也没有跳到创意的入口（架构第 7.2、7.5 节）。
  return std::make_unique<ParameterBackedModule>(std::move(descriptor), ModuleTraits{});
}

}  // namespace pixcake
