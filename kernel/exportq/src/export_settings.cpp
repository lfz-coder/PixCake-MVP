#include "pixcake/exportq/export_settings.hpp"

#include "pixcake/capability/capability_registry.hpp"

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
    case ExportFormat::Original:
      break;
  }
  return "original";
}

}  // namespace

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
    } else {
      return ResultCode::StateConflict;
    }
    if (end == std::string::npos) break;
    begin = end + 1;
  }
  if (parsed.quality.has_value() && parsed.size_limit_kb.has_value()) return ResultCode::StateConflict;
  out = parsed;
  return ResultCode::Ok;
}

std::string encode_export_settings(const ExportSettings& settings) {
  std::string text = std::string("format=") + format_name(settings.format);
  if (settings.quality) text += ";quality=" + std::to_string(*settings.quality);
  if (settings.size_limit_kb) text += ";size_limit_kb=" + std::to_string(*settings.size_limit_kb);
  if (settings.convert_to_srgb) text += ";srgb=1";
  if (settings.print_copies > 0) text += ";copies=" + std::to_string(settings.print_copies);
  return text;
}

}  // namespace pixcake
