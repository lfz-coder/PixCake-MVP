#include "pixcake/generate/generation_tool_catalog.hpp"

#include <cmath>
#include <cstdlib>
#include <limits>
#include <set>
#include <string_view>
#include <utility>

namespace pixcake::generate {
namespace {

bool valid_optional_identifier(const std::string& value) {
  return value.empty() || (value.find('\0') == std::string::npos);
}

bool valid_text(const std::string& value) { return value.find('\0') == std::string::npos; }

void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  for (unsigned int shift = 0; shift < 32; shift += 8) {
    out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
  }
}

bool read_u32(const std::vector<std::uint8_t>& bytes, std::size_t& offset, std::uint32_t& value) {
  if (offset > bytes.size() || bytes.size() - offset < 4) return false;
  value = static_cast<std::uint32_t>(bytes[offset]) |
          (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
          (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
          (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
  offset += 4;
  return true;
}

const ToolParameterDescriptor* find_parameter(const ToolDescriptor& descriptor, std::string_view id) {
  for (const auto& parameter : descriptor.parameters) {
    if (parameter.id == id) return &parameter;
  }
  return nullptr;
}

bool valid_parameter_value(const ToolParameterDescriptor& descriptor, const std::string& value) {
  if (!valid_text(value) || (descriptor.max_length != 0 && value.size() > descriptor.max_length)) return false;
  if (descriptor.required && value.empty()) return false;
  switch (descriptor.type) {
    case ToolParameterType::Text:
      return true;
    case ToolParameterType::Number: {
      if (value.empty()) return !descriptor.required;
      char* end = nullptr;
      const double parsed = std::strtod(value.c_str(), &end);
      return end != value.c_str() && *end == '\0' && std::isfinite(parsed);
    }
    case ToolParameterType::Choice:
      for (const auto& choice : descriptor.choices) {
        if (choice == value) return true;
      }
      return false;
    case ToolParameterType::Boolean:
      return value == "true" || value == "false";
  }
  return false;
}

}  // namespace

ToolCatalog ToolCatalog::development_fixture() {
  ToolCatalog catalog;
  ToolDescriptor fixture;
  fixture.spec.tool_id = kDevelopmentFixtureToolId;
  fixture.spec.parameter_mode_id = "fixture-default";
  fixture.spec.result_shape = ResultShape::NewLayer;
  fixture.spec.billing_rule_id = "fixture-points";
  fixture.spec.output_count = 1;
  fixture.spec.cancellable = true;
  fixture.title = "Creative fixture";
  fixture.category = "creative";
  fixture.adapter_id = "fixture";
  fixture.supports_masks = true;
  fixture.supports_references = true;
  fixture.parameters.push_back({"prompt", "Prompt", ToolParameterType::Text, {}, {}, 2048, false});
  fixture.parameters.push_back({"strength", "Strength", ToolParameterType::Number, "1.0", {}, 32, true});
  fixture.parameters.push_back({"mode", "Mode", ToolParameterType::Choice, "standard", {"standard", "detail"}, 0,
                                true});
  fixture.parameters.push_back({"preserve", "Preserve", ToolParameterType::Boolean, "false", {}, 0, false});
  (void)catalog.add(std::move(fixture));
  return catalog;
}

ResultCode ToolCatalog::add(ToolDescriptor descriptor) {
  if (validate_tool_spec(descriptor.spec) != ResultCode::Ok || descriptor.title.empty() ||
      descriptor.title.find('\0') != std::string::npos || descriptor.category.empty() ||
      descriptor.category.find('\0') != std::string::npos || descriptor.adapter_id.empty() ||
      descriptor.adapter_id.find('\0') != std::string::npos || find(descriptor.spec.tool_id) != nullptr) {
    return ResultCode::StateConflict;
  }
  for (const auto& parameter : descriptor.parameters) {
    if (parameter.id.empty() || !valid_text(parameter.id) || parameter.title.empty() || !valid_text(parameter.title) ||
        find_parameter(descriptor, parameter.id) != &parameter ||
        (parameter.type == ToolParameterType::Choice && parameter.choices.empty())) {
      return ResultCode::StateConflict;
    }
    if (!valid_parameter_value(parameter, parameter.default_value)) return ResultCode::StateConflict;
    for (const auto& choice : parameter.choices) {
      if (choice.empty() || !valid_text(choice)) return ResultCode::StateConflict;
    }
  }
  descriptors_.push_back(std::move(descriptor));
  return ResultCode::Ok;
}

ResultCode ToolCatalog::default_parameters(std::string_view tool_id,
                                            std::vector<std::pair<std::string, std::string>>& out) const {
  const ToolDescriptor* descriptor = find(tool_id);
  if (descriptor == nullptr) return ResultCode::UnsupportedFormat;
  out.clear();
  out.reserve(descriptor->parameters.size());
  for (const auto& parameter : descriptor->parameters) {
    out.emplace_back(parameter.id, parameter.default_value);
  }
  return ResultCode::Ok;
}

ResultCode ToolCatalog::encode_parameters(
    std::string_view tool_id, const std::vector<std::pair<std::string, std::string>>& values,
    std::vector<std::uint8_t>& out) const {
  out.clear();
  const ToolDescriptor* descriptor = find(tool_id);
  if (descriptor == nullptr) return ResultCode::UnsupportedFormat;
  if (values.size() > descriptor->parameters.size() || values.size() > 64) return ResultCode::StateConflict;

  std::vector<const ToolParameterDescriptor*> ordered;
  ordered.reserve(descriptor->parameters.size());
  for (const auto& parameter : descriptor->parameters) ordered.push_back(&parameter);
  std::vector<bool> seen(ordered.size(), false);
  std::vector<std::pair<std::string, std::string>> normalized;
  normalized.reserve(ordered.size());
  for (const auto& [id, value] : values) {
    const ToolParameterDescriptor* parameter = find_parameter(*descriptor, id);
    if (parameter == nullptr || !valid_parameter_value(*parameter, value)) return ResultCode::StateConflict;
    std::size_t index = 0;
    while (index < ordered.size() && ordered[index] != parameter) ++index;
    if (index >= seen.size() || seen[index]) return ResultCode::StateConflict;
    seen[index] = true;
    normalized.emplace_back(id, value);
  }
  for (std::size_t index = 0; index < ordered.size(); ++index) {
    if (!seen[index]) {
      if (!valid_parameter_value(*ordered[index], ordered[index]->default_value)) return ResultCode::StateConflict;
      normalized.emplace_back(ordered[index]->id, ordered[index]->default_value);
    }
  }

  constexpr std::uint8_t magic[] = {'P', 'X', 'P', '1'};
  out.insert(out.end(), magic, magic + sizeof(magic));
  append_u32(out, static_cast<std::uint32_t>(normalized.size()));
  for (const auto& [id, value] : normalized) {
    if (id.size() > std::numeric_limits<std::uint32_t>::max() ||
        value.size() > std::numeric_limits<std::uint32_t>::max() || out.size() > 32 * 1024 ||
        id.size() > 32 * 1024 - out.size() ||
        value.size() > 32 * 1024 - out.size() - id.size() ||
        out.size() + id.size() + value.size() > 32 * 1024 - 8) {
      out.clear();
      return ResultCode::StateConflict;
    }
    append_u32(out, static_cast<std::uint32_t>(id.size()));
    out.insert(out.end(), id.begin(), id.end());
    append_u32(out, static_cast<std::uint32_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
  }
  return out.size() <= 32 * 1024 ? ResultCode::Ok : ResultCode::StateConflict;
}

ResultCode ToolCatalog::decode_parameters(std::string_view tool_id, const std::vector<std::uint8_t>& payload,
                                           std::vector<std::pair<std::string, std::string>>& out) const {
  out.clear();
  const ToolDescriptor* descriptor = find(tool_id);
  if (descriptor == nullptr || payload.size() < 8 || payload.size() > 32 * 1024 || payload[0] != 'P' ||
      payload[1] != 'X' || payload[2] != 'P' || payload[3] != '1') {
    return descriptor == nullptr ? ResultCode::UnsupportedFormat : ResultCode::StateConflict;
  }
  std::size_t offset = 4;
  std::uint32_t count = 0;
  if (!read_u32(payload, offset, count) || count > 64 || count > descriptor->parameters.size()) {
    return ResultCode::StateConflict;
  }
  std::set<std::string> seen;
  out.reserve(descriptor->parameters.size());
  for (std::uint32_t index = 0; index < count; ++index) {
    std::uint32_t id_size = 0;
    std::uint32_t value_size = 0;
    if (!read_u32(payload, offset, id_size) || id_size > payload.size() - offset) {
      out.clear();
      return ResultCode::StateConflict;
    }
    const std::size_t id_begin = offset;
    offset += id_size;
    if (!read_u32(payload, offset, value_size) || value_size > payload.size() - offset) {
      out.clear();
      return ResultCode::StateConflict;
    }
    const std::size_t value_begin = offset;
    std::string id(payload.begin() + static_cast<std::ptrdiff_t>(id_begin),
                   payload.begin() + static_cast<std::ptrdiff_t>(id_begin + id_size));
    std::string value(payload.begin() + static_cast<std::ptrdiff_t>(value_begin),
                      payload.begin() + static_cast<std::ptrdiff_t>(value_begin + value_size));
    offset += value_size;
    const ToolParameterDescriptor* parameter = find_parameter(*descriptor, id);
    if (parameter == nullptr || !seen.insert(id).second || !valid_parameter_value(*parameter, value)) {
      out.clear();
      return ResultCode::StateConflict;
    }
    out.emplace_back(std::move(id), std::move(value));
  }
  if (offset != payload.size()) {
    out.clear();
    return ResultCode::StateConflict;
  }
  // Older history records may omit optional defaults.  Normalize them through
  // the same encoder used for new tasks before exposing the retry values.
  for (const auto& parameter : descriptor->parameters) {
    if (seen.find(parameter.id) == seen.end()) {
      if (!valid_parameter_value(parameter, parameter.default_value)) {
        out.clear();
        return ResultCode::StateConflict;
      }
      out.emplace_back(parameter.id, parameter.default_value);
    }
  }
  std::vector<std::uint8_t> canonical;
  if (encode_parameters(tool_id, out, canonical) != ResultCode::Ok) {
    out.clear();
    return ResultCode::StateConflict;
  }
  return ResultCode::Ok;
}

const ToolDescriptor* ToolCatalog::find(std::string_view tool_id) const {
  for (const auto& descriptor : descriptors_) {
    if (descriptor.spec.tool_id == tool_id) return &descriptor;
  }
  return nullptr;
}

ResultCode ToolCatalog::build_task(std::string_view tool_id, std::string task_id, std::string image_id,
                                   std::uint64_t source_revision,
                                   std::vector<std::uint8_t> parameter_payload,
                                   std::vector<std::string> mask_ids,
                                   std::vector<std::string> reference_image_ids,
                                   std::string idempotency_key, GenerationTask& out) const {
  const ToolDescriptor* descriptor = find(tool_id);
  if (descriptor == nullptr) return ResultCode::UnsupportedFormat;
  if ((!descriptor->supports_masks && !mask_ids.empty()) ||
      (!descriptor->supports_references && !reference_image_ids.empty()) ||
      !valid_optional_identifier(idempotency_key)) {
    return ResultCode::UnsupportedFormat;
  }
  if (parameter_payload.size() > 32 * 1024) return ResultCode::StateConflict;

  GenerationTask task;
  task.task_id = std::move(task_id);
  task.image_id = std::move(image_id);
  task.source_revision = source_revision;
  task.tool = descriptor->spec;
  task.parameter_payload = std::move(parameter_payload);
  task.mask_ids = std::move(mask_ids);
  task.reference_image_ids = std::move(reference_image_ids);
  task.idempotency_key = idempotency_key.empty() ? task.task_id : std::move(idempotency_key);
  if (const ResultCode valid = validate_task(task); valid != ResultCode::Ok) return valid;
  out = std::move(task);
  return ResultCode::Ok;
}

}  // namespace pixcake::generate
