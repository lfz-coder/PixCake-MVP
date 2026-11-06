#include "pixcake/capabilities/color_global/color_global_module.hpp"

#include <utility>

namespace pixcake {

ColorGlobalModule::ColorGlobalModule(CapabilityDescriptor descriptor) : ParameterBackedModule(std::move(descriptor)) {}

bool ColorGlobalModule::draws_parameter(const ParameterReader& reader, const ParameterMode& mode) const {
  // 参数模式里登记的禁用规则就是配置文件的切换规则；被禁用的一组这一帧不参与绘制。
  return parameter_disabled_reason(reader, mode.parameter_id).empty();
}

}  // namespace pixcake
