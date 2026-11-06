#include "pixcake/capabilities/portrait_blemish/portrait_blemish_module.hpp"

#include <utility>

namespace pixcake {

// 祛除瑕疵（需求第 11.2 节）。
std::unique_ptr<CapabilityModule> make_portrait_blemish_module(CapabilityDescriptor descriptor) {
  ModuleTraits traits;
  traits.needs_detection = true;  // 架构第 6.7 节
  return std::make_unique<ParameterBackedModule>(std::move(descriptor), std::move(traits));
}

}  // namespace pixcake
