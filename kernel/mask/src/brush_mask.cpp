#include "pixcake/mask/brush_mask.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace pixcake::mask {
namespace {

bool read_int(std::string_view text, std::size_t& at, long& out) {
  std::size_t end = at;
  if (end < text.size() && text[end] == '-') ++end;
  const std::size_t digits = end;
  while (end < text.size() && text[end] >= '0' && text[end] <= '9') ++end;
  if (end == digits || end - at > 8) return false;
  out = std::strtol(std::string(text.substr(at, end - at)).c_str(), nullptr, 10);
  at = end;
  return true;
}

bool in_range(long value, long low, long high) { return value >= low && value <= high; }

}  // namespace

std::string encode_brush_strokes(const std::vector<BrushStroke>& strokes) {
  std::string out;
  for (std::size_t s = 0; s < strokes.size(); ++s) {
    const auto& stroke = strokes[s];
    if (s != 0) out += '/';
    out += stroke.erase ? 'e' : 'a';
    out += std::to_string(stroke.size) + ',' + std::to_string(stroke.feather) + ',' + std::to_string(stroke.flow) + ',' +
           std::to_string(stroke.density) + ':';
    int previous_x = 0;
    int previous_y = 0;
    for (std::size_t p = 0; p < stroke.points.size(); ++p) {
      if (p != 0) out += ',';
      const int x = stroke.points[p].first;
      const int y = stroke.points[p].second;
      out += std::to_string(p == 0 ? x : x - previous_x) + ',' + std::to_string(p == 0 ? y : y - previous_y);
      previous_x = x;
      previous_y = y;
    }
  }
  return out;
}

bool decode_brush_strokes(std::string_view text, std::vector<BrushStroke>& strokes) {
  if (text.size() > kMaxBrushTextChars) return false;
  std::vector<BrushStroke> out;
  std::size_t at = 0;
  while (at < text.size()) {
    if (out.size() >= kMaxBrushStrokes) return false;
    BrushStroke stroke;
    if (text[at] != 'a' && text[at] != 'e') return false;
    stroke.erase = text[at] == 'e';
    ++at;
    long header[4];
    for (int i = 0; i < 4; ++i) {
      if (!read_int(text, at, header[i])) return false;
      const char separator = i < 3 ? ',' : ':';
      if (at >= text.size() || text[at] != separator) return false;
      ++at;
    }
    if (!in_range(header[0], 1, 10000) || !in_range(header[1], 0, 100) || !in_range(header[2], 0, 100) ||
        !in_range(header[3], 0, 100)) {
      return false;
    }
    stroke.size = static_cast<int>(header[0]);
    stroke.feather = static_cast<int>(header[1]);
    stroke.flow = static_cast<int>(header[2]);
    stroke.density = static_cast<int>(header[3]);
    long x = 0;
    long y = 0;
    for (;;) {
      long a = 0;
      long b = 0;
      if (!read_int(text, at, a) || at >= text.size() || text[at] != ',') return false;
      ++at;
      if (!read_int(text, at, b)) return false;
      if (stroke.points.empty()) {
        x = a;
        y = b;
      } else {
        x += a;
        y += b;
      }
      if (!in_range(x, 0, 10000) || !in_range(y, 0, 10000)) return false;
      stroke.points.emplace_back(static_cast<int>(x), static_cast<int>(y));
      if (stroke.points.size() > kMaxBrushPoints) return false;
      if (at < text.size() && text[at] == ',') {
        ++at;
        continue;
      }
      break;
    }
    out.push_back(std::move(stroke));
    if (at < text.size()) {
      if (text[at] != '/') return false;
      ++at;
      if (at >= text.size()) return false;  // 末尾多一个分隔符
    }
  }
  strokes = std::move(out);
  return true;
}

std::string brush_mask_id(std::string_view stroke_text) { return std::string(kBrushMaskPrefix) + std::string(stroke_text); }

bool is_brush_mask_id(std::string_view mask_id) { return mask_id.substr(0, kBrushMaskPrefix.size()) == kBrushMaskPrefix; }

bool parse_brush_mask_id(std::string_view mask_id, std::vector<BrushStroke>& strokes) {
  if (!is_brush_mask_id(mask_id)) return false;
  const auto text = mask_id.substr(kBrushMaskPrefix.size());
  std::vector<BrushStroke> parsed;
  if (text.empty() || !decode_brush_strokes(text, parsed)) return false;
  strokes = std::move(parsed);
  return true;
}

double brush_outer_radius(double diameter, int feather) { return diameter / 2.0 * (1.0 + 2.45 * feather / 100.0); }

double brush_profile(double distance, double diameter, int feather) {
  const double f = feather / 100.0;
  const double inner = diameter / 2.0 * (1.0 - 0.25 * f);
  const double outer = brush_outer_radius(diameter, feather);
  if (distance <= inner) return 1.0;
  if (distance >= outer) return 0.0;
  const double t = (distance - inner) / (outer - inner);
  return 1.0 - t * t * (3.0 - 2.0 * t);
}

}  // namespace pixcake::mask
