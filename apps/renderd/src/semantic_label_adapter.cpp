#include "pixcake/renderd/semantic_label_adapter.hpp"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace pixcake::renderd {
namespace {

bool valid_input(const RgbaInput& input) {
  const std::uint64_t minimum_stride = static_cast<std::uint64_t>(input.width) * 4ull;
  if (input.width == 0 || input.height == 0 || minimum_stride > std::numeric_limits<std::uint32_t>::max() ||
      input.stride < minimum_stride) {
    return false;
  }
  const std::uint64_t required = static_cast<std::uint64_t>(input.stride) * input.height;
  return required <= std::numeric_limits<std::size_t>::max() &&
         input.bytes.size() >= static_cast<std::size_t>(required);
}

bool valid_raw_mask(const RawSemanticMask& mask, const RgbaInput& input) {
  if (mask.label.empty() || mask.width != input.width || mask.height != input.height || mask.stride < mask.width) {
    return false;
  }
  const std::uint64_t required = static_cast<std::uint64_t>(mask.stride) * mask.height;
  return required <= std::numeric_limits<std::size_t>::max() &&
         mask.alpha.size() >= static_cast<std::size_t>(required);
}

const SemanticLabelRule* find_rule(const std::vector<SemanticLabelRule>& rules, SemanticPart part) {
  const auto it = std::find_if(rules.begin(), rules.end(),
                               [part](const auto& rule) { return rule.part == part; });
  return it == rules.end() ? nullptr : &*it;
}

bool duplicate_strings(const std::vector<std::string>& values) {
  std::unordered_set<std::string> seen;
  for (const auto& value : values) {
    if (value.empty() || !seen.insert(value).second) return true;
  }
  return false;
}

ResultCode build_result_unchecked(
    const RgbaInput& input, const std::string& model_id, const std::string& model_version,
    const std::vector<SemanticLabelRule>& rules, const std::vector<RawSemanticMask>& raw_masks,
    SegmentationResult& out, std::string& diagnostic) {
  if (!valid_input(input)) {
    diagnostic = "semantic label adapter received invalid RGBA input";
    return ResultCode::StateConflict;
  }
  if (model_id.empty() || model_version.empty()) {
    diagnostic = "semantic label adapter is missing model identity";
    return ResultCode::StateConflict;
  }
  if (rules.size() != semantic_part_catalog().size()) {
    diagnostic = "semantic label adapter must declare every project semantic part";
    return ResultCode::StateConflict;
  }

  for (const auto& descriptor : semantic_part_catalog()) {
    const SemanticLabelRule* rule = find_rule(rules, descriptor.part);
    if (rule == nullptr) {
      diagnostic = std::string("semantic label adapter is missing rule: ") + descriptor.id;
      return ResultCode::StateConflict;
    }
    const auto duplicate = std::count_if(rules.begin(), rules.end(),
                                         [part = descriptor.part](const auto& item) { return item.part == part; });
    if (duplicate != 1) {
      diagnostic = std::string("semantic label adapter has duplicate rule: ") + descriptor.id;
      return ResultCode::StateConflict;
    }
    if (rule->status == SupportStatus::Unsupported) {
      if (!rule->source_labels.empty() || rule->diagnostic.empty()) {
        diagnostic = std::string("unsupported semantic part has an invalid mapping: ") + descriptor.id;
        return ResultCode::StateConflict;
      }
    } else if (rule->source_labels.empty() || !rule->diagnostic.empty() ||
               duplicate_strings(rule->source_labels)) {
      diagnostic = std::string("supported semantic part has an invalid source mapping: ") + descriptor.id;
      return ResultCode::StateConflict;
    }
  }

  std::unordered_map<std::string, const RawSemanticMask*> raw_by_label;
  raw_by_label.reserve(raw_masks.size());
  for (const auto& raw : raw_masks) {
    if (!valid_raw_mask(raw, input)) {
      diagnostic = "semantic label adapter received malformed raw mask";
      return ResultCode::ImageCorrupt;
    }
    if (!raw_by_label.emplace(raw.label, &raw).second) {
      diagnostic = std::string("semantic label adapter received duplicate raw label: ") + raw.label;
      return ResultCode::StateConflict;
    }
  }

  out.model_id = model_id;
  out.model_version = model_version;
  out.capabilities.reserve(rules.size());
  out.masks.reserve(rules.size());

  for (const auto& descriptor : semantic_part_catalog()) {
    const SemanticLabelRule& rule = *find_rule(rules, descriptor.part);
    out.capabilities.push_back({rule.part, rule.status, rule.source_labels, rule.diagnostic});
    if (rule.status == SupportStatus::Unsupported) continue;

    for (const auto& source_label : rule.source_labels) {
      if (raw_by_label.find(source_label) == raw_by_label.end()) {
        diagnostic = std::string("semantic label is unavailable for ") + descriptor.id + ": " + source_label;
        return ResultCode::UnsupportedFormat;
      }
    }

    SemanticMask merged;
    merged.part = rule.part;
    merged.width = input.width;
    merged.height = input.height;
    merged.stride = input.width;
    merged.alpha.assign(static_cast<std::size_t>(merged.stride) * merged.height, 0);
    for (const auto& source_label : rule.source_labels) {
      const RawSemanticMask& source = *raw_by_label.at(source_label);
      for (std::uint32_t row = 0; row < merged.height; ++row) {
        const auto* source_row = source.alpha.data() + static_cast<std::size_t>(row) * source.stride;
        auto* destination_row = merged.alpha.data() + static_cast<std::size_t>(row) * merged.stride;
        for (std::uint32_t column = 0; column < merged.width; ++column) {
          destination_row[column] = std::max(destination_row[column], source_row[column]);
        }
      }
    }
    out.masks.push_back(std::move(merged));
  }

  return validate_segmentation_result(input, out, diagnostic);
}

}  // namespace

ResultCode build_semantic_segmentation_result(
    const RgbaInput& input, const std::string& model_id, const std::string& model_version,
    const std::vector<SemanticLabelRule>& rules, const std::vector<RawSemanticMask>& raw_masks,
    SegmentationResult& out, std::string& diagnostic) {
  out = {};
  const ResultCode code = build_result_unchecked(input, model_id, model_version, rules, raw_masks, out, diagnostic);
  if (code != ResultCode::Ok) {
    // Callers must never see a partially mapped result.
    out = {};
    return code;
  }
  diagnostic.clear();
  return ResultCode::Ok;
}

}  // namespace pixcake::renderd
