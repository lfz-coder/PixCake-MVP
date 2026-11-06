#pragma once

#include "pixcake/capability/capability_module.hpp"

namespace pixcake {

// 细节（契约 §4 `detail`）：降噪、锐化、噪点、杂色、颗粒、镜头、透视、校准。各子工具有自己的绘制阶段，
// 编译时每个阶段一条绘制描述。本阶段只登记了颗粒：颗粒效果为 0 时，其余颗粒参数不可调节也不绘制。
class DetailModule final : public ParameterBackedModule {
 public:
  explicit DetailModule(CapabilityDescriptor descriptor);

  ModuleCompile compile(const ParameterReader& reader) const override;
  ModuleCompile compile_interactive(const ParameterReader& reader, const std::string& parameter_id) const override;

 protected:
  bool draws_parameter(const ParameterReader& reader, const ParameterMode& mode) const override;

 private:
  ModuleCompile compile_by_stage(const ParameterReader& reader, const std::string& only) const;
};

}  // namespace pixcake
