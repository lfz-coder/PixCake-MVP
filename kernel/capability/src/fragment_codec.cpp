#include "pixcake/capability/fragment_codec.hpp"

#include <cstddef>
#include <utility>

namespace pixcake {
namespace {

void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xffu));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
  out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffu));
  out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffu));
}

bool read_u32(const std::vector<std::uint8_t>& in, std::size_t offset, std::uint32_t& out) {
  if (offset > in.size() || in.size() - offset < 4) {
    return false;
  }
  out = static_cast<std::uint32_t>(in[offset]) | (static_cast<std::uint32_t>(in[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(in[offset + 2]) << 16) | (static_cast<std::uint32_t>(in[offset + 3]) << 24);
  return true;
}

}  // namespace

bool decode_fragment_records(const std::vector<std::uint8_t>& in, std::vector<FragmentRecord>& out) {
  std::size_t offset = 0;
  while (offset < in.size()) {
    std::uint32_t type = 0;
    std::uint32_t size = 0;
    if (!read_u32(in, offset, type) || !read_u32(in, offset + 4, size)) {
      return false;
    }
    if (size > in.size() - (offset + 8)) {
      return false;
    }
    FragmentRecord record;
    record.type = type;
    record.payload.assign(in.begin() + static_cast<std::ptrdiff_t>(offset + 8),
                          in.begin() + static_cast<std::ptrdiff_t>(offset + 8 + size));
    out.push_back(std::move(record));
    offset += 8 + size;
  }
  return true;
}

std::vector<std::uint8_t> encode_fragment_records(const std::vector<FragmentRecord>& records) {
  std::vector<std::uint8_t> out;
  for (const auto& record : records) {
    append_u32(out, record.type);
    append_u32(out, static_cast<std::uint32_t>(record.payload.size()));
    out.insert(out.end(), record.payload.begin(), record.payload.end());
  }
  return out;
}

bool decode_parameter_record(const std::vector<std::uint8_t>& payload, std::string& id, std::string& value) {
  std::uint32_t id_len = 0;
  if (!read_u32(payload, 0, id_len) || id_len > payload.size() - 4) {
    return false;
  }
  const std::size_t value_len_at = 4 + static_cast<std::size_t>(id_len);
  if (value_len_at > payload.size() || payload.size() - value_len_at < 4) {
    return false;
  }
  std::uint32_t value_len = 0;
  if (!read_u32(payload, value_len_at, value_len)) {
    return false;
  }
  if (static_cast<std::size_t>(value_len) != payload.size() - (value_len_at + 4)) {
    return false;
  }
  id.assign(payload.begin() + 4, payload.begin() + static_cast<std::ptrdiff_t>(4 + id_len));
  value.assign(payload.begin() + static_cast<std::ptrdiff_t>(value_len_at + 4), payload.end());
  return true;
}

std::vector<std::uint8_t> encode_parameter_record(const std::string& id, const std::string& value) {
  std::vector<std::uint8_t> out;
  append_u32(out, static_cast<std::uint32_t>(id.size()));
  out.insert(out.end(), id.begin(), id.end());
  append_u32(out, static_cast<std::uint32_t>(value.size()));
  out.insert(out.end(), value.begin(), value.end());
  return out;
}

}  // namespace pixcake
