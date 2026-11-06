#include "pixcake/capabilities/color_match/color_match_module.hpp"

#include <utility>

namespace pixcake {
namespace {

// AI 追色（需求第 10.1 节）。程度 0–200。精准控制（契约 C-33，架构“写回的存放与切换”）：
// precise 是开关，本身不画；打开时程度通道挂起，效果来自色彩片段里的追色写回记录，程度值保留、仍可调。
class ColorMatchModule final : public ParameterBackedModule {
 public:
  explicit ColorMatchModule(CapabilityDescriptor descriptor) : ParameterBackedModule(std::move(descriptor), {}) {}

 protected:
  bool draws_parameter(const ParameterReader& reader, const ParameterMode& mode) const override {
    const auto& toggle = match_writeback_switch();
    if (mode.parameter_id == toggle.parameter_id) return false;
    if (reader.value(toggle.capability_id, toggle.parameter_id) == std::optional<std::string>(toggle.on_value)) {
      return false;
    }
    return ParameterBackedModule::draws_parameter(reader, mode);
  }
};

}  // namespace

std::unique_ptr<CapabilityModule> make_color_match_module(CapabilityDescriptor descriptor) {
  // 没有写明依赖检测、素材或纯色背景，也没有跳到创意的入口（架构第 7.2、7.5 节）。
  return std::make_unique<ColorMatchModule>(std::move(descriptor));
}

}  // namespace pixcake
