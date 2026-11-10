#pragma once

#include "pixcake/generate/generation_task.hpp"

#include <cstdint>
#include <utility>
#include <string>
#include <string_view>
#include <vector>

namespace pixcake::generate {

// Stable identifiers for the development provider.  Production providers are
// registered from their own configuration once their contract is available.
inline constexpr char kDevelopmentFixtureToolId[] = "creative.fixture";

enum class ToolParameterType {
  Text,
  Number,
  Choice,
  Boolean,
};

struct ToolParameterDescriptor {
  std::string id;
  std::string title;
  ToolParameterType type = ToolParameterType::Text;
  std::string default_value;
  std::vector<std::string> choices;
  std::uint32_t max_length = 0;
  bool required = false;
};

// Provider-neutral metadata used by the shell and the generated-process
// router.  It deliberately contains no endpoint, credential, JSON field, or
// SDK type.  The adapter id only selects a process-side implementation.
struct ToolDescriptor {
  ToolSpec spec;
  std::string title;
  std::string category;
  std::string adapter_id;
  bool supports_masks = false;
  bool supports_references = false;
  std::vector<ToolParameterDescriptor> parameters;
};

// In-memory catalog boundary for generation tools.  A remote catalog or a
// signed package can populate the same structure later without changing the
// task, ledger, or layer contracts.
class ToolCatalog {
 public:
  ToolCatalog() = default;

  // Development-only catalog used by the local fixture path.  It is explicit
  // so a release build cannot accidentally present it as an official tool.
  static ToolCatalog development_fixture();

  ResultCode add(ToolDescriptor descriptor);
  const ToolDescriptor* find(std::string_view tool_id) const;
  const std::vector<ToolDescriptor>& descriptors() const { return descriptors_; }

  ResultCode default_parameters(std::string_view tool_id,
                                std::vector<std::pair<std::string, std::string>>& out) const;
  ResultCode encode_parameters(std::string_view tool_id,
                               const std::vector<std::pair<std::string, std::string>>& values,
                               std::vector<std::uint8_t>& out) const;
  // Decodes the catalog-owned opaque parameter payload back into validated
  // display values.  This is used only for durable retry; provider wire
  // fields still remain outside the kernel catalog.
  ResultCode decode_parameters(std::string_view tool_id, const std::vector<std::uint8_t>& payload,
                               std::vector<std::pair<std::string, std::string>>& out) const;

  // Builds a complete task from catalog metadata.  The caller supplies the
  // image/document inputs; provider-specific parameter bytes remain opaque.
  ResultCode build_task(std::string_view tool_id, std::string task_id, std::string image_id,
                        std::uint64_t source_revision, std::vector<std::uint8_t> parameter_payload,
                        std::vector<std::string> mask_ids, std::vector<std::string> reference_image_ids,
                        std::string idempotency_key, GenerationTask& out) const;

 private:
  std::vector<ToolDescriptor> descriptors_;
};

}  // namespace pixcake::generate
