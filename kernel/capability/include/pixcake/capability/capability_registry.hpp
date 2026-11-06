#pragma once

#include <cstdint>
#include <optional>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pixcake {

struct CapabilityDescriptor {
  std::string id;
  std::string module_label;
  std::string label;
  bool registered_for_drawing = true;
  bool can_use_in_preset = true;
  bool can_batch_sync = true;
  std::string batch_sync_blocked_reason;
};

class CapabilityRegistry {
 public:
  static CapabilityRegistry phase_one();

  const CapabilityDescriptor* find(std::string_view id) const;
  const std::vector<CapabilityDescriptor>& descriptors() const;

 private:
  explicit CapabilityRegistry(std::vector<CapabilityDescriptor> descriptors);

  std::vector<CapabilityDescriptor> descriptors_;
};

struct PipelineStep {
  std::string step_id;
  std::string capability_id;
  bool registered_for_drawing = true;
  std::vector<std::string> dependencies;

  PipelineStep() = default;
  PipelineStep(std::string step, std::string capability, bool registered,
               std::vector<std::string> deps = {})
      : step_id(std::move(step)),
        capability_id(std::move(capability)),
        registered_for_drawing(registered),
        dependencies(std::move(deps)) {}
};

// 条件参数取某个枚举值时，目标参数不可编辑。when_zero 为真时改为：条件参数（数值）为 0 时不可编辑，
// 不看 when_choice；没有值时按条件参数的默认值判断。
struct ParameterDisableRule {
  std::string when_capability_id;
  std::string when_parameter_id;
  std::string when_choice;
  std::string reason;
  bool when_zero = false;
};

struct ParameterMode {
  std::string capability_id;
  std::string parameter_id;
  double minimum = 0.0;
  double maximum = 0.0;
  double step = 1.0;
  double default_value = 0.0;
  // Stable render contract fields. They are descriptive strings, not GPU
  // handles or backend types; the renderer may map them to a shader later.
  std::string render_stage;
  std::string render_program;
  std::string render_channel;
  std::uint32_t schema_version = 1;
  std::uint32_t order = 0;
  // 非空即为枚举参数，显示值只能是其中之一；minimum/maximum/step 不使用。
  std::vector<std::string> choices;
  std::string default_choice;
  // 需求或契约没有写明步进、默认值时为假：校验只查范围，界面标“待确认”。
  bool step_confirmed = true;
  bool default_confirmed = true;
  std::vector<ParameterDisableRule> disable_rules;
};

std::vector<PipelineStep> phase_one_pipeline();
bool validate_phase_one_pipeline(const std::vector<PipelineStep>& pipeline, std::string* error);
std::vector<std::string> phase_one_pipeline_order(std::string* error = nullptr);
std::optional<std::uint32_t> phase_one_capability_order(std::string_view capability_id);
const std::vector<ParameterMode>& phase_one_parameter_modes();
const ParameterMode* find_parameter_mode(std::string_view capability_id, std::string_view parameter_id);
// 参数当前是否不可编辑。committed_value 返回条件参数的已提交值；没有时按该参数的 default_choice 判断。
std::optional<std::string> parameter_disabled_reason(
    std::string_view capability_id, std::string_view parameter_id,
    const std::function<std::optional<std::string>(std::string_view, std::string_view)>& committed_value);

// Validates the ranges and steps explicitly defined by the phase-one contract.
// An unknown parameter is rejected until its owning capability publishes a
// display mode.
bool validate_parameter_display_value(std::string_view capability_id, std::string_view parameter_id,
                                      std::string_view display_value);

}  // namespace pixcake
