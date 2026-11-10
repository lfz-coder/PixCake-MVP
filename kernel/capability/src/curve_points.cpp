// Validation ported from PhotoCraft crates/algo/src/camera_raw.rs validate_curve
// @ 885a6ddf27cc58c07faf1722e5c327cd38953657, MIT (elected from MIT OR Apache-2.0).
// Copyright (c) 2026 ArtCraft Team and the PhotoCraft contributors.
// Licence text: third_party/photocraft-LICENSES/LICENSE-MIT.
#include "pixcake/capability/curve_points.hpp"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace pixcake {
namespace {

// 一个十进制数（可带小数），不接受空白、正负号以外的前缀或十六进制。
bool parse_coordinate(std::string_view text, float& out) {
  if (text.empty() || text.size() > 32) return false;
  for (const char c : text) {
    if (!((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+' || c == 'e' || c == 'E')) return false;
  }
  const std::string copy(text);
  char* end = nullptr;
  errno = 0;
  const double value = std::strtod(copy.c_str(), &end);
  if (errno != 0 || end != copy.c_str() + copy.size()) return false;
  out = static_cast<float>(value);
  return true;
}

// PhotoCraft validate_curve（空曲线在这里不出现：不写参数就是线性）。
bool valid_curve(const std::vector<std::array<float, 2>>& points) {
  if (points.size() < 2 || points.size() > kMaxCurvePoints) return false;
  for (const auto& p : points) {
    for (const float v : p) {
      if (!std::isfinite(v) || v < 0.0F || v > 255.0F) return false;
    }
  }
  for (std::size_t i = 1; i < points.size(); ++i) {
    if (points[i][0] - points[i - 1][0] < 1.0F) return false;
  }
  return true;
}

}  // namespace

bool is_curve_points_parameter(std::string_view parameter_id) {
  return parameter_id == "curve_points_rgb" || parameter_id == "curve_points_red" || parameter_id == "curve_points_green" ||
         parameter_id == "curve_points_blue";
}

bool decode_curve_points(std::string_view text, std::vector<std::array<float, 2>>& out) {
  out.clear();
  while (true) {
    const std::size_t semicolon = text.find(';');
    const std::string_view point = text.substr(0, semicolon);
    const std::size_t comma = point.find(',');
    if (comma == std::string_view::npos) return false;
    std::array<float, 2> p{};
    if (!parse_coordinate(point.substr(0, comma), p[0]) || !parse_coordinate(point.substr(comma + 1), p[1])) return false;
    out.push_back(p);
    if (out.size() > kMaxCurvePoints) return false;
    if (semicolon == std::string_view::npos) break;
    text.remove_prefix(semicolon + 1);
  }
  return valid_curve(out);
}

std::string encode_curve_points(const std::vector<std::array<float, 2>>& points) {
  std::string out;
  for (const auto& p : points) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%s%.9g,%.9g", out.empty() ? "" : ";", static_cast<double>(p[0]), static_cast<double>(p[1]));
    out += buffer;
  }
  return out;
}

}  // namespace pixcake
