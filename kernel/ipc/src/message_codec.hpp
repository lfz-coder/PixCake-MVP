#pragma once

#include "pixcake/support/result_code.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace pixcake::ipc::detail {

// 进程间消息的编码（契约 C-30，架构第 354 行）：一串文档记录（kernel/capability/fragment_codec），
// 第一条是信封（kEnvelopeRecord：u32 消息种类、u32 协议版本），其后每个字段一条记录，类型是字段号。
// 整数小端定长，字符串是原始字节。读方忽略不认识的字段号；协议版本比本进程新时返回 CapabilityTooNew。
class MessageWriter {
 public:
  MessageWriter(std::uint32_t kind, std::uint32_t version);
  void u32(std::uint32_t field, std::uint32_t value);
  void u64(std::uint32_t field, std::uint64_t value);
  // 空串或超过 limit、含 NUL 时返回 false；allow_empty 为真时允许空串。
  bool text(std::uint32_t field, const std::string& value, std::size_t limit, bool allow_empty = false);
  // Raw bytes (may contain NUL); false when larger than limit.
  bool blob(std::uint32_t field, const std::vector<std::uint8_t>& value, std::size_t limit);
  std::vector<std::uint8_t> bytes() const;

 private:
  std::uint32_t kind_;
  std::uint32_t version_;
  std::vector<std::pair<std::uint32_t, std::vector<std::uint8_t>>> fields_;
};

// Message kind from the envelope, without decoding the fields.  False when the
// bytes do not start with a valid envelope.
bool peek_kind(const std::vector<std::uint8_t>& bytes, std::uint32_t& kind, std::uint32_t& version);

class MessageReader {
 public:
  ResultCode open(const std::vector<std::uint8_t>& bytes, std::uint32_t kind, std::uint32_t version,
                  std::size_t max_bytes);
  bool u32(std::uint32_t field, std::uint32_t& value) const;
  bool u64(std::uint32_t field, std::uint64_t& value) const;
  bool text(std::uint32_t field, std::string& value, std::size_t limit, bool allow_empty = false) const;
  bool blob(std::uint32_t field, std::vector<std::uint8_t>& value, std::size_t limit) const;
  bool has(std::uint32_t field) const { return fields_.count(field) != 0; }

 private:
  std::map<std::uint32_t, std::vector<std::uint8_t>> fields_;
};

}  // namespace pixcake::ipc::detail
