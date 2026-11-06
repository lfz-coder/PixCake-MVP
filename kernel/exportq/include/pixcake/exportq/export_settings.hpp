#pragma once

#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace pixcake {

enum class ExportFormat { Original, Jpg, Tiff };

// 导出设置（需求第 19 节）。品质与文件大小限制二选一：最多给一个。
struct ExportSettings {
  ExportFormat format = ExportFormat::Original;
  std::optional<int> quality;                  // 0 到 14 的整数，范围来自参数模式 export/quality
  std::optional<std::uint64_t> size_limit_kb;  // 目标文件大小，正整数
  bool convert_to_srgb = false;
  int print_copies = 0;                        // 0 表示不打印
};

// 文本格式：以 ';' 分隔的 key=value，键为 format、quality、size_limit_kb、srgb、copies；
// 空字符串表示全部默认。未声明的键、重复的键、空值或越界值返回状态冲突。
ResultCode parse_export_settings(const std::string& text, ExportSettings& out);

// 规范编码：format 在前，其后依次是 quality 或 size_limit_kb、srgb（仅为 1 时）、copies（仅大于 0 时）。
std::string encode_export_settings(const ExportSettings& settings);

}  // namespace pixcake
