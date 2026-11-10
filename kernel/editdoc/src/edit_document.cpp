#include "pixcake/editdoc/edit_document.hpp"
#include "pixcake/support/user_notice.hpp"

#include <chrono>

#include "pixcake/persist/atomic_file.hpp"
#include "pixcake/persist/write_lock.hpp"
#include "pixcake/capability/capability_registry.hpp"
#include "pixcake/capability/fragment_codec.hpp"
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
constexpr std::uint32_t kRecordBaked = 10;
constexpr std::uint32_t kRecordEditingInstance = 11;
constexpr std::uint32_t kInnerParameter = kFragmentParameterRecord;
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

// 能力片段正文的编码只在 kernel/capability/fragment_codec 定义一次（契约 C-30）。
using InnerRecord = FragmentRecord;

bool parse_tlv(const std::vector<std::uint8_t>& in, std::vector<InnerRecord>& out) {
  return decode_fragment_records(in, out);
}

std::vector<std::uint8_t> serialize_tlv(const std::vector<InnerRecord>& records) {
  return encode_fragment_records(records);
}

bool parse_parameter(const std::vector<std::uint8_t>& payload, std::string& id, std::string& value) {
  return decode_parameter_record(payload, id, value);
}

std::vector<std::uint8_t> serialize_parameter(const std::string& id, const std::string& value) {
  return encode_parameter_record(id, value);
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

// 本程序能读写的这个能力的最高片段版本（取自参数模式，至少 1）。
std::uint32_t max_known_version(const std::string& capability_id) {
  std::uint32_t version = 1;
  for (const auto& mode : phase_one_parameter_modes()) {
    if (mode.capability_id == capability_id) version = std::max(version, mode.schema_version);
  }
  return version;
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

std::vector<VersionLimit> phase_one_version_limits() {
  // Keep the registry alive: ranging over phase_one().descriptors() directly
  // would iterate a destroyed temporary.
  const CapabilityRegistry registry = CapabilityRegistry::phase_one();
  std::vector<VersionLimit> limits;
  for (const auto& descriptor : registry.descriptors()) {
    // 能读到的最高片段版本取自参数模式（局部调色是 2：多蒙版的前缀参数）。
    std::uint32_t readable = 1;
    for (const auto& mode : phase_one_parameter_modes()) {
      if (mode.capability_id == descriptor.id) readable = std::max(readable, mode.schema_version);
    }
    limits.push_back({descriptor.id, readable});
  }
  return limits;
}

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
  document.editing_instance_id_ = image_id;
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
  document.editing_instance_id_.clear();
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

namespace {

// 正式文件里记录的修订号。供提交前在写区内与调用方期望值比较。
std::optional<std::uint64_t> durable_document_revision(const std::vector<std::uint8_t>& bytes) {
  const auto decoded = EditDocument::decode(bytes);
  if (!decoded) return std::nullopt;
  return decoded->revision();
}

// 按本文档已提交的条件参数判断目标参数是否被禁用。
bool parameter_disabled_in(const EditDocument& document, const std::string& capability_id,
                           const std::string& parameter_id) {
  return parameter_disabled_reason(capability_id, parameter_id,
                                   [&document](std::string_view capability, std::string_view parameter) {
                                     // 与能力模块编译时一样取有效值（预览、追色写回、已提交）。
                                     return document.effective_parameter(std::string(capability),
                                                                         std::string(parameter));
                                   })
      .has_value();
}

}  // namespace

LoadResult EditDocument::load(EditDocument& destination, FileSystem& files, const std::string& path,
                              const std::vector<VersionLimit>& limits) {
  LoadResult result;
  LogSink* const log = destination.log_;
  const DurableLoad opened = open_durable_file(files, path, [](const std::vector<std::uint8_t>& bytes) {
    return decode(bytes).has_value();
  }, log);
  if (opened.absent) {
    result.absent = true;
    return result;
  }
  if (opened.recovered_empty) {
    destination = make_recovered_empty();
    destination.limits_ = limits;
    destination.log_ = log;
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
    destination.log_ = log;
    result.code = ResultCode::DocumentCorrupt;
    result.recovered_empty = true;
    return result;
  }
  decoded->limits_ = limits;
  // The path is the stable identity of this editing instance.  Older files
  // do not carry an instance field, and copies intentionally retain the
  // original image id while getting a distinct document path.
  decoded->editing_instance_id_ = path;
  destination = std::move(*decoded);
  destination.log_ = log;
  result.code = ResultCode::Ok;
  result.used_backup = opened.used_backup;
  return result;
}

LoadResult EditDocument::copy_committed(FileSystem& files, const std::string& source_path,
                                        const std::string& destination_path,
                                        const std::vector<VersionLimit>& limits, EditDocument& copy,
                                        const FastExportLatch* latch) {
  LoadResult rejected;
  if (source_path.empty() || destination_path.empty() || source_path == destination_path) {
    rejected.code = ResultCode::StateConflict;
    return rejected;
  }
  EditDocument loaded;
  loaded.log_ = copy.log_;
  const LoadResult opened = load(loaded, files, source_path, limits);
  if (opened.absent || !opened.code.has_value() || *opened.code != ResultCode::Ok || opened.recovered_empty) {
    return opened;
  }
  const ResultCode saved = loaded.write_snapshot(files, destination_path, latch);
  if (saved != ResultCode::Ok) {
    rejected.code = saved;
    return rejected;
  }
  copy = std::move(loaded);
  copy.set_editing_instance_id(destination_path);
  LoadResult result;
  result.code = ResultCode::Ok;
  result.used_backup = opened.used_backup;
  return result;
}

ResultCode EditDocument::write_snapshot_steps(FileSystem& files, const std::string& path,
                                        const FastExportLatch* latch) const {
  WriteScope scope(files, path, latch);
  if (!scope) {
    return scope.code();
  }
  const ResultCode durable = check_durable_revision(files, path, revision(), durable_document_revision);
  if (durable != ResultCode::Ok) {
    return durable;
  }
  return atomic_replace(files, path, encode(), log_);
}

std::string EditDocument::image_id() const {
  for (const auto& record : records_) {
    if (record.type == kRecordImageId) {
      return text_of(record.payload);
    }
  }
  return "";
}

std::string EditDocument::editing_instance_id() const {
  if (!editing_instance_id_.empty()) return editing_instance_id_;
  for (const auto& record : records_) {
    if (record.type == kRecordEditingInstance) return text_of(record.payload);
  }
  return image_id();
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

namespace {

std::optional<std::string> record_value(const std::optional<FragmentView>& found, std::uint32_t type,
                                        const std::string& parameter_id) {
  if (!found) {
    return std::nullopt;
  }
  std::vector<InnerRecord> records;
  if (!parse_tlv(found->inner, records)) {
    return std::nullopt;
  }
  for (const auto& record : records) {
    if (record.type != type) {
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

}  // namespace

std::optional<std::string> EditDocument::committed_parameter(const std::string& capability_id,
                                                            const std::string& parameter_id) const {
  return record_value(fragment(capability_id), kInnerParameter, parameter_id);
}

std::optional<std::string> EditDocument::match_written_parameter(const std::string& capability_id,
                                                                 const std::string& parameter_id) const {
  return record_value(fragment(capability_id), kFragmentMatchWritebackRecord, parameter_id);
}

std::optional<std::string> EditDocument::effective_parameter(const std::string& capability_id,
                                                             const std::string& parameter_id) const {
  if (auto value = preview(capability_id, parameter_id)) return value;
  const auto& toggle = match_writeback_switch();
  auto on = preview(toggle.capability_id, toggle.parameter_id);
  if (!on) on = committed_parameter(toggle.capability_id, toggle.parameter_id);
  if (on == std::optional<std::string>(toggle.on_value)) {
    if (auto written = match_written_parameter(capability_id, parameter_id)) return written;
  }
  return committed_parameter(capability_id, parameter_id);
}

ResultCode EditDocument::set_preview(const std::string& capability_id, const std::string& parameter_id,
                                     const std::string& display_value) {
  if (capability_id.empty() || parameter_id.empty() || parameter_disabled_in(*this, capability_id, parameter_id)) {
    return ResultCode::StateConflict;
  }
  preview_[{capability_id, parameter_id}] = display_value;
  return ResultCode::Ok;
}

std::vector<std::string> EditDocument::preview_parameter_ids(const std::string& capability_id) const {
  std::vector<std::string> ids;
  for (const auto& [key, value] : preview_) {
    if (key.first == capability_id) ids.push_back(key.second);
  }
  return ids;
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

void EditDocument::set_editing_instance_id(std::string value) {
  editing_instance_id_ = std::move(value);
}

ResultCode EditDocument::commit_preview_steps(std::uint64_t expected_revision, const std::string& capability_id,
                                        const std::string& parameter_id,
                                        const std::vector<VersionLimit>& limits, FileSystem& files,
                                        const std::string& path) {
  WriteScope scope(files, path);
  if (!scope) {
    return scope.code();
  }
  if (expected_revision != revision() || capability_id.empty() || parameter_id.empty()) {
    return ResultCode::StateConflict;
  }
  // 只有已注册的能力能写成文档片段；被禁用的参数不能提交。
  if (CapabilityRegistry::phase_one().find(capability_id) == nullptr ||
      parameter_disabled_in(*this, capability_id, parameter_id)) {
    return ResultCode::StateConflict;
  }
  const auto preview_value = preview(capability_id, parameter_id);
  if (!preview_value) {
    return ResultCode::StateConflict;
  }
  if (!validate_parameter_display_value(capability_id, parameter_id, *preview_value)) {
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
  if (existing && existing->version > max_known_version(capability_id)) {
    return ResultCode::CapabilityTooNew;
  }

  std::vector<InnerRecord> inner_records;
  if (existing && !existing->inner.empty() && !parse_tlv(existing->inner, inner_records)) {
    return ResultCode::StateConflict;
  }
  // 精准控制打开、且这个参数有追色写回时，用户看到并改的是写回值（契约 C-33）。
  const auto& toggle = match_writeback_switch();
  const bool precise_on = committed_parameter(toggle.capability_id, toggle.parameter_id) ==
                          std::optional<std::string>(toggle.on_value);
  const std::uint32_t target_type =
      precise_on && match_written_parameter(capability_id, parameter_id) ? kFragmentMatchWritebackRecord
                                                                          : kInnerParameter;
  bool updated = false;
  for (auto& record : inner_records) {
    if (record.type != target_type) {
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
    created.type = target_type;
    created.payload = serialize_parameter(parameter_id, *preview_value);
    inner_records.push_back(std::move(created));
  }

  const ResultCode durable = check_durable_revision(files, path, expected_revision, durable_document_revision);
  if (durable != ResultCode::Ok) {
    return durable;
  }
  EditDocument next = *this;
  next.clear_recovered_flag();
  next.store_fragment(capability_id, std::max(version, max_known_version(capability_id)), serialize_tlv(inner_records));  // 旧版本片段写入时升级
  next.set_revision(revision() + 1);
  const ResultCode saved = atomic_replace(files, path, next.encode(), log_);
  if (saved != ResultCode::Ok) {
    return saved;
  }
  *this = std::move(next);
  return ResultCode::Ok;
}

ResultCode EditDocument::commit_fragment_replacement_steps(
    std::uint64_t expected_revision, const std::vector<std::string>& remove_capability_ids,
    const std::vector<FragmentView>& replacements, FileSystem& files, const std::string& path) {
  WriteScope scope(files, path);
  if (!scope) {
    return scope.code();
  }
  if (expected_revision != revision()) {
    return ResultCode::StateConflict;
  }
  if (remove_capability_ids.empty() && replacements.empty()) return ResultCode::Ok;
  for (const auto& fragment : replacements) {
    if (fragment.capability_id.empty()) {
      return ResultCode::StateConflict;
    }
    if (!limits_.empty()) {
      bool too_new = false;
      if (!known_capability(limits_, fragment.capability_id, too_new, fragment.version)) {
        return ResultCode::StateConflict;
      }
      if (too_new) return ResultCode::CapabilityTooNew;
    }
  }

  const ResultCode durable = check_durable_revision(files, path, expected_revision, durable_document_revision);
  if (durable != ResultCode::Ok) {
    return durable;
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
                                       if (std::find(remove_capability_ids.begin(),
                                                     remove_capability_ids.end(),
                                                     parsed->id) == remove_capability_ids.end()) {
                                         return false;
                                       }
                                       if (parsed->version > max_known_version(parsed->id)) return false;
                                       if (!limits_.empty()) {
                                         bool too_new = false;
                                         if (!known_capability(limits_, parsed->id, too_new, parsed->version) || too_new) {
                                           return false;
                                         }
                                       }
                                       return true;
                                     }),
                      next.records_.end());
  // A fragment replacement is also used by the reset command.  Discard any
  // in-memory previews belonging to removed capabilities so the next redraw
  // cannot resurrect an effect that was just reset.
  for (const auto& capability_id : remove_capability_ids) {
    for (auto it = next.preview_.begin(); it != next.preview_.end();) {
      if (it->first.first == capability_id) it = next.preview_.erase(it);
      else ++it;
    }
  }
  for (const auto& fragment : replacements) {
    next.store_fragment(fragment.capability_id, fragment.version, fragment.inner);
  }
  next.set_revision(revision() + 1);
  const ResultCode saved = atomic_replace(files, path, next.encode(), log_);
  if (saved != ResultCode::Ok) {
    return saved;
  }
  *this = std::move(next);
  return ResultCode::Ok;
}

bool EditDocument::effects_baked() const {
  for (const auto& record : records_) {
    if (record.type == kRecordBaked && record.payload.size() == 1 && record.payload[0] == 1) {
      return true;
    }
  }
  return false;
}

ResultCode EditDocument::commit_baked_reset_steps(std::uint64_t expected_revision, FileSystem& files,
                                            const std::string& path) {
  WriteScope scope(files, path);
  if (!scope) {
    return scope.code();
  }
  if (expected_revision != revision()) {
    return ResultCode::StateConflict;
  }
  if (effects_baked() && fragments().empty() && preview_.empty()) {
    return ResultCode::Ok;
  }
  const ResultCode durable = check_durable_revision(files, path, expected_revision, durable_document_revision);
  if (durable != ResultCode::Ok) {
    return durable;
  }
  EditDocument next = *this;
  next.records_.erase(std::remove_if(next.records_.begin(), next.records_.end(), [&](const Record& record) {
                       if (record.type != kRecordFragment) return false;
                       const auto parsed = parse_fragment(record.payload);
                       if (!parsed) return false;
                       const auto* descriptor = CapabilityRegistry::phase_one().find(parsed->id);
                       bool too_new = descriptor == nullptr || parsed->version > max_known_version(parsed->id);
                       if (!limits_.empty()) {
                         bool known_too_new = false;
                         if (known_capability(limits_, parsed->id, known_too_new, parsed->version)) {
                           too_new = known_too_new;
                         } else {
                           too_new = true;
                         }
                       }
                       return !too_new;
                     }),
                      next.records_.end());
  if (!next.effects_baked()) {
    Record baked;
    baked.type = kRecordBaked;
    baked.payload.push_back(1);
    next.records_.push_back(std::move(baked));
  }
  next.preview_.clear();
  next.set_revision(revision() + 1);
  const ResultCode saved = atomic_replace(files, path, next.encode(), log_);
  if (saved != ResultCode::Ok) {
    return saved;
  }
  *this = std::move(next);
  return ResultCode::Ok;
}

namespace {

std::uint64_t elapsed_ms(std::chrono::steady_clock::time_point began) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began).count());
}

}  // namespace

void EditDocument::note_commit(const std::string& path, ResultCode code, std::uint64_t duration_ms) const {
  if (log_ == nullptr) return;
  LogRecord record;
  record.module = "editdoc";
  record.event = "editdoc.commit";
  record.code = code;
  record.message = result_code_label(code);
  record.path = path;
  record.document_id = path;
  record.revision = revision();
  record.duration_ms = duration_ms;
  log_->write(record);
}

ResultCode EditDocument::write_snapshot(FileSystem& files, const std::string& path,
                                        const FastExportLatch* latch) const {
  const auto began = std::chrono::steady_clock::now();
  const ResultCode code = write_snapshot_steps(files, path, latch);
  note_commit(path, code, elapsed_ms(began));
  return code;
}

ResultCode EditDocument::commit_preview(std::uint64_t expected_revision, const std::string& capability_id,
                                        const std::string& parameter_id, const std::vector<VersionLimit>& limits,
                                        FileSystem& files, const std::string& path) {
  const auto began = std::chrono::steady_clock::now();
  const ResultCode code = commit_preview_steps(expected_revision, capability_id, parameter_id, limits, files, path);
  note_commit(path, code, elapsed_ms(began));
  return code;
}

namespace {
// 参数编号是否属于这个实例前缀；空前缀是没有 "m<数字>:" 前缀的那一组（局部调色实例 0）。
bool in_instance(const std::string& id, const std::string& prefix) {
  if (!prefix.empty()) return id.rfind(prefix, 0) == 0;
  return !(id.size() > 2 && id[0] == 'm' && id[1] >= '0' && id[1] <= '9');
}
}  // namespace

ResultCode EditDocument::commit_parameters(std::uint64_t expected_revision, const std::string& capability_id,
                                           const std::vector<std::string>& parameter_ids,
                                           const std::vector<VersionLimit>& limits, FileSystem& files,
                                           const std::string& path) {
  WriteScope scope(files, path);
  if (!scope) return scope.code();
  if (expected_revision != revision() || capability_id.empty() || parameter_ids.empty()) return ResultCode::StateConflict;
  if (CapabilityRegistry::phase_one().find(capability_id) == nullptr) return ResultCode::StateConflict;
  const auto existing = fragment(capability_id);
  const std::uint32_t version = existing ? existing->version : 1;
  bool too_new = false;
  if (!known_capability(limits, capability_id, too_new, version)) return ResultCode::StateConflict;
  if (too_new || (existing && existing->version > max_known_version(capability_id))) return ResultCode::CapabilityTooNew;
  std::vector<InnerRecord> inner_records;
  if (existing && !existing->inner.empty() && !parse_tlv(existing->inner, inner_records)) return ResultCode::StateConflict;
  const auto& toggle = match_writeback_switch();
  const bool precise_on = committed_parameter(toggle.capability_id, toggle.parameter_id) ==
                          std::optional<std::string>(toggle.on_value);
  for (const auto& parameter_id : parameter_ids) {
    if (parameter_id.empty() || parameter_disabled_in(*this, capability_id, parameter_id)) return ResultCode::StateConflict;
    const auto value = preview(capability_id, parameter_id);
    if (!value || !validate_parameter_display_value(capability_id, parameter_id, *value)) return ResultCode::StateConflict;
    const std::uint32_t target_type =
        precise_on && match_written_parameter(capability_id, parameter_id) ? kFragmentMatchWritebackRecord : kInnerParameter;
    bool updated = false;
    for (auto& record : inner_records) {
      if (record.type != target_type) continue;
      std::string id;
      std::string old_value;
      if (!parse_parameter(record.payload, id, old_value)) return ResultCode::StateConflict;
      if (id == parameter_id) {
        record.payload = serialize_parameter(parameter_id, *value);
        updated = true;
        break;
      }
    }
    if (!updated) {
      InnerRecord created;
      created.type = target_type;
      created.payload = serialize_parameter(parameter_id, *value);
      inner_records.push_back(std::move(created));
    }
  }
  const ResultCode durable = check_durable_revision(files, path, expected_revision, durable_document_revision);
  if (durable != ResultCode::Ok) return durable;
  EditDocument next = *this;
  next.clear_recovered_flag();
  next.store_fragment(capability_id, std::max(version, max_known_version(capability_id)), serialize_tlv(inner_records));
  next.set_revision(revision() + 1);
  const ResultCode saved = atomic_replace(files, path, next.encode(), log_);
  if (saved != ResultCode::Ok) return saved;
  *this = std::move(next);
  return ResultCode::Ok;
}

bool EditDocument::restore_preview_value(const std::string& capability_id, const std::string& parameter_id,
                                        const std::string& expected, const std::optional<std::string>& previous) {
  const auto key = std::make_pair(capability_id, parameter_id);
  const auto found = preview_.find(key);
  if (found == preview_.end() || found->second != expected) return false;
  if (previous) found->second = *previous;
  else preview_.erase(found);
  return true;
}

void EditDocument::clear_preview_prefix(const std::string& capability_id, const std::string& prefix) {
  for (auto it = preview_.begin(); it != preview_.end();) {
    if (it->first.first == capability_id && in_instance(it->first.second, prefix)) {
      it = preview_.erase(it);
    } else {
      ++it;
    }
  }
}

ResultCode EditDocument::commit_parameter_prefix_removal(std::uint64_t expected_revision,
                                                         const std::string& capability_id, const std::string& prefix,
                                                         FileSystem& files, const std::string& path) {
  WriteScope scope(files, path);
  if (!scope) return scope.code();
  if (expected_revision != revision() || capability_id.empty()) return ResultCode::StateConflict;
  const auto existing = fragment(capability_id);
  if (!existing || existing->version > max_known_version(capability_id)) return ResultCode::StateConflict;
  std::vector<InnerRecord> inner_records;
  if (!parse_tlv(existing->inner, inner_records)) return ResultCode::StateConflict;
  std::vector<InnerRecord> kept;
  bool removed = false;
  for (auto& record : inner_records) {
    std::string id;
    std::string value;
    if (record.type == kInnerParameter && parse_parameter(record.payload, id, value) && in_instance(id, prefix)) {
      removed = true;
      continue;
    }
    kept.push_back(std::move(record));
  }
  if (!removed) return ResultCode::StateConflict;
  const ResultCode durable = check_durable_revision(files, path, expected_revision, durable_document_revision);
  if (durable != ResultCode::Ok) return durable;
  EditDocument next = *this;
  next.clear_recovered_flag();
  next.store_fragment(capability_id, std::max(existing->version, max_known_version(capability_id)), serialize_tlv(kept));
  next.clear_preview_prefix(capability_id, prefix);
  next.set_revision(revision() + 1);
  const ResultCode saved = atomic_replace(files, path, next.encode(), log_);
  if (saved != ResultCode::Ok) return saved;
  *this = std::move(next);
  return ResultCode::Ok;
}

ResultCode EditDocument::commit_fragment_replacement(std::uint64_t expected_revision,
                                                     const std::vector<std::string>& remove_capability_ids,
                                                     const std::vector<FragmentView>& replacements,
                                                     FileSystem& files, const std::string& path) {
  const auto began = std::chrono::steady_clock::now();
  const ResultCode code =
      commit_fragment_replacement_steps(expected_revision, remove_capability_ids, replacements, files, path);
  note_commit(path, code, elapsed_ms(began));
  return code;
}

ResultCode EditDocument::commit_baked_reset(std::uint64_t expected_revision, FileSystem& files,
                                            const std::string& path) {
  const auto began = std::chrono::steady_clock::now();
  const ResultCode code = commit_baked_reset_steps(expected_revision, files, path);
  note_commit(path, code, elapsed_ms(began));
  return code;
}

OpenState describe_open(const LoadResult& loaded, const EditDocument& document, ResultCode source_status) {
  OpenState state;
  state.source_missing = source_status == ResultCode::SourceMissing;
  state.recovered_empty = loaded.recovered_empty;
  for (const auto& fragment : document.fragments()) {
    std::uint32_t current = 1;
    for (const auto& mode : phase_one_parameter_modes()) {
      if (mode.capability_id == fragment.capability_id) current = std::max(current, mode.schema_version);
    }
    if (fragment.version > current) state.needs_update.push_back(fragment.capability_id);
  }
  if (state.source_missing) state.notices.push_back(notice_for(NoticeScene::OpenDocument, ResultCode::SourceMissing).text);
  if (state.recovered_empty) {
    state.notices.push_back(notice_for(NoticeScene::OpenDocument, ResultCode::DocumentCorrupt).text);
  }
  if (!state.needs_update.empty()) {
    std::string text = notice_for(NoticeScene::OpenDocument, ResultCode::CapabilityTooNew).text + "：";
    for (std::size_t index = 0; index < state.needs_update.size(); ++index) {
      if (index > 0) text += "、";
      text += state.needs_update[index];
    }
    state.notices.push_back(text);
  }
  return state;
}

}  // namespace pixcake
