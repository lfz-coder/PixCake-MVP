#include "pixcake/generation_session/generation_history.hpp"

#include "pixcake/persist/atomic_file.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace pixcake::generation_session {
namespace {

constexpr std::uint8_t kMagic[] = {'P', 'X', 'G', 'H'};
constexpr std::uint32_t kVersion = 1;
constexpr std::size_t kMaxBytes = 4 * 1024 * 1024;
constexpr std::size_t kMaxString = 4096;
constexpr std::size_t kMaxBlob = 32 * 1024;
constexpr std::size_t kMaxList = 64;

void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8) out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
}

void append_u64(std::vector<std::uint8_t>& out, std::uint64_t value) {
  for (unsigned shift = 0; shift < 64; shift += 8) out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
}

bool read_u32(const std::vector<std::uint8_t>& bytes, std::size_t& offset, std::uint32_t& value) {
  if (offset > bytes.size() || bytes.size() - offset < 4) return false;
  value = static_cast<std::uint32_t>(bytes[offset]) |
          (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
          (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
          (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
  offset += 4;
  return true;
}

bool read_u64(const std::vector<std::uint8_t>& bytes, std::size_t& offset, std::uint64_t& value) {
  if (offset > bytes.size() || bytes.size() - offset < 8) return false;
  value = 0;
  for (unsigned shift = 0; shift < 64; shift += 8) value |= static_cast<std::uint64_t>(bytes[offset++]) << shift;
  return true;
}

void append_blob(std::vector<std::uint8_t>& out, const std::vector<std::uint8_t>& value) {
  append_u32(out, static_cast<std::uint32_t>(value.size()));
  out.insert(out.end(), value.begin(), value.end());
}

void append_string(std::vector<std::uint8_t>& out, const std::string& value) {
  append_u32(out, static_cast<std::uint32_t>(value.size()));
  out.insert(out.end(), value.begin(), value.end());
}

bool read_string(const std::vector<std::uint8_t>& bytes, std::size_t& offset, std::string& value) {
  std::uint32_t size = 0;
  if (!read_u32(bytes, offset, size) || size > kMaxString || size > bytes.size() - offset ||
      bytes.size() - offset < size) return false;
  value.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
               bytes.begin() + static_cast<std::ptrdiff_t>(offset + size));
  offset += size;
  return value.find('\0') == std::string::npos;
}

bool read_blob(const std::vector<std::uint8_t>& bytes, std::size_t& offset, std::vector<std::uint8_t>& value) {
  std::uint32_t size = 0;
  if (!read_u32(bytes, offset, size) || size > kMaxBlob || size > bytes.size() - offset ||
      bytes.size() - offset < size) return false;
  value.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
               bytes.begin() + static_cast<std::ptrdiff_t>(offset + size));
  offset += size;
  return true;
}

bool valid_count(std::uint32_t count) { return count <= kMaxList; }

void append_strings(std::vector<std::uint8_t>& out, const std::vector<std::string>& values) {
  append_u32(out, static_cast<std::uint32_t>(values.size()));
  for (const auto& value : values) append_string(out, value);
}

bool read_strings(const std::vector<std::uint8_t>& bytes, std::size_t& offset, std::vector<std::string>& values) {
  std::uint32_t count = 0;
  if (!read_u32(bytes, offset, count) || !valid_count(count)) return false;
  values.clear();
  values.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    std::string value;
    if (!read_string(bytes, offset, value)) return false;
    values.push_back(std::move(value));
  }
  return true;
}

}  // namespace

ResultCode GenerationHistoryStore::open(const std::string& path, std::unique_ptr<GenerationHistoryStore>& out,
                                        std::string& diagnostic) {
  out.reset();
  if (path.empty() || path.find('\0') != std::string::npos) {
    diagnostic = "generation history path is invalid";
    return ResultCode::StateConflict;
  }
  std::unique_ptr<GenerationHistoryStore> store(new GenerationHistoryStore(path));
  const ResultCode loaded = store->load(diagnostic);
  if (loaded != ResultCode::Ok) return loaded;
  out = std::move(store);
  diagnostic.clear();
  return ResultCode::Ok;
}

ResultCode GenerationHistoryStore::load(std::string& diagnostic) {
  if (!files_.exists(path_)) {
    diagnostic.clear();
    return ResultCode::Ok;
  }
  std::vector<std::uint8_t> bytes;
  const IoResult read = files_.read_file(path_, bytes);
  if (!read.ok) {
    diagnostic = "generation history cannot be read: " + path_;
    return read.code;
  }
  if (!decode(bytes, entries_)) {
    diagnostic = "generation history is corrupt: " + path_;
    return ResultCode::DocumentCorrupt;
  }
  return ResultCode::Ok;
}

std::vector<std::uint8_t> GenerationHistoryStore::encode(const std::vector<GenerationHistoryEntry>& entries) {
  std::vector<std::uint8_t> out;
  out.insert(out.end(), std::begin(kMagic), std::end(kMagic));
  append_u32(out, kVersion);
  append_u32(out, static_cast<std::uint32_t>(entries.size()));
  for (const auto& entry : entries) {
    append_string(out, entry.task_id);
    append_string(out, entry.image_id);
    append_u64(out, entry.source_revision);
    append_string(out, entry.tool_id);
    append_string(out, entry.result_shape);
    append_string(out, entry.status);
    append_string(out, entry.idempotency_key);
    append_blob(out, entry.parameter_payload);
    append_strings(out, entry.mask_ids);
    append_strings(out, entry.reference_image_ids);
    append_u32(out, static_cast<std::uint32_t>(entry.results.size()));
    for (const auto& result : entry.results) {
      append_u32(out, result.ordinal);
      append_string(out, result.result_id);
      append_string(out, result.pixel_file);
      append_string(out, result.checksum);
      append_u32(out, result.width);
      append_u32(out, result.height);
      append_blob(out, result.continuation_parameters);
    }
  }
  return out;
}

bool GenerationHistoryStore::decode(const std::vector<std::uint8_t>& bytes,
                                     std::vector<GenerationHistoryEntry>& out) {
  if (bytes.size() < 12 || bytes.size() > kMaxBytes || !std::equal(std::begin(kMagic), std::end(kMagic), bytes.begin())) {
    return false;
  }
  std::size_t offset = 4;
  std::uint32_t version = 0;
  std::uint32_t count = 0;
  if (!read_u32(bytes, offset, version) || version != kVersion || !read_u32(bytes, offset, count) ||
      count > kMaxEntries) return false;
  out.clear();
  out.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    GenerationHistoryEntry entry;
    if (!read_string(bytes, offset, entry.task_id) || !read_string(bytes, offset, entry.image_id) ||
        !read_u64(bytes, offset, entry.source_revision) || !read_string(bytes, offset, entry.tool_id) ||
        !read_string(bytes, offset, entry.result_shape) || !read_string(bytes, offset, entry.status) ||
        !read_string(bytes, offset, entry.idempotency_key) || !read_blob(bytes, offset, entry.parameter_payload) ||
        entry.task_id.empty() || entry.image_id.empty() || entry.tool_id.empty() || entry.status.empty() ||
        !read_strings(bytes, offset, entry.mask_ids) || !read_strings(bytes, offset, entry.reference_image_ids)) {
      return false;
    }
    std::uint32_t result_count = 0;
    if (!read_u32(bytes, offset, result_count) || !valid_count(result_count)) return false;
    entry.results.reserve(result_count);
    for (std::uint32_t result_index = 0; result_index < result_count; ++result_index) {
      GenerationHistoryResult result;
      if (!read_u32(bytes, offset, result.ordinal) || !read_string(bytes, offset, result.result_id) ||
          !read_string(bytes, offset, result.pixel_file) || !read_string(bytes, offset, result.checksum) ||
          !read_u32(bytes, offset, result.width) || !read_u32(bytes, offset, result.height) ||
          !read_blob(bytes, offset, result.continuation_parameters) || result.result_id.empty() ||
          result.pixel_file.empty() || result.checksum.empty() || result.width == 0 || result.height == 0) {
        return false;
      }
      entry.results.push_back(std::move(result));
    }
    out.push_back(std::move(entry));
  }
  return offset == bytes.size();
}

ResultCode GenerationHistoryStore::persist() {
  const std::vector<std::uint8_t> bytes = encode(entries_);
  if (bytes.size() > kMaxBytes) return ResultCode::StateConflict;
  return atomic_replace(files_, path_, bytes);
}

const GenerationHistoryEntry* GenerationHistoryStore::find(const std::string& task_id) const {
  const auto found = std::find_if(entries_.begin(), entries_.end(),
                                  [&](const auto& entry) { return entry.task_id == task_id; });
  return found == entries_.end() ? nullptr : &*found;
}

ResultCode GenerationHistoryStore::record_task(const generate::GenerationTask& task, std::string status) {
  if (generate::validate_task(task) != ResultCode::Ok || task.task_id.empty() || status.empty() ||
      status.find('\0') != std::string::npos) return ResultCode::StateConflict;
  GenerationHistoryEntry entry;
  entry.task_id = task.task_id;
  entry.image_id = task.image_id;
  entry.source_revision = task.source_revision;
  entry.tool_id = task.tool.tool_id;
  entry.result_shape = generate::result_shape_name(task.tool.result_shape);
  entry.status = std::move(status);
  entry.idempotency_key = task.idempotency_key;
  entry.parameter_payload = task.parameter_payload;
  entry.mask_ids = task.mask_ids;
  entry.reference_image_ids = task.reference_image_ids;
  if (entry.parameter_payload.size() > kMaxBlob || entry.mask_ids.size() > kMaxList ||
      entry.reference_image_ids.size() > kMaxList) return ResultCode::StateConflict;

  const auto found = std::find_if(entries_.begin(), entries_.end(),
                                  [&](const auto& value) { return value.task_id == entry.task_id; });
  if (found != entries_.end()) {
    *found = std::move(entry);
  } else {
    entries_.push_back(std::move(entry));
    if (entries_.size() > kMaxEntries) entries_.erase(entries_.begin());
  }
  return persist();
}

ResultCode GenerationHistoryStore::update_status(const std::string& task_id, std::string status,
                                                 const std::vector<GenerationHistoryResult>& results) {
  if (task_id.empty() || status.empty() || status.find('\0') != std::string::npos) return ResultCode::StateConflict;
  auto found = std::find_if(entries_.begin(), entries_.end(),
                            [&](const auto& entry) { return entry.task_id == task_id; });
  if (found == entries_.end()) return ResultCode::StateConflict;
  if (results.size() > kMaxList) return ResultCode::StateConflict;
  found->status = std::move(status);
  if (!results.empty()) found->results = results;
  return persist();
}

}  // namespace pixcake::generation_session
