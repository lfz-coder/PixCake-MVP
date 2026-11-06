#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pixcake {

// 能力片段正文的唯一编码（契约 C-30：消息体和磁盘文档用同一套编码）。
// 正文是一串记录：u32 类型、u32 长度、负载，均为小端。类型 1 是参数记录：
// u32 标识长度、标识、u32 值长度、显示值。其他类型是能力自己的记录或未来字段，读写时原样保留。
inline constexpr std::uint32_t kFragmentParameterRecord = 1;

struct FragmentRecord {
  std::uint32_t type = 0;
  std::vector<std::uint8_t> payload;
};

bool decode_fragment_records(const std::vector<std::uint8_t>& inner, std::vector<FragmentRecord>& out);
std::vector<std::uint8_t> encode_fragment_records(const std::vector<FragmentRecord>& records);

bool decode_parameter_record(const std::vector<std::uint8_t>& payload, std::string& parameter_id,
                             std::string& display_value);
std::vector<std::uint8_t> encode_parameter_record(const std::string& parameter_id, const std::string& display_value);

}  // namespace pixcake
