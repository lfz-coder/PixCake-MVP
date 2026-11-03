#include "pixcake/editdoc/edit_document.hpp"

#include "pixcake/persist/atomic_file.hpp"
#include "pixcake/support/checksum.hpp"

#include <algorithm>
#include <utility>

namespace pixcake {
namespace {

constexpr std::uint32_t kRecordDocumentVersion = 1;
constexpr std::uint32_t kRecordImageId = 2;
constexpr std::uint32_t kRecordRevision = 3;
constexpr std::uint32_t kRecordFragment = 4;
constexpr std::uint32_t kRecordMaskId = 5;
constexpr std::uint32_t kRecordSubject = 6;
constexpr std::uint32_t kRecordManual = 7;
constexpr std::uint32_t kRecordPreset = 8;
constexpr std::uint32_t kRecordRecovered = 9;
constexpr std::uint32_t kInnerParameter = 1;
constexpr std::uint32_t kFormatVersion = 1;

void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xffu));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
  out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffu));
  out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffu));
}

void append_u64(std::vector<std::uint8_t>& out, std::uint64_t value) {
  for (int shift = 0; shift < 64; shift += 8) {
    out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
  }
}

bool read_u32(const std::vector<std::uint8_t>& in, std::size_t offset, std::uint32_t& out) {
  if (offset > in.size() || in.size() - offset < 4) {
    return false;
  }
  out = static_cast<std::uint32_t>(in[offset]) |
        (static_cast<std::uint32_t>(in[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(in[offset + 2]) << 16) |
        (static_cast<std::uint32_t>(in[offset + 3]) << 24);
  return true;
}

bool read_u64(const std::vector<std::uint8_t>& in, std::size_t offset, std::uint64_t& out) {
  if (offset > in.size() || in.size() - offset < 8) {
    return false;
  }
  out = 0;
  for (int i = 0; i < 8; ++i) {
    out |= static_cast<std::uint64_t>(in[offset + static_cast<std::size_t>(i)]) << (8 * i);
  }
  return true;
}

std::vector<std::uint8_t> bytes_of(const std::string& text) {
  return std::vector<std::uint8_t>(text.begin(), text.end());
}

std::string text_of(const std::vector<std::uint8_t>& bytes) {
  return std::string(bytes.begin(), bytes.end());
}

struct InnerRecord {
  std::uint32_t type = 0;
  std::vector<std::uint8_t> payload;
};

bool parse_tlv(const std::vector<std::uint8_t>& in, std::vector<InnerRecord>& out) {
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
    InnerRecord record;
    record.type = type;
    record.payload.assign(in.begin() + static_cast<std::ptrdiff_t>(offset + 8),
                          in.begin() + static_cast<std::ptrdiff_t>(offset + 8 + size));
    out.push_back(std::move(record));
    offset += 8 + size;
  }
  return true;
}

std::vector<std::uint8_t> serialize_tlv(const std::vector<InnerRecord>& records) {
  std::vector<std::uint8_t> out;
  for (const auto& record : records) {
    append_u32(out, record.type);
    append_u32(out, static_cast<std::uint32_t>(record.payload.size()));
    out.insert(out.end(), record.payload.begin(), record.payload.end());
  }
  return out;
}

bool parse_parameter(const std::vector<std::uint8_t>& payload, std::string& id, std::string& value) {
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

std::vector<std::uint8_t> serialize_parameter(const std::string& id, const std::string& value) {
  std::vector<std::uint8_t> out;
  append_u32(out, static_cast<std::uint32_t>(id.size()));
  out.insert(out.end(), id.begin(), id.end());
  append_u32(out, static_cast<std::uint32_t>(value.size()));
  out.insert(out.end(), value.begin(), value.end());
  return out;
}

struct ParsedFragment {
  std::uint32_t version = 0;
  std::string id;
  std::vector<std::uint8_t> inner;
};

std::optional<ParsedFragment> parse_fragment(const std::vector<std::uint8_t>& payload) {
  std::uint32_t version = 0;
  std::uint32_t id_len = 0;
  if (!read_u32(payload, 0, version) || !read_u32(payload, 4, id_len)) {
    return std::nullopt;
  }
  if (id_len > payload.size() - 8) {
    return std::nullopt;
  }
  ParsedFragment parsed;
  parsed.version = version;
  parsed.id.assign(payload.begin() + 8, payload.begin() + static_cast<std::ptrdiff_t>(8 + id_len));
  parsed.inner.assign(payload.begin() + static_cast<std::ptrdiff_t>(8 + id_len), payload.end());
  return parsed;
}

std::vector<std::uint8_t> serialize_fragment(const ParsedFragment& fragment) {
  std::vector<std::uint8_t> out;
  append_u32(out, fragment.version);
  append_u32(out, static_cast<std::uint32_t>(fragment.id.size()));
  out.insert(out.end(), fragment.id.begin(), fragment.id.end());
  out.insert(out.end(), fragment.inner.begin(), fragment.inner.end());
  return out;
}

bool known_capability(const std::vector<VersionLimit>& limits, const std::string& id, bool& too_new,
                      std::uint32_t version) {
  for (const auto& limit : limits) {
    if (limit.capability_id == id) {
      too_new = version > limit.max_readable_version;
      return true;
    }
  }
  too_new = false;
  return false;
}

}  // namespace

std::optional<EditDocument> EditDocument::create(std::string image_id) {
  if (image_id.empty()) {
    return std::nullopt;
  }
  EditDocument document;
  Record version;
  version.type = kRecordDocumentVersion;
  append_u32(version.payload, 1);
  Record image;
  image.type = kRecordImageId;
  image.payload = bytes_of(image_id);
  Record revision;
  revision.type = kRecordRevision;
  append_u64(revision.payload, 0);
  document.records_.push_back(std::move(version));
  document.records_.push_back(std::move(image));
  document.records_.push_back(std::move(revision));
  return document;
}

EditDocument EditDocument::make_recovered_empty() {
  EditDocument document;
  Record version;
  version.type = kRecordDocumentVersion;
  append_u32(version.payload, 1);
  Record image;
  image.type = kRecordImageId;
  Record revision;
  revision.type = kRecordRevision;
  append_u64(revision.payload, 0);
  Record recovered;
  recovered.type = kRecordRecovered;
  recovered.payload.push_back(1);
  document.records_.push_back(std::move(version));
  document.records_.push_back(std::move(image));
  document.records_.push_back(std::move(revision));
  document.records_.push_back(std::move(recovered));
  return document;
}

std::vector<std::uint8_t> EditDocument::encode_body() const {
  std::vector<std::uint8_t> body;
  for (const auto& record : records_) {
    append_u32(body, record.type);
    append_u32(body, static_cast<std::uint32_t>(record.payload.size()));
    body.insert(body.end(), record.payload.begin(), record.payload.end());
  }
  return body;
}

std::vector<std::uint8_t> EditDocument::encode() const {
  const std::vector<std::uint8_t> body = encode_body();
  std::vector<std::uint8_t> out;
  out.push_back('P');
  out.push_back('X');
  out.push_back('E');
  out.push_back('D');
  append_u32(out, kFormatVersion);
  append_u64(out, body.size());
  out.insert(out.end(), body.begin(), body.end());
  append_u64(out, checksum_fnv1a64(body.data(), body.size()));
  return out;
}

std::optional<EditDocument> EditDocument::decode(const std::vector<std::uint8_t>& bytes) {
  if (bytes.size() < 24 || bytes[0] != 'P' || bytes[1] != 'X' || bytes[2] != 'E' || bytes[3] != 'D') {
    return std::nullopt;
  }
  std::uint32_t format = 0;
  std::uint64_t body_size = 0;
  if (!read_u32(bytes, 4, format) || !read_u64(bytes, 8, body_size)) {
    return std::nullopt;
  }
  if (body_size > bytes.size() - 24 || 16 + body_size + 8 != bytes.size()) {
    return std::nullopt;
  }
  const auto body_begin = bytes.begin() + 16;
  const std::vector<std::uint8_t> body(body_begin, body_begin + static_cast<std::ptrdiff_t>(body_size));
  std::uint64_t stored = 0;
  if (!read_u64(bytes, 16 + static_cast<std::size_t>(body_size), stored) ||
      stored != checksum_fnv1a64(body.data(), body.size()) || format != kFormatVersion) {
    return std::nullopt;
  }

  EditDocument document;
  bool saw_version = false;
  bool saw_image = false;
  bool saw_revision = false;
  std::size_t offset = 0;
  while (offset < body.size()) {
    std::uint32_t type = 0;
    std::uint32_t size = 0;
    if (!read_u32(body, offset, type) || !read_u32(body, offset + 4, size) ||
        size > body.size() - (offset + 8)) {
      return std::nullopt;
    }
    Record record;
    record.type = type;
    record.payload.assign(body.begin() + static_cast<std::ptrdiff_t>(offset + 8),
                          body.begin() + static_cast<std::ptrdiff_t>(offset + 8 + size));
    if (type == kRecordDocumentVersion) {
      if (size != 4) {
        return std::nullopt;
      }
      saw_version = true;
    } else if (type == kRecordImageId) {
      saw_image = true;
    } else if (type == kRecordRevision) {
      if (size != 8) {
        return std::nullopt;
      }
      saw_revision = true;
    } else if (type == kRecordFragment) {
      if (!parse_fragment(record.payload)) {
        return std::nullopt;
      }
    } else if (type == kRecordRecovered) {
      if (size != 1) {
        return std::nullopt;
      }
    }
    document.records_.push_back(std::move(record));
    offset += 8 + size;
  }
  if (!saw_version || !saw_image || !saw_revision) {
    return std::nullopt;
  }
  return document;
}

LoadResult EditDocument::load(EditDocument& destination, FileSystem& files, const std::string& path,
                              const std::vector<VersionLimit>& limits) {
  LoadResult result;
  const DurableLoad opened = open_durable_file(files, path, [](const std::vector<std::uint8_t>& bytes) {
    return decode(bytes).has_value();
  });
  if (opened.absent) {
    result.absent = true;
    return result;
  }
  if (opened.recovered_empty) {
    destination = make_recovered_empty();
    destination.limits_ = limits;
    result.code = ResultCode::DocumentCorrupt;
    result.recovered_empty = true;
    return result;
  }
  if (opened.code != ResultCode::Ok) {
    result.code = opened.code;
    return result;
  }
  auto decoded = decode(opened.bytes);
  if (!decoded) {
    destination = make_recovered_empty();
    destination.limits_ = limits;
    result.code = ResultCode::DocumentCorrupt;
    result.recovered_empty = true;
    return result;
  }
  decoded->limits_ = limits;
  destination = std::move(*decoded);
  result.code = ResultCode::Ok;
  result.used_backup = opened.used_backup;
  return result;
}

LoadResult EditDocument::copy_committed(FileSystem& files, const std::string& source_path,
                                        const std::string& destination_path,
                                        const std::vector<VersionLimit>& limits, EditDocument& copy) {
  LoadResult rejected;
  if (source_path.empty() || destination_path.empty() || source_path == destination_path) {
    rejected.code = ResultCode::StateConflict;
    return rejected;
  }
  EditDocument loaded;
  const LoadResult opened = load(loaded, files, source_path, limits);
  if (opened.absent || !opened.code.has_value() || *opened.code != ResultCode::Ok || opened.recovered_empty) {
    return opened;
  }
  const ResultCode saved = loaded.write_snapshot(files, destination_path);
  if (saved != ResultCode::Ok) {
    rejected.code = saved;
    return rejected;
  }
  copy = std::move(loaded);
  LoadResult result;
  result.code = ResultCode::Ok;
  result.used_backup = opened.used_backup;
  return result;
}

ResultCode EditDocument::write_snapshot(FileSystem& files, const std::string& path) const {
  return atomic_replace(files, path, encode());
}

std::string EditDocument::image_id() const {
  for (const auto& record : records_) {
    if (record.type == kRecordImageId) {
      return text_of(record.payload);
    }
  }
  return "";
}

std::uint64_t EditDocument::revision() const {
  for (const auto& record : records_) {
    if (record.type == kRecordRevision) {
      std::uint64_t value = 0;
      if (read_u64(record.payload, 0, value)) {
        return value;
      }
    }
  }
  return 0;
}

std::uint32_t EditDocument::document_version() const {
  for (const auto& record : records_) {
    if (record.type == kRecordDocumentVersion) {
      std::uint32_t value = 0;
      if (read_u32(record.payload, 0, value)) {
        return value;
      }
    }
  }
  return 0;
}

bool EditDocument::recovered_empty() const {
  for (const auto& record : records_) {
    if (record.type == kRecordRecovered && record.payload.size() == 1 && record.payload[0] == 1) {
      return true;
    }
  }
  return false;
}

std::uint64_t EditDocument::content_checksum() const {
  const std::vector<std::uint8_t> body = encode_body();
  return checksum_fnv1a64(body.data(), body.size());
}

bool EditDocument::fragment_too_new(const std::string& capability_id) const {
  const auto found = fragment(capability_id);
  if (!found) {
    return false;
  }
  bool too_new = false;
  if (!known_capability(limits_, capability_id, too_new, found->version)) {
    return false;
  }
  return too_new;
}

void EditDocument::add_mask_id(std::string id) {
  records_.push_back(Record{kRecordMaskId, bytes_of(id)});
}

std::vector<std::string> EditDocument::mask_ids() const {
  std::vector<std::string> ids;
  for (const auto& record : records_) {
    if (record.type == kRecordMaskId) {
      ids.push_back(text_of(record.payload));
    }
  }
  return ids;
}

void EditDocument::set_subject_override(std::vector<std::uint8_t> bytes) {
  for (auto it = records_.rbegin(); it != records_.rend(); ++it) {
    if (it->type == kRecordSubject) {
      it->payload = std::move(bytes);
      return;
    }
  }
  records_.push_back(Record{kRecordSubject, std::move(bytes)});
}

std::vector<std::uint8_t> EditDocument::subject_override() const {
  for (auto it = records_.rbegin(); it != records_.rend(); ++it) {
    if (it->type == kRecordSubject) {
      return it->payload;
    }
  }
  return {};
}

void EditDocument::add_manual_ref(std::string id) {
  records_.push_back(Record{kRecordManual, bytes_of(id)});
}

std::vector<std::string> EditDocument::manual_refs() const {
  std::vector<std::string> ids;
  for (const auto& record : records_) {
    if (record.type == kRecordManual) {
      ids.push_back(text_of(record.payload));
    }
  }
  return ids;
}

void EditDocument::add_preset_ref(std::string id) {
  records_.push_back(Record{kRecordPreset, bytes_of(id)});
}

std::vector<std::string> EditDocument::preset_refs() const {
  std::vector<std::string> ids;
  for (const auto& record : records_) {
    if (record.type == kRecordPreset) {
      ids.push_back(text_of(record.payload));
    }
  }
  return ids;
}

void EditDocument::append_raw_record(std::uint32_t type, std::vector<std::uint8_t> payload) {
  records_.push_back(Record{type, std::move(payload)});
}

std::vector<std::vector<std::uint8_t>> EditDocument::payloads_of_type(std::uint32_t type) const {
  std::vector<std::vector<std::uint8_t>> payloads;
  for (const auto& record : records_) {
    if (record.type == type) {
      payloads.push_back(record.payload);
    }
  }
  return payloads;
}

void EditDocument::store_fragment(std::string capability_id, std::uint32_t version,
                                  std::vector<std::uint8_t> inner) {
  ParsedFragment parsed;
  parsed.version = version;
  parsed.id = capability_id;
  parsed.inner = std::move(inner);
  const std::vector<std::uint8_t> payload = serialize_fragment(parsed);
  for (auto& record : records_) {
    if (record.type != kRecordFragment) {
      continue;
    }
    const auto existing = parse_fragment(record.payload);
    if (existing && existing->id == capability_id) {
      record.payload = payload;
      return;
    }
  }
  records_.push_back(Record{kRecordFragment, payload});
}

std::optional<FragmentView> EditDocument::fragment(const std::string& capability_id) const {
  for (const auto& record : records_) {
    if (record.type != kRecordFragment) {
      continue;
    }
    const auto parsed = parse_fragment(record.payload);
    if (parsed && parsed->id == capability_id) {
      FragmentView view;
      view.capability_id = parsed->id;
      view.version = parsed->version;
      view.inner = parsed->inner;
      return view;
    }
  }
  return std::nullopt;
}

std::vector<FragmentView> EditDocument::fragments() const {
  std::vector<FragmentView> views;
  for (const auto& record : records_) {
    if (record.type != kRecordFragment) {
      continue;
    }
    const auto parsed = parse_fragment(record.payload);
    if (!parsed) {
      continue;
    }
    FragmentView view;
    view.capability_id = parsed->id;
    view.version = parsed->version;
    view.inner = parsed->inner;
    views.push_back(std::move(view));
  }
  return views;
}

std::optional<std::string> EditDocument::committed_parameter(const std::string& capability_id,
                                                            const std::string& parameter_id) const {
  const auto found = fragment(capability_id);
  if (!found) {
    return std::nullopt;
  }
  std::vector<InnerRecord> records;
  if (!parse_tlv(found->inner, records)) {
    return std::nullopt;
  }
  for (const auto& record : records) {
    if (record.type != kInnerParameter) {
      continue;
    }
    std::string id;
    std::string value;
    if (!parse_parameter(record.payload, id, value)) {
      return std::nullopt;
    }
    if (id == parameter_id) {
      return value;
    }
  }
  return std::nullopt;
}

ResultCode EditDocument::set_preview(const std::string& capability_id, const std::string& parameter_id,
                                     const std::string& display_value) {
  if (capability_id.empty() || parameter_id.empty()) {
    return ResultCode::StateConflict;
  }
  preview_[{capability_id, parameter_id}] = display_value;
  return ResultCode::Ok;
}

std::optional<std::string> EditDocument::preview(const std::string& capability_id,
                                                 const std::string& parameter_id) const {
  const auto found = preview_.find({capability_id, parameter_id});
  if (found == preview_.end()) {
    return std::nullopt;
  }
  return found->second;
}

void EditDocument::set_revision(std::uint64_t value) {
  for (auto& record : records_) {
    if (record.type == kRecordRevision) {
      record.payload.clear();
      append_u64(record.payload, value);
      return;
    }
  }
}

void EditDocument::clear_recovered_flag() {
  records_.erase(std::remove_if(records_.begin(), records_.end(),
                                [](const Record& record) { return record.type == kRecordRecovered; }),
                 records_.end());
}

ResultCode EditDocument::commit_preview(std::uint64_t expected_revision, const std::string& capability_id,
                                        const std::string& parameter_id,
                                        const std::vector<VersionLimit>& limits, FileSystem& files,
                                        const std::string& path) {
  if (expected_revision != revision() || capability_id.empty() || parameter_id.empty()) {
    return ResultCode::StateConflict;
  }
  const auto preview_value = preview(capability_id, parameter_id);
  if (!preview_value) {
    return ResultCode::StateConflict;
  }
  const auto existing = fragment(capability_id);
  const std::uint32_t version = existing ? existing->version : 1;
  bool too_new = false;
  if (!known_capability(limits, capability_id, too_new, version)) {
    return ResultCode::StateConflict;
  }
  if (too_new) {
    return ResultCode::CapabilityTooNew;
  }

  std::vector<InnerRecord> inner_records;
  if (existing && !existing->inner.empty() && !parse_tlv(existing->inner, inner_records)) {
    return ResultCode::StateConflict;
  }
  bool updated = false;
  for (auto& record : inner_records) {
    if (record.type != kInnerParameter) {
      continue;
    }
    std::string id;
    std::string value;
    if (!parse_parameter(record.payload, id, value)) {
      return ResultCode::StateConflict;
    }
    if (id == parameter_id) {
      record.payload = serialize_parameter(parameter_id, *preview_value);
      updated = true;
      break;
    }
  }
  if (!updated) {
    InnerRecord created;
    created.type = kInnerParameter;
    created.payload = serialize_parameter(parameter_id, *preview_value);
    inner_records.push_back(std::move(created));
  }

  EditDocument next = *this;
  next.clear_recovered_flag();
  next.store_fragment(capability_id, version, serialize_tlv(inner_records));
  next.set_revision(revision() + 1);
  const ResultCode saved = atomic_replace(files, path, next.encode());
  if (saved != ResultCode::Ok) {
    return saved;
  }
  *this = std::move(next);
  return ResultCode::Ok;
}

ResultCode EditDocument::commit_fragment_replacement(
    std::uint64_t expected_revision, const std::vector<std::string>& remove_capability_ids,
    const std::vector<FragmentView>& replacements, FileSystem& files, const std::string& path) {
  if (expected_revision != revision()) {
    return ResultCode::StateConflict;
  }
  for (const auto& fragment : replacements) {
    if (fragment.capability_id.empty()) {
      return ResultCode::StateConflict;
    }
  }

  EditDocument next = *this;
  next.clear_recovered_flag();
  next.records_.erase(std::remove_if(next.records_.begin(), next.records_.end(),
                                     [&](const Record& record) {
                                       if (record.type != kRecordFragment) {
                                         return false;
                                       }
                                       const auto parsed = parse_fragment(record.payload);
                                       if (!parsed) {
                                         return false;
                                       }
                                       return std::find(remove_capability_ids.begin(),
                                                        remove_capability_ids.end(),
                                                        parsed->id) != remove_capability_ids.end();
                                     }),
                      next.records_.end());
  for (const auto& fragment : replacements) {
    next.store_fragment(fragment.capability_id, fragment.version, fragment.inner);
  }
  next.set_revision(revision() + 1);
  const ResultCode saved = atomic_replace(files, path, next.encode());
  if (saved != ResultCode::Ok) {
    return saved;
  }
  *this = std::move(next);
  return ResultCode::Ok;
}

}  // namespace pixcake
