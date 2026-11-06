#include "pixcake/capabilities/portrait_feature/portrait_feature_module.hpp"

#include <utility>

namespace pixcake {

// 表情、牙齿、眼睛、妆容、头发（需求第 11.5 节）。眼神光三个参数在这里。
std::unique_ptr<CapabilityModule> make_portrait_feature_module(CapabilityDescriptor descriptor) {
  ModuleTraits traits;
  traits.needs_detection = true;  // 架构第 6.7 节
  return std::make_unique<ParameterBackedModule>(std::move(descriptor), std::move(traits));
}

}  // namespace pixcake
