#include "pixcake/exportq/export_settings.hpp"

#include "pixcake/capability/capability_registry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <set>

namespace pixcake {
namespace {

bool parse_unsigned(const std::string& text, std::uint64_t& out) {
  if (text.empty() || text.size() > 19) return false;
  std::uint64_t value = 0;
  for (const char digit : text) {
    if (digit < '0' || digit > '9') return false;
    value = value * 10 + static_cast<std::uint64_t>(digit - '0');
  }
  out = value;
  return true;
}

const char* format_name(ExportFormat format) {
  switch (format) {
    case ExportFormat::Jpg:
      return "jpg";
    case ExportFormat::Tiff:
      return "tiff";
    case ExportFormat::Tiff16:
      return "tiff16";
    case ExportFormat::Original:
      break;
  }
  return "original";
}

const char* const kShapeKeys[3] = {"wm.l", "wm.p", "wm.s"};

bool parse_int(const std::string& text, int low, int high, int& out) {
  if (text.empty() || text.size() > 4) return false;
  std::size_t at = 0;
  const bool negative = text[0] == '-';
  if (negative) at = 1;
  if (at == text.size()) return false;
  int value = 0;
  for (; at < text.size(); ++at) {
    if (text[at] < '0' || text[at] > '9') return false;
    value = value * 10 + (text[at] - '0');
  }
  if (negative) value = -value;
  if (value < low || value > high) return false;
  out = value;
  return true;
}

bool parse_placement(const std::string& text, WatermarkPlacement& out) {
  int values[7] = {};
  const int lows[7] = {0, 0, 0, 0, -100, -100, 0};
  const int highs[7] = {100, 1, 100, 8, 100, 100, 3};
  std::size_t begin = 0;
  for (int i = 0; i < 7; ++i) {
    const std::size_t end = text.find(',', begin);
    if ((i < 6) == (end == std::string::npos)) return false;
    const std::string part = text.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
    if (!parse_int(part, lows[i], highs[i], values[i])) return false;
    begin = end + 1;
  }
  out = {values[0], values[1] == 1, values[2], values[3], values[4], values[5], values[6]};
  return true;
}

std::string encode_placement(const WatermarkPlacement& p) {
  return std::to_string(p.size) + "," + (p.fit ? "1" : "0") + "," + std::to_string(p.opacity) + "," +
         std::to_string(p.anchor) + "," + std::to_string(p.x) + "," + std::to_string(p.y) + "," +
         std::to_string(p.quarter_turns);
}

constexpr int kLayoutLows[11] = {0, 20, 20, 72, 1, 1, 0, 0, 0, 0, 0};
constexpr int kLayoutHighs[11] = {1, 2000, 2000, 1200, 20, 20, 200, 400, 400, 400, 400};

bool parse_layout(const std::string& text, ExportLayout& out) {
  int values[11] = {};
  std::size_t begin = 0;
  for (int i = 0; i < 11; ++i) {
    const std::size_t end = text.find(',', begin);
    if ((i < 10) == (end == std::string::npos)) return false;
    const std::string part = text.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
    if (!parse_int(part, kLayoutLows[i], kLayoutHighs[i], values[i])) return false;
    begin = end + 1;
  }
  out = {values[0] == 1, values[1], values[2], values[3], values[4], values[5], values[6], values[7], values[8],
         values[9], values[10]};
  return layout_page_width(out) <= 20000 && layout_page_height(out) <= 20000;
}

std::string encode_layout(const ExportLayout& l) {
  return std::string(l.id_photo ? "1" : "0") + "," + std::to_string(l.paper_width_mm) + "," +
         std::to_string(l.paper_height_mm) + "," + std::to_string(l.dpi) + "," + std::to_string(l.rows) + "," +
         std::to_string(l.cols) + "," + std::to_string(l.gap_px) + "," + std::to_string(l.margin_top_mm) + "," +
         std::to_string(l.margin_bottom_mm) + "," + std::to_string(l.margin_left_mm) + "," +
         std::to_string(l.margin_right_mm);
}

}  // namespace

std::uint32_t layout_page_width(const ExportLayout& layout) {
  return static_cast<std::uint32_t>(std::llround(layout.paper_width_mm * layout.dpi / 25.4));
}
std::uint32_t layout_page_height(const ExportLayout& layout) {
  return static_cast<std::uint32_t>(std::llround(layout.paper_height_mm * layout.dpi / 25.4));
}

bool layout_geometry(const ExportLayout& layout, std::uint32_t photo_width, std::uint32_t photo_height,
                     LayoutGeometry& out) {
  const std::uint32_t width = layout_page_width(layout);
  const std::uint32_t height = layout_page_height(layout);
  if (width == 0 || height == 0 || width > 20000 || height > 20000 || photo_width == 0 || photo_height == 0) {
    return false;
  }
  out = LayoutGeometry{};
  out.page_width = width;
  out.page_height = height;
  const auto mm = [&](int value) { return static_cast<std::int64_t>(std::llround(value * layout.dpi / 25.4)); };
  out.window_left = std::min<std::int64_t>(mm(layout.margin_left_mm), width);
  out.window_top = std::min<std::int64_t>(mm(layout.margin_top_mm), height);
  out.window_right = std::max<std::int64_t>(static_cast<std::int64_t>(width) - mm(layout.margin_right_mm), 0);
  out.window_bottom = std::max<std::int64_t>(static_cast<std::int64_t>(height) - mm(layout.margin_bottom_mm), 0);
  if (!layout.id_photo) {
    // 整张照片缩放到正好放进相纸，居中。
    const double scale = std::min(static_cast<double>(width) / photo_width, static_cast<double>(height) / photo_height);
    const auto dw = std::min<std::int64_t>(width, std::max<std::int64_t>(1, std::llround(photo_width * scale)));
    const auto dh = std::min<std::int64_t>(height, std::max<std::int64_t>(1, std::llround(photo_height * scale)));
    out.scaled = true;
    out.photos.push_back({(static_cast<std::int64_t>(width) - dw) / 2, (static_cast<std::int64_t>(height) - dh) / 2, dw, dh});
    return true;
  }
  // 证件照排版：rows×cols 张原始大小的照片（格间 gap_px）拼成一整块画布，画布中心对准相纸中心，
  // 相纸是这块画布上的窗口：画布大了裁、小了留白（2026-11-08 实测归纳）。
  const std::int64_t iw = photo_width, ih = photo_height, gap = layout.gap_px;
  const std::int64_t canvas_w = layout.cols * iw + (layout.cols - 1) * gap;
  const std::int64_t canvas_h = layout.rows * ih + (layout.rows - 1) * gap;
  const auto floor_half = [](std::int64_t v) { return v >= 0 ? v / 2 : -((-v + 1) / 2); };
  const std::int64_t origin_x = floor_half(static_cast<std::int64_t>(width) - canvas_w);
  const std::int64_t origin_y = floor_half(static_cast<std::int64_t>(height) - canvas_h);
  for (int row = 0; row < layout.rows; ++row) {
    for (int col = 0; col < layout.cols; ++col) {
      out.photos.push_back({origin_x + col * (iw + gap), origin_y + row * (ih + gap), iw, ih});
    }
  }
  std::set<std::int64_t> rows;
  for (const LayoutRect& at : out.photos) {
    const std::int64_t top = std::max({at.top, out.window_top, std::int64_t{0}});
    const std::int64_t bottom = std::min({at.top + at.height, out.window_bottom, static_cast<std::int64_t>(height)});
    const std::int64_t left = std::max({at.left, out.window_left, std::int64_t{0}});
    const std::int64_t right = std::min({at.left + at.width, out.window_right, static_cast<std::int64_t>(width)});
    if (top >= bottom || left >= right) continue;  // 看不见的照片没有线
    if (out.cut_skip_left == out.cut_skip_right) {
      out.cut_skip_left = left;
      out.cut_skip_right = right;
    } else {
      out.cut_skip_left = std::min(out.cut_skip_left, left);
      out.cut_skip_right = std::max(out.cut_skip_right, right);
    }
    for (const std::int64_t y : {top - 1, bottom + 1}) {
      if (y >= 0 && y < static_cast<std::int64_t>(height)) rows.insert(y);
    }
  }
  out.cut_rows.assign(rows.begin(), rows.end());
  return true;
}

WatermarkShape watermark_shape(std::uint32_t width, std::uint32_t height) {
  // 方图：长宽相差不超过 1%（test 版的判定没有测，取一个容得下取整误差的值）。
  const std::uint64_t longer = std::max(width, height);
  const std::uint64_t shorter = std::min(width, height);
  if ((longer - shorter) * 100 <= longer) return WatermarkShape::Square;
  return width > height ? WatermarkShape::Landscape : WatermarkShape::Portrait;
}

bool valid_watermark_material(const std::string& name) {
  // <16 hex>-<bytes>.<png|jpg>
  if (name.size() < 16 + 1 + 1 + 4 || name.size() > 64) return false;
  for (std::size_t i = 0; i < 16; ++i) {
    const char c = name[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  if (name[16] != '-') return false;
  const std::string extension = name.substr(name.size() - 4);
  if (extension != ".png" && extension != ".jpg") return false;
  const std::string bytes = name.substr(17, name.size() - 4 - 17);
  if (bytes.empty() || bytes.size() > 9) return false;
  for (const char c : bytes) {
    if (c < '0' || c > '9') return false;
  }
  return bytes[0] != '0';
}

namespace {
std::int64_t percent_of(std::uint32_t length, int percent) {
  return static_cast<std::int64_t>(std::llround(static_cast<double>(length) * percent / 100.0));
}
}  // namespace

WatermarkRect place_watermark(std::uint32_t material_width, std::uint32_t material_height,
                              const WatermarkPlacement& placement, std::uint32_t image_width,
                              std::uint32_t image_height) {
  WatermarkRect rect;
  if (material_width == 0 || material_height == 0 || image_width == 0 || image_height == 0) return rect;
  const bool swap = placement.quarter_turns % 2 == 1;
  const double turned_width = swap ? material_height : material_width;
  const double turned_height = swap ? material_width : material_height;
  double scale = 0.0;
  if (placement.fit) {
    scale = std::min(image_width / turned_width, image_height / turned_height);
  } else {
    const double short_edge = std::min(material_width, material_height);
    scale = std::max(image_width, image_height) * (placement.size / 100.0) / short_edge;
  }
  const auto width = static_cast<std::uint32_t>(std::llround(turned_width * scale));
  const auto height = static_cast<std::uint32_t>(std::llround(turned_height * scale));
  if (width == 0 || height == 0) return rect;
  rect.width = std::min(width, placement.fit ? image_width : width);
  rect.height = std::min(height, placement.fit ? image_height : height);

  const int column = placement.anchor % 3;
  const int row = placement.anchor / 3;
  const bool full_width = placement.fit && rect.width >= image_width;
  const bool full_height = placement.fit && rect.height >= image_height;
  const std::int64_t dx = full_width ? 0 : percent_of(image_width, placement.x);
  const std::int64_t dy = full_height ? 0 : percent_of(image_height, placement.y);
  const std::int64_t free_x = static_cast<std::int64_t>(image_width) - rect.width;
  const std::int64_t free_y = static_cast<std::int64_t>(image_height) - rect.height;
  rect.left = column == 0 ? dx : column == 2 ? free_x - dx : free_x / 2 + dx;
  rect.top = row == 0 ? dy : row == 2 ? free_y - dy : free_y / 2 + dy;
  return rect;
}

ResultCode parse_export_settings(const std::string& text, ExportSettings& out) {
  ExportSettings parsed;
  std::set<std::string> seen;
  std::size_t begin = 0;
  while (!text.empty() && begin <= text.size()) {
    const std::size_t end = text.find(';', begin);
    const std::string item = text.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
    const std::size_t equals = item.find('=');
    if (equals == std::string::npos || equals == 0) return ResultCode::StateConflict;
    const std::string key = item.substr(0, equals);
    const std::string value = item.substr(equals + 1);
    if (!seen.insert(key).second) return ResultCode::StateConflict;
    std::uint64_t number = 0;
    if (key == "format") {
      if (value == "original") {
        parsed.format = ExportFormat::Original;
      } else if (value == "jpg") {
        parsed.format = ExportFormat::Jpg;
      } else if (value == "tiff") {
        parsed.format = ExportFormat::Tiff;
      } else if (value == "tiff16") {
        parsed.format = ExportFormat::Tiff16;
      } else {
        return ResultCode::StateConflict;
      }
    } else if (key == "quality") {
      if (!parse_unsigned(value, number) || !validate_parameter_display_value("export", "quality", value)) {
        return ResultCode::StateConflict;
      }
      parsed.quality = static_cast<int>(number);
    } else if (key == "size_limit_kb") {
      if (!parse_unsigned(value, number) || number == 0) return ResultCode::StateConflict;
      parsed.size_limit_kb = number;
    } else if (key == "srgb") {
      if (value != "0" && value != "1") return ResultCode::StateConflict;
      parsed.convert_to_srgb = value == "1";
    } else if (key == "copies") {
      if (!parse_unsigned(value, number) || number > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
        return ResultCode::StateConflict;
      }
      parsed.print_copies = static_cast<int>(number);
    } else if (key == "wm") {
      if (!valid_watermark_material(value)) return ResultCode::StateConflict;
      if (!parsed.watermark) parsed.watermark.emplace();
      parsed.watermark->material = value;
    } else if (key == kShapeKeys[0] || key == kShapeKeys[1] || key == kShapeKeys[2]) {
      const int shape = key == kShapeKeys[0] ? 0 : key == kShapeKeys[1] ? 1 : 2;
      if (!parsed.watermark) parsed.watermark.emplace();
      if (!parse_placement(value, parsed.watermark->placements[shape])) return ResultCode::StateConflict;
    } else if (key == "layout") {
      ExportLayout layout;
      if (!parse_layout(value, layout)) return ResultCode::StateConflict;
      parsed.layout = layout;
    } else {
      return ResultCode::StateConflict;
    }
    if (end == std::string::npos) break;
    begin = end + 1;
  }
  if (parsed.quality.has_value() && parsed.size_limit_kb.has_value()) return ResultCode::StateConflict;
  // A watermark names its material and all three placements.
  if (parsed.watermark && (!seen.count("wm") || !seen.count(kShapeKeys[0]) || !seen.count(kShapeKeys[1]) ||
                           !seen.count(kShapeKeys[2]))) {
    return ResultCode::StateConflict;
  }
  out = parsed;
  return ResultCode::Ok;
}

std::string encode_export_settings(const ExportSettings& settings) {
  std::string text = std::string("format=") + format_name(settings.format);
  if (settings.quality) text += ";quality=" + std::to_string(*settings.quality);
  if (settings.size_limit_kb) text += ";size_limit_kb=" + std::to_string(*settings.size_limit_kb);
  if (settings.convert_to_srgb) text += ";srgb=1";
  if (settings.print_copies > 0) text += ";copies=" + std::to_string(settings.print_copies);
  if (settings.watermark) {
    text += ";wm=" + settings.watermark->material;
    for (int shape = 0; shape < 3; ++shape) {
      text += std::string(";") + kShapeKeys[shape] + "=" + encode_placement(settings.watermark->placements[shape]);
    }
  }
  if (settings.layout) text += ";layout=" + encode_layout(*settings.layout);
  return text;
}

}  // namespace pixcake
