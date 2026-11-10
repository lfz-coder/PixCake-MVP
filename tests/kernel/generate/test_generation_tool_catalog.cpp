#include "pixcake/generate/generation_tool_catalog.hpp"

#include <cstdio>
#include <string>
#include <utility>

namespace {

int g_failures = 0;

void expect(bool condition, const char* expression, int line) {
  if (!condition) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, expression);
    ++g_failures;
  }
}

#define EXPECT(expression) expect(static_cast<bool>(expression), #expression, __LINE__)

void test_fixture_catalog_builds_provider_neutral_task() {
  const auto catalog = pixcake::generate::ToolCatalog::development_fixture();
  const auto* descriptor = catalog.find(pixcake::generate::kDevelopmentFixtureToolId);
  EXPECT(descriptor != nullptr);
  EXPECT(descriptor != nullptr && descriptor->spec.parameter_mode_id == "fixture-default");
  EXPECT(descriptor != nullptr && descriptor->adapter_id == "fixture");

  pixcake::generate::GenerationTask task;
  EXPECT(catalog.build_task(pixcake::generate::kDevelopmentFixtureToolId, "catalog-task", "image-1", 9,
                            {1, 2, 3}, {"mask-1"}, {"reference-1"}, {}, task) == pixcake::ResultCode::Ok);
  EXPECT(task.task_id == "catalog-task" && task.image_id == "image-1" && task.source_revision == 9);
  EXPECT(task.tool.tool_id == pixcake::generate::kDevelopmentFixtureToolId);
  EXPECT(task.idempotency_key == task.task_id && task.parameter_payload == std::vector<std::uint8_t>({1, 2, 3}));
  EXPECT(task.mask_ids == std::vector<std::string>({"mask-1"}));
  EXPECT(task.reference_image_ids == std::vector<std::string>({"reference-1"}));
}

void test_unknown_and_duplicate_tools_are_rejected() {
  pixcake::generate::ToolCatalog catalog;
  pixcake::generate::ToolDescriptor descriptor;
  descriptor.spec.tool_id = "tool-a";
  descriptor.spec.parameter_mode_id = "mode-a";
  descriptor.spec.billing_rule_id = "billing-a";
  descriptor.title = "Tool A";
  descriptor.category = "creative";
  descriptor.adapter_id = "provider-a";
  EXPECT(catalog.add(descriptor) == pixcake::ResultCode::Ok);
  EXPECT(catalog.add(descriptor) == pixcake::ResultCode::StateConflict);

  pixcake::generate::GenerationTask task;
  EXPECT(catalog.build_task("missing", "task", "image", 1, {}, {}, {}, {}, task) ==
         pixcake::ResultCode::UnsupportedFormat);
}

void test_duplicate_parameters_and_oversized_payloads_are_rejected() {
  pixcake::generate::ToolCatalog catalog;
  pixcake::generate::ToolDescriptor descriptor;
  descriptor.spec.tool_id = "tool-duplicate-parameter";
  descriptor.spec.parameter_mode_id = "mode";
  descriptor.spec.billing_rule_id = "billing";
  descriptor.title = "Tool";
  descriptor.category = "creative";
  descriptor.adapter_id = "provider";
  descriptor.parameters.push_back({"value", "Value", pixcake::generate::ToolParameterType::Text, {}, {}, 0, false});
  descriptor.parameters.push_back(
      {"value", "Value again", pixcake::generate::ToolParameterType::Text, {}, {}, 0, false});
  EXPECT(catalog.add(std::move(descriptor)) == pixcake::ResultCode::StateConflict);

  const auto fixture = pixcake::generate::ToolCatalog::development_fixture();
  pixcake::generate::GenerationTask task;
  EXPECT(fixture.build_task(pixcake::generate::kDevelopmentFixtureToolId, "task", "image", 1,
                            std::vector<std::uint8_t>(32 * 1024 + 1, 0), {}, {}, {}, task) ==
         pixcake::ResultCode::StateConflict);
}

void test_input_capabilities_are_enforced() {
  pixcake::generate::ToolCatalog catalog;
  pixcake::generate::ToolDescriptor descriptor;
  descriptor.spec.tool_id = "tool-no-inputs";
  descriptor.spec.parameter_mode_id = "mode";
  descriptor.spec.billing_rule_id = "billing";
  descriptor.title = "No inputs";
  descriptor.category = "creative";
  descriptor.adapter_id = "provider";
  EXPECT(catalog.add(std::move(descriptor)) == pixcake::ResultCode::Ok);

  pixcake::generate::GenerationTask task;
  EXPECT(catalog.build_task("tool-no-inputs", "task", "image", 1, {}, {"mask"}, {}, {}, task) ==
         pixcake::ResultCode::UnsupportedFormat);
  EXPECT(catalog.build_task("tool-no-inputs", "task", "image", 1, {}, {}, {"reference"}, {}, task) ==
         pixcake::ResultCode::UnsupportedFormat);
}

void test_parameter_defaults_have_stable_opaque_encoding() {
  const auto catalog = pixcake::generate::ToolCatalog::development_fixture();
  std::vector<std::pair<std::string, std::string>> values;
  std::vector<std::uint8_t> first;
  std::vector<std::uint8_t> second;
  EXPECT(catalog.default_parameters(pixcake::generate::kDevelopmentFixtureToolId, values) ==
         pixcake::ResultCode::Ok);
  EXPECT(values.size() == 4 && values.front().first == "prompt" && values.front().second.empty());
  EXPECT(values[1].first == "strength" && values[1].second == "1.0");
  EXPECT(values[2].first == "mode" && values[2].second == "standard");
  EXPECT(values[3].first == "preserve" && values[3].second == "false");
  EXPECT(catalog.encode_parameters(pixcake::generate::kDevelopmentFixtureToolId, values, first) ==
         pixcake::ResultCode::Ok);
  EXPECT(catalog.encode_parameters(pixcake::generate::kDevelopmentFixtureToolId, values, second) ==
         pixcake::ResultCode::Ok && first == second && first.size() > 8);
  values.front().second = std::string("bad\0prompt", 10);
  EXPECT(catalog.encode_parameters(pixcake::generate::kDevelopmentFixtureToolId, values, second) ==
         pixcake::ResultCode::StateConflict);
  values.front().second.clear();
  values[2].second = "unknown";
  EXPECT(catalog.encode_parameters(pixcake::generate::kDevelopmentFixtureToolId, values, second) ==
         pixcake::ResultCode::StateConflict);
}

void test_parameter_payload_can_be_decoded_for_retry() {
  const auto catalog = pixcake::generate::ToolCatalog::development_fixture();
  std::vector<std::pair<std::string, std::string>> values;
  std::vector<std::uint8_t> payload;
  EXPECT(catalog.default_parameters(pixcake::generate::kDevelopmentFixtureToolId, values) ==
         pixcake::ResultCode::Ok);
  values[0].second = "remove the passerby";
  values[1].second = "0.75";
  EXPECT(catalog.encode_parameters(pixcake::generate::kDevelopmentFixtureToolId, values, payload) ==
         pixcake::ResultCode::Ok);
  std::vector<std::pair<std::string, std::string>> decoded;
  EXPECT(catalog.decode_parameters(pixcake::generate::kDevelopmentFixtureToolId, payload, decoded) ==
         pixcake::ResultCode::Ok);
  EXPECT(decoded == values);
  payload.back() ^= 0x7f;
  EXPECT(catalog.decode_parameters(pixcake::generate::kDevelopmentFixtureToolId, payload, decoded) ==
         pixcake::ResultCode::StateConflict);
}

}  // namespace

int main() {
  test_fixture_catalog_builds_provider_neutral_task();
  test_unknown_and_duplicate_tools_are_rejected();
  test_duplicate_parameters_and_oversized_payloads_are_rejected();
  test_input_capabilities_are_enforced();
  test_parameter_defaults_have_stable_opaque_encoding();
  test_parameter_payload_can_be_decoded_for_retry();
  return g_failures == 0 ? 0 : 1;
}
