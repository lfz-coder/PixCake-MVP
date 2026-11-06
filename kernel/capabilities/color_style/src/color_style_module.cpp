#include "pixcake/capabilities/color_style/color_style_module.hpp"

#include <utility>

namespace pixcake {

// AI 色彩风格（需求第 10.2 节）。程度 0–100。
std::unique_ptr<CapabilityModule> make_color_style_module(CapabilityDescriptor descriptor) {
  // 没有写明依赖检测、素材或纯色背景，也没有跳到创意的入口（架构第 7.2、7.5 节）。
  return std::make_unique<ParameterBackedModule>(std::move(descriptor), ModuleTraits{});
}

}  // namespace pixcake
