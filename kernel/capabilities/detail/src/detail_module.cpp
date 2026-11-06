#include "pixcake/capabilities/detail/detail_module.hpp"

#include <map>
#include <utility>

namespace pixcake {

DetailModule::DetailModule(CapabilityDescriptor descriptor) : ParameterBackedModule(std::move(descriptor)) {}

bool DetailModule::draws_parameter(const ParameterReader& reader, const ParameterMode& mode) const {
  return parameter_disabled_reason(reader, mode.parameter_id).empty();
}

ModuleCompile DetailModule::compile_by_stage(const ParameterReader& reader, const std::string& only) const {
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

ModuleCompile DetailModule::compile(const ParameterReader& reader) const { return compile_by_stage(reader, ""); }

ModuleCompile DetailModule::compile_interactive(const ParameterReader& reader, const std::string& parameter_id) const {
  if (parameter_id.empty()) return {};
  return compile_by_stage(reader, parameter_id);
}

}  // namespace pixcake
