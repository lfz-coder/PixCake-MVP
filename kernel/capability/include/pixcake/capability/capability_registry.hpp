#pragma once

#include <string>
#include <string_view>
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
};

std::vector<PipelineStep> phase_one_pipeline();
bool validate_phase_one_pipeline(const std::vector<PipelineStep>& pipeline, std::string* error);

}  // namespace pixcake
