#include "pixcake/capabilities/color_global/color_global_module.hpp"

#include <utility>

namespace pixcake {

// 全图色彩工具：白平衡、影调、配置文件、色阶、HSL、可选颜色、颜色分级、色彩编辑器、黑白组（需求第 10.3 节）。
// 配置文件决定哪一组参与绘制，规则登记在参数模式的禁用规则里（架构第 7.4 节末）：黑白配置文件下 HSL、
// 色彩编辑器、鲜艳度、饱和度不画，黑白组画；其他配置文件相反。切换只改变哪一边参与编译，不删数据。
std::unique_ptr<CapabilityModule> make_color_global_module(CapabilityDescriptor descriptor) {
  // 没有写明依赖检测、素材或纯色背景，也没有跳到创意的入口（架构第 7.2、7.5 节）。
  return std::make_unique<ParameterBackedModule>(std::move(descriptor), ModuleTraits{});
}

}  // namespace pixcake
