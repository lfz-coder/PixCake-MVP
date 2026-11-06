#include "pixcake/capabilities/detail/detail_module.hpp"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace pixcake {
namespace {

// 细节：降噪、锐化、噪点、杂色、颗粒、镜头、透视、校准（架构第 7.3 节）。各子工具有自己的绘制阶段，
// 编译时每个阶段一条绘制描述。本阶段只登记了颗粒；颗粒效果为 0 时其余颗粒参数不可调节也不绘制（参数模式规则）。
class DetailModule final : public ParameterBackedModule {
 public:
  explicit DetailModule(CapabilityDescriptor descriptor) : ParameterBackedModule(std::move(descriptor), {}) {}

  ModuleCompile compile(const ParameterReader& reader) const override { return compile_by_stage(reader, ""); }

  ModuleCompile compile_interactive(const ParameterReader& reader, const std::string& parameter_id) const override {
    if (parameter_id.empty()) return {};
    return compile_by_stage(reader, parameter_id);
  }

 private:
  ModuleCompile compile_by_stage(const ParameterReader& reader, const std::string& only) const {
    // 按阶段分组，保持参数模式里的先后。
    std::vector<std::string> stages;
    std::map<std::string, std::vector<const ParameterMode*>> grouped;
    for (const auto* mode : parameter_modes()) {
      if (grouped.find(mode->render_stage) == grouped.end()) stages.push_back(mode->render_stage);
      grouped[mode->render_stage].push_back(mode);
    }
    ModuleCompile result;
    for (const auto& stage : stages) {
      ModuleCompile part = compile_modes(reader, grouped[stage], only);
      for (auto& step : part.steps) result.steps.push_back(std::move(step));
      for (auto& diagnostic : part.diagnostics) result.diagnostics.push_back(std::move(diagnostic));
    }
    return result;
  }
};

}  // namespace

std::unique_ptr<CapabilityModule> make_detail_module(CapabilityDescriptor descriptor) {
  return std::make_unique<DetailModule>(std::move(descriptor));
}

}  // namespace pixcake
