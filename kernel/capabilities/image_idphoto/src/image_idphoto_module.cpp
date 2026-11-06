#include "pixcake/capabilities/image_idphoto/image_idphoto_module.hpp"

#include <utility>

namespace pixcake {

// 证件照换背景（需求第 12.2 节）。
std::unique_ptr<CapabilityModule> make_image_idphoto_module(CapabilityDescriptor descriptor) {
  ModuleTraits traits;
  traits.generation_entries = {{"creative.idphoto_clothes", "证件照换衣服"}};  // 需求第 610 行
  return std::make_unique<ParameterBackedModule>(std::move(descriptor), std::move(traits));
}

}  // namespace pixcake
