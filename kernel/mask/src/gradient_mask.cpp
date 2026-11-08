#include "pixcake/mask/gradient_mask.hpp"

#include <cstdlib>
#include <vector>

namespace pixcake::mask {

std::string gradient_mask_id(const GradientMask& g) {
  return std::string(kGradientMaskPrefix) + (g.radial ? "radial" : "linear") + ":" + std::to_string(g.x) + ":" +
         std::to_string(g.y) + ":" + std::to_string(g.angle) + ":" + std::to_string(g.size) + ":" +
         std::to_string(g.feather);
}

bool is_gradient_mask_id(std::string_view mask_id) { return mask_id.substr(0, kGradientMaskPrefix.size()) == kGradientMaskPrefix; }

bool parse_gradient_mask_id(std::string_view mask_id, GradientMask& gradient) {
  if (!is_gradient_mask_id(mask_id)) return false;
  std::vector<std::string> fields;
  std::string rest(mask_id.substr(kGradientMaskPrefix.size()));
  std::size_t start = 0;
  for (;;) {
    const auto colon = rest.find(':', start);
    fields.push_back(rest.substr(start, colon == std::string::npos ? std::string::npos : colon - start));
    if (colon == std::string::npos) break;
    start = colon + 1;
  }
  if (fields.size() != 6 || (fields[0] != "linear" && fields[0] != "radial")) return false;
  int numbers[5];
  for (int i = 0; i < 5; ++i) {
    const std::string& text = fields[static_cast<std::size_t>(i) + 1];
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (text.empty() || end != text.c_str() + text.size()) return false;
    numbers[i] = static_cast<int>(value);
  }
  if (numbers[0] < 0 || numbers[0] > 100 || numbers[1] < 0 || numbers[1] > 100 || numbers[2] < -180 ||
      numbers[2] > 180 || numbers[3] < 0 || numbers[3] > 100 || numbers[4] < 0 || numbers[4] > 100) {
    return false;
  }
  gradient = GradientMask{fields[0] == "radial", numbers[0], numbers[1], numbers[2], numbers[3], numbers[4]};
  return true;
}

}  // namespace pixcake::mask
