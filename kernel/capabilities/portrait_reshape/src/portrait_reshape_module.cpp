#include "pixcake/capabilities/portrait_reshape/portrait_reshape_module.hpp"

#include <utility>

namespace pixcake {

// 面部重塑（需求第 11.4 节）。
std::unique_ptr<CapabilityModule> make_portrait_reshape_module(CapabilityDescriptor descriptor) {
  ModuleTraits traits;
  traits.needs_detection = true;  // 架构第 6.7 节
  return std::make_unique<ParameterBackedModule>(std::move(descriptor), std::move(traits));
}

}  // namespace pixcake
