#include "pixcake/mask/range_mask.hpp"

#include <cstdlib>
#include <vector>

namespace pixcake::mask {
namespace {

bool split_numbers(std::string_view text, std::size_t count, long* out) {
  std::size_t start = 0;
  for (std::size_t i = 0; i < count; ++i) {
    const auto colon = text.find(':', start);
    const bool last = i + 1 == count;
    if (last != (colon == std::string_view::npos)) return false;
    const std::string field(text.substr(start, last ? std::string_view::npos : colon - start));
    char* end = nullptr;
    out[i] = std::strtol(field.c_str(), &end, 10);
    if (field.empty() || end != field.c_str() + field.size()) return false;
    start = colon + 1;
  }
  return true;
}

}  // namespace

std::string color_range_mask_id(const ColorRangeMask& m) {
  return std::string(kColorRangePrefix) + std::to_string(m.r) + ":" + std::to_string(m.g) + ":" + std::to_string(m.b) +
         ":" + std::to_string(m.adjust);
}

std::string luminance_range_mask_id(const LuminanceRangeMask& m) {
  return std::string(kLuminanceRangePrefix) + std::to_string(m.a) + ":" + std::to_string(m.b) + ":" +
         std::to_string(m.c) + ":" + std::to_string(m.d);
}

bool is_color_range_mask_id(std::string_view id) { return id.substr(0, kColorRangePrefix.size()) == kColorRangePrefix; }
bool is_luminance_range_mask_id(std::string_view id) {
  return id.substr(0, kLuminanceRangePrefix.size()) == kLuminanceRangePrefix;
}

bool parse_color_range_mask_id(std::string_view id, ColorRangeMask& range) {
  if (!is_color_range_mask_id(id)) return false;
  long v[4];
  if (!split_numbers(id.substr(kColorRangePrefix.size()), 4, v)) return false;
  for (int i = 0; i < 3; ++i) {
    if (v[i] < 0 || v[i] > 255) return false;
  }
  if (v[3] < 1 || v[3] > 100) return false;
  range = ColorRangeMask{static_cast<int>(v[0]), static_cast<int>(v[1]), static_cast<int>(v[2]), static_cast<int>(v[3])};
  return true;
}

bool parse_luminance_range_mask_id(std::string_view id, LuminanceRangeMask& range) {
  if (!is_luminance_range_mask_id(id)) return false;
  long v[4];
  if (!split_numbers(id.substr(kLuminanceRangePrefix.size()), 4, v)) return false;
  if (v[0] < 0 || v[3] > 100 || v[0] > v[1] || v[1] > v[2] || v[2] > v[3]) return false;
  range = LuminanceRangeMask{static_cast<int>(v[0]), static_cast<int>(v[1]), static_cast<int>(v[2]), static_cast<int>(v[3])};
  return true;
}

double color_range_reach(int adjust) { return 18.0 + 0.4 * adjust; }

}  // namespace pixcake::mask
