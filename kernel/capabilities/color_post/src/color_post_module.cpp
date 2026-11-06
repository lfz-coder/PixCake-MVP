#include "pixcake/capabilities/color_post/color_post_module.hpp"

#include <utility>

namespace pixcake {

// 后期调色（需求第 8.4 节）。读取前面已经形成的图像，不读人像能力的私有滑杆（架构第 7.4 节）。
std::unique_ptr<CapabilityModule> make_color_post_module(CapabilityDescriptor descriptor) {
  // 没有写明依赖检测、素材或纯色背景，也没有跳到创意的入口（架构第 7.2、7.5 节）。
  return std::make_unique<ParameterBackedModule>(std::move(descriptor), ModuleTraits{});
}

}  // namespace pixcake
