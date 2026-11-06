#include "message_codec.hpp"

#include "pixcake/capability/fragment_codec.hpp"
#include "pixcake/ipc/process_messages.hpp"

namespace pixcake::ipc::detail {
namespace {

std::vector<std::uint8_t> le(std::uint64_t value, int width) {
  std::vector<std::uint8_t> out;
  for (int index = 0; index < width; ++index) out.push_back(static_cast<std::uint8_t>(value >> (index * 8)));
  return out;
}

std::uint64_t read_le(const std::vector<std::uint8_t>& bytes, std::size_t offset, int width) {
  std::uint64_t value = 0;
  for (int index = 0; index < width; ++index) {
    value |= static_cast<std::uint64_t>(bytes[offset + static_cast<std::size_t>(index)]) << (index * 8);
  }
  return value;
}

bool acceptable_text(const std::string& value, std::size_t limit, bool allow_empty) {
  if (value.empty()) return allow_empty;
  return value.size() <= limit && value.find('\0') == std::string::npos;
}

}  // namespace

MessageWriter::MessageWriter(std::uint32_t kind, std::uint32_t version) : kind_(kind), version_(version) {}

void MessageWriter::u32(std::uint32_t field, std::uint32_t value) { fields_.emplace_back(field, le(value, 4)); }

void MessageWriter::u64(std::uint32_t field, std::uint64_t value) { fields_.emplace_back(field, le(value, 8)); }

bool MessageWriter::text(std::uint32_t field, const std::string& value, std::size_t limit, bool allow_empty) {
  if (!acceptable_text(value, limit, allow_empty)) return false;
  fields_.emplace_back(field, std::vector<std::uint8_t>(value.begin(), value.end()));
  return true;
}

std::vector<std::uint8_t> MessageWriter::bytes() const {
  std::vector<FragmentRecord> records;
  std::vector<std::uint8_t> envelope = le(kind_, 4);
  const auto version = le(version_, 4);
  envelope.insert(envelope.end(), version.begin(), version.end());
  records.push_back({kEnvelopeRecord, std::move(envelope)});
  for (const auto& field : fields_) records.push_back({field.first, field.second});
  return encode_fragment_records(records);
}

ResultCode MessageReader::open(const std::vector<std::uint8_t>& bytes, std::uint32_t kind, std::uint32_t version,
                               std::size_t max_bytes) {
  fields_.clear();
  if (bytes.size() > max_bytes) return ResultCode::StateConflict;
  std::vector<FragmentRecord> records;
  if (!decode_fragment_records(bytes, records) || records.empty()) return ResultCode::StateConflict;
  const auto& envelope = records.front();
  if (envelope.type != kEnvelopeRecord || envelope.payload.size() != 8) return ResultCode::StateConflict;
  if (read_le(envelope.payload, 0, 4) != kind) return ResultCode::StateConflict;
  const auto sent_version = read_le(envelope.payload, 4, 4);
  if (sent_version > version) return ResultCode::CapabilityTooNew;
  if (sent_version != version) return ResultCode::StateConflict;
  for (std::size_t index = 1; index < records.size(); ++index) {
    const auto& record = records[index];
    if (record.type == kEnvelopeRecord) return ResultCode::StateConflict;
    if (!fields_.emplace(record.type, record.payload).second) return ResultCode::StateConflict;
  }
  return ResultCode::Ok;
}

bool MessageReader::u32(std::uint32_t field, std::uint32_t& value) const {
  const auto found = fields_.find(field);
  if (found == fields_.end() || found->second.size() != 4) return false;
  value = static_cast<std::uint32_t>(read_le(found->second, 0, 4));
  return true;
}

bool MessageReader::u64(std::uint32_t field, std::uint64_t& value) const {
  const auto found = fields_.find(field);
  if (found == fields_.end() || found->second.size() != 8) return false;
  value = read_le(found->second, 0, 8);
  return true;
}

bool MessageReader::text(std::uint32_t field, std::string& value, std::size_t limit, bool allow_empty) const {
  const auto found = fields_.find(field);
  if (found == fields_.end()) return false;
  value.assign(found->second.begin(), found->second.end());
  return acceptable_text(value, limit, allow_empty);
}

}  // namespace pixcake::ipc::detail
