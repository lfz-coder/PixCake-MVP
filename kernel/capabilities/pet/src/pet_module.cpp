#include "pixcake/capabilities/pet/pet_module.hpp"

#include <utility>

namespace pixcake {

// 宠物美化（需求第 12.5 节）。宠物眼神光三个参数在这里。
std::unique_ptr<CapabilityModule> make_pet_module(CapabilityDescriptor descriptor) {
  // 没有写明依赖检测、素材或纯色背景，也没有跳到创意的入口（架构第 7.2、7.5 节）。
  return std::make_unique<ParameterBackedModule>(std::move(descriptor), ModuleTraits{});
}

}  // namespace pixcake
