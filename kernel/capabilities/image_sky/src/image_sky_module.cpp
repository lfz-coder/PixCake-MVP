#include "pixcake/capabilities/image_sky/image_sky_module.hpp"

#include <utility>

namespace pixcake {

// 换天空（需求第 12.3 节）。
std::unique_ptr<CapabilityModule> make_image_sky_module(CapabilityDescriptor descriptor) {
  ModuleTraits traits;
  traits.needs_assets = true;  // 需求第 12.3 节：用内置素材或上传的素材
  return std::make_unique<ParameterBackedModule>(std::move(descriptor), std::move(traits));
}

}  // namespace pixcake
