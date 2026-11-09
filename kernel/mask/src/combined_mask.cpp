#include "pixcake/mask/combined_mask.hpp"

namespace pixcake::mask {

std::string combined_mask_id(const std::vector<CombinedPart>& parts) {
  std::string out(kCombinedMaskPrefix);
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i != 0) out += '|';
    out += parts[i].subtract ? '-' : '+';
    out += parts[i].id;
  }
  return out;
}

bool is_combined_mask_id(std::string_view mask_id) { return mask_id.substr(0, kCombinedMaskPrefix.size()) == kCombinedMaskPrefix; }

bool parse_combined_mask_id(std::string_view mask_id, std::vector<CombinedPart>& parts) {
  if (!is_combined_mask_id(mask_id)) return false;
  std::string_view rest = mask_id.substr(kCombinedMaskPrefix.size());
  std::vector<CombinedPart> out;
  while (!rest.empty()) {
    const auto bar = rest.find('|');
    const std::string_view item = rest.substr(0, bar);
    if (item.size() < 2 || (item[0] != '+' && item[0] != '-')) return false;
    out.push_back({item[0] == '-', std::string(item.substr(1))});
    if (bar == std::string_view::npos) break;
    rest = rest.substr(bar + 1);
    if (rest.empty()) return false;  // trailing separator
  }
  if (out.empty()) return false;
  parts = std::move(out);
  return true;
}

std::string invert_mask_id(std::string_view inner) { return std::string(kInvertMaskPrefix) + std::string(inner); }

bool is_invert_mask_id(std::string_view mask_id) { return mask_id.substr(0, kInvertMaskPrefix.size()) == kInvertMaskPrefix; }

bool parse_invert_mask_id(std::string_view mask_id, std::string& inner) {
  if (!is_invert_mask_id(mask_id) || mask_id.size() == kInvertMaskPrefix.size()) return false;
  inner = std::string(mask_id.substr(kInvertMaskPrefix.size()));
  return true;
}

std::vector<std::string> leaf_mask_ids(std::string_view mask_id) {
  const auto unwrap = [](std::string id) {
    std::string inner;
    return parse_invert_mask_id(id, inner) ? inner : id;
  };
  std::vector<CombinedPart> parts;
  if (is_combined_mask_id(mask_id) && parse_combined_mask_id(mask_id, parts)) {
    std::vector<std::string> out;
    for (const auto& part : parts) out.push_back(unwrap(part.id));
    return out;
  }
  return {unwrap(std::string(mask_id))};
}

}  // namespace pixcake::mask
