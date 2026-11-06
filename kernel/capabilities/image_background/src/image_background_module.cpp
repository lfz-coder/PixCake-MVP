#include "pixcake/capabilities/image_background/image_background_module.hpp"

#include <utility>

namespace pixcake {

// 背景调整（需求第 12.1 节）。
std::unique_ptr<CapabilityModule> make_image_background_module(CapabilityDescriptor descriptor) {
  ModuleTraits traits;
  // 需求第 590、905 行：祛穿帮、祛瑕疵、光影均匀、祛色彩断层只对纯色背景生效；目前登记了光影均匀的明度。
  traits.solid_background_parameters = {"light_uniform_brightness"};
  traits.generation_entries = {{"creative.remove_passerby", "AI 祛路人"},  // 需求第 594 行
                               {"creative.fill_grass", "AI 补草地"},
                               {"creative.backdrop", "AI 布景"},
                               {"creative.winter_snow", "冬日雪景"}};  // 需求第 767 行
  return std::make_unique<ParameterBackedModule>(std::move(descriptor), std::move(traits));
}

}  // namespace pixcake
