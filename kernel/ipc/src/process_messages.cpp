#include "pixcake/ipc/process_messages.hpp"

#include "message_codec.hpp"

#include <limits>
#include <utility>

namespace pixcake::ipc {
namespace {

using detail::MessageReader;
using detail::MessageWriter;

constexpr std::uint32_t kExportProgress = 4;
constexpr std::uint32_t kGenerationProgress = 5;
constexpr std::uint32_t kTaskCancel = 6;
constexpr std::uint32_t kHeartbeat = 7;
constexpr std::uint32_t kExportStart = 12;
constexpr std::uint32_t kGenerationSubmit = 13;
constexpr std::uint32_t kGenerationResultConfirm = 14;
constexpr std::size_t kMaxId = 1024;
constexpr std::size_t kMaxText = 4096;
constexpr std::size_t kMaxTaskPayload = 32 * 1024;
constexpr std::size_t kMaxTaskList = 16 * 1024;
constexpr std::size_t kMaxResultList = 48 * 1024;
constexpr std::size_t kMaxResults = 32;

bool valid_code(std::uint32_t value) { return value <= static_cast<std::uint32_t>(ResultCode::IoFailed); }

bool valid_state(std::uint32_t value) {
  return value <= static_cast<std::uint32_t>(generate::TaskState::CancelledRunning);
}

ResultCode finish(const MessageWriter& writer, std::vector<std::uint8_t>& out) {
  out = writer.bytes();
  return out.size() <= kMaxProcessMessageBytes ? ResultCode::Ok : ResultCode::StateConflict;
}

bool append_string_list(const std::vector<std::string>& values, std::vector<std::uint8_t>& out,
                        std::size_t max_bytes) {
  out.clear();
  if (values.size() > std::numeric_limits<std::uint32_t>::max()) return false;
  const auto append_u32 = [&out](std::uint32_t value) {
    for (unsigned int shift = 0; shift < 32; shift += 8) {
      out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
  };
  append_u32(static_cast<std::uint32_t>(values.size()));
  for (const auto& value : values) {
    if (value.empty() || value.find('\0') != std::string::npos ||
        value.size() > std::numeric_limits<std::uint32_t>::max()) {
      return false;
    }
    if (out.size() > max_bytes || max_bytes - out.size() < 4 || value.size() > max_bytes - out.size() - 4) {
      return false;
    }
    append_u32(static_cast<std::uint32_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
  }
  return out.size() <= max_bytes;
}

bool read_string_list(const std::vector<std::uint8_t>& bytes, std::vector<std::string>& out,
                      std::size_t max_bytes) {
  if (bytes.size() > max_bytes || bytes.size() < 4) return false;
  std::size_t offset = 0;
  const auto read_u32 = [&bytes, &offset](std::uint32_t& value) {
    if (bytes.size() - offset < 4) return false;
    value = static_cast<std::uint32_t>(bytes[offset]) |
            (static_cast<std::uint32_t>(bytes[offset + 1]) << 8u) |
            (static_cast<std::uint32_t>(bytes[offset + 2]) << 16u) |
            (static_cast<std::uint32_t>(bytes[offset + 3]) << 24u);
    offset += 4;
    return true;
  };
  std::uint32_t count = 0;
  if (!read_u32(count)) return false;
  // Every item needs at least a four-byte length and one byte of payload.  Bound
  // the count before reserve() so malformed input cannot request an enormous
  // allocation while still being below the byte limit.
  if (count > (bytes.size() - 4) / 5) return false;
  out.clear();
  out.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    std::uint32_t size = 0;
    if (!read_u32(size) || bytes.size() - offset < size || size == 0) return false;
    std::string value(reinterpret_cast<const char*>(bytes.data() + offset), size);
    if (value.find('\0') != std::string::npos) return false;
    out.push_back(std::move(value));
    offset += size;
  }
  return offset == bytes.size();
}

bool valid_shape(std::uint32_t value) {
  return value <= static_cast<std::uint32_t>(generate::ResultShape::TransformableLayer);
}

bool append_result_list(const std::vector<GenerationResultInfo>& values, std::vector<std::uint8_t>& out) {
  out.clear();
  if (values.size() > kMaxResults) return false;
  const auto append_u32 = [&out](std::uint32_t value) {
    for (unsigned int shift = 0; shift < 32; shift += 8) {
      out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
  };
  const auto append_text = [&](const std::string& value, std::size_t limit, bool allow_empty) {
    if ((!allow_empty && value.empty()) || value.size() > limit || value.find('\0') != std::string::npos ||
        value.size() > std::numeric_limits<std::uint32_t>::max()) {
      return false;
    }
    if (out.size() > kMaxResultList || kMaxResultList - out.size() < 4 ||
        value.size() > kMaxResultList - out.size() - 4) {
      return false;
    }
    append_u32(static_cast<std::uint32_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
    return true;
  };
  append_u32(static_cast<std::uint32_t>(values.size()));
  for (const auto& value : values) {
    append_u32(value.ordinal);
    if (!append_text(value.result_id, kMaxId, false) || !append_text(value.pixel_file, kMaxText, false) ||
        !append_text(value.checksum, kMaxId, false) || value.width == 0 || value.height == 0 ||
        value.continuation_parameters.size() > kMaxTaskPayload) {
      return false;
    }
    append_u32(value.width);
    append_u32(value.height);
    append_u32(static_cast<std::uint32_t>(value.continuation_parameters.size()));
    out.insert(out.end(), value.continuation_parameters.begin(), value.continuation_parameters.end());
    if (out.size() > kMaxResultList) return false;
  }
  return out.size() <= kMaxResultList;
}

bool read_result_list(const std::vector<std::uint8_t>& bytes, std::vector<GenerationResultInfo>& out) {
  if (bytes.size() > kMaxResultList || bytes.size() < 4) return false;
  std::size_t offset = 0;
  const auto read_u32 = [&bytes, &offset](std::uint32_t& value) {
    if (bytes.size() - offset < 4) return false;
    value = static_cast<std::uint32_t>(bytes[offset]) |
            (static_cast<std::uint32_t>(bytes[offset + 1]) << 8u) |
            (static_cast<std::uint32_t>(bytes[offset + 2]) << 16u) |
            (static_cast<std::uint32_t>(bytes[offset + 3]) << 24u);
    offset += 4;
    return true;
  };
  const auto read_text = [&](std::string& value, std::size_t limit, bool allow_empty) {
    std::uint32_t size = 0;
    if (!read_u32(size) || bytes.size() - offset < size || size > limit || (!allow_empty && size == 0)) {
      return false;
    }
    value.assign(reinterpret_cast<const char*>(bytes.data() + offset), size);
    offset += size;
    return value.find('\0') == std::string::npos;
  };
  std::uint32_t count = 0;
  if (!read_u32(count) || count > kMaxResults) return false;
  out.clear();
  out.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    GenerationResultInfo value;
    std::uint32_t continuation_size = 0;
    if (!read_u32(value.ordinal) || value.ordinal != index || !read_text(value.result_id, kMaxId, false) ||
        !read_text(value.pixel_file, kMaxText, false) || !read_text(value.checksum, kMaxId, false) ||
        !read_u32(value.width) || !read_u32(value.height) || value.width == 0 || value.height == 0 ||
        !read_u32(continuation_size) || continuation_size > kMaxTaskPayload ||
        bytes.size() - offset < continuation_size) {
      return false;
    }
    value.continuation_parameters.assign(
        bytes.begin() + static_cast<std::ptrdiff_t>(offset),
        bytes.begin() + static_cast<std::ptrdiff_t>(offset + continuation_size));
    offset += continuation_size;
    out.push_back(std::move(value));
  }
  return offset == bytes.size();
}

}  // namespace

ResultCode encode_export_progress(const ExportProgressMessage& message, std::vector<std::uint8_t>& out) {
  out.clear();
  if (message.protocol_version != kProcessProtocolVersion || message.completed > message.total) {
    return ResultCode::StateConflict;
  }
  MessageWriter writer(kExportProgress, message.protocol_version);
  if (!writer.text(1, message.export_id, kMaxId) || !writer.text(2, message.status, kMaxText)) {
    return ResultCode::StateConflict;
  }
  if (!writer.text(6, message.reason, kMaxText, true)) return ResultCode::StateConflict;
  writer.u32(3, message.completed);
  writer.u32(4, message.total);
  writer.u32(5, static_cast<std::uint32_t>(message.code));
  return finish(writer, out);
}

ResultCode decode_export_progress(const std::vector<std::uint8_t>& bytes, ExportProgressMessage& out) {
  MessageReader reader;
  if (const ResultCode opened = reader.open(bytes, kExportProgress, kProcessProtocolVersion, kMaxProcessMessageBytes);
      opened != ResultCode::Ok) {
    return opened;
  }
  std::uint32_t code = 0;
  ExportProgressMessage message;
  if (!reader.text(1, message.export_id, kMaxId) || !reader.text(2, message.status, kMaxText) ||
      !reader.u32(3, message.completed) || !reader.u32(4, message.total) || !reader.u32(5, code) ||
      !valid_code(code) || message.completed > message.total ||
      (reader.has(6) && !reader.text(6, message.reason, kMaxText, true))) {
    return ResultCode::StateConflict;
  }
  message.code = static_cast<ResultCode>(code);
  out = std::move(message);
  return ResultCode::Ok;
}

ResultCode encode_export_start(const ExportStartMessage& message, std::vector<std::uint8_t>& out) {
  out.clear();
  if (message.protocol_version != kProcessProtocolVersion) return ResultCode::StateConflict;
  MessageWriter writer(kExportStart, message.protocol_version);
  if (!writer.text(1, message.export_id, kMaxId) || !writer.text(2, message.source_path, kMaxText)) {
    return ResultCode::StateConflict;
  }
  return finish(writer, out);
}

ResultCode decode_export_start(const std::vector<std::uint8_t>& bytes, ExportStartMessage& out) {
  MessageReader reader;
  if (const ResultCode opened = reader.open(bytes, kExportStart, kProcessProtocolVersion, kMaxProcessMessageBytes);
      opened != ResultCode::Ok) {
    return opened;
  }
  ExportStartMessage message;
  if (!reader.text(1, message.export_id, kMaxId) || !reader.text(2, message.source_path, kMaxText)) {
    return ResultCode::StateConflict;
  }
  out = std::move(message);
  return ResultCode::Ok;
}

namespace {
bool kind_is(const std::vector<std::uint8_t>& bytes, std::uint32_t expected) {
  std::uint32_t kind = 0, version = 0;
  return detail::peek_kind(bytes, kind, version) && kind == expected;
}
}  // namespace

bool is_export_start(const std::vector<std::uint8_t>& bytes) { return kind_is(bytes, kExportStart); }
bool is_export_progress(const std::vector<std::uint8_t>& bytes) { return kind_is(bytes, kExportProgress); }
bool is_generation_progress(const std::vector<std::uint8_t>& bytes) { return kind_is(bytes, kGenerationProgress); }
bool is_generation_submit(const std::vector<std::uint8_t>& bytes) { return kind_is(bytes, kGenerationSubmit); }
bool is_generation_result_confirm(const std::vector<std::uint8_t>& bytes) {
  return kind_is(bytes, kGenerationResultConfirm);
}
bool is_task_cancel(const std::vector<std::uint8_t>& bytes) { return kind_is(bytes, kTaskCancel); }

ResultCode encode_generation_progress(const GenerationProgressMessage& message, std::vector<std::uint8_t>& out) {
  out.clear();
  if (message.protocol_version != kProcessProtocolVersion || message.percent > 100 ||
      !valid_state(static_cast<std::uint32_t>(message.state)) ||
      !valid_code(static_cast<std::uint32_t>(message.code))) {
    return ResultCode::StateConflict;
  }
  std::vector<std::uint8_t> result_list;
  if (!message.results.empty() && !append_result_list(message.results, result_list)) {
    return ResultCode::StateConflict;
  }
  MessageWriter writer(kGenerationProgress, message.protocol_version);
  if (!writer.text(1, message.task_id, kMaxId) || !writer.text(4, message.result_id, kMaxId, true) ||
      !writer.text(5, message.pixel_file, kMaxText, true) || !writer.text(6, message.checksum, kMaxId, true)) {
    return ResultCode::StateConflict;
  }
  writer.u32(2, static_cast<std::uint32_t>(message.state));
  writer.u32(3, message.percent);
  writer.u32(7, static_cast<std::uint32_t>(message.code));
  if (!message.results.empty() && !writer.blob(8, result_list, kMaxResultList)) return ResultCode::StateConflict;
  return finish(writer, out);
}

ResultCode decode_generation_progress(const std::vector<std::uint8_t>& bytes, GenerationProgressMessage& out) {
  MessageReader reader;
  if (const ResultCode opened =
          reader.open(bytes, kGenerationProgress, kProcessProtocolVersion, kMaxProcessMessageBytes);
      opened != ResultCode::Ok) {
    return opened;
  }
  std::uint32_t state = 0;
  std::uint32_t code = 0;
  std::vector<std::uint8_t> result_list;
  GenerationProgressMessage message;
  if (!reader.text(1, message.task_id, kMaxId) || !reader.u32(2, state) || !valid_state(state) ||
      !reader.u32(3, message.percent) || message.percent > 100 || !reader.text(4, message.result_id, kMaxId, true) ||
      !reader.text(5, message.pixel_file, kMaxText, true) || !reader.text(6, message.checksum, kMaxId, true) ||
      !reader.u32(7, code) || !valid_code(code) ||
      (reader.has(8) && !reader.blob(8, result_list, kMaxResultList))) {
    return ResultCode::StateConflict;
  }
  if (reader.has(8) && !read_result_list(result_list, message.results)) return ResultCode::StateConflict;
  message.state = static_cast<generate::TaskState>(state);
  message.code = static_cast<ResultCode>(code);
  out = std::move(message);
  return ResultCode::Ok;
}

ResultCode encode_generation_submit(const GenerationSubmitMessage& message, std::vector<std::uint8_t>& out) {
  out.clear();
  if (message.protocol_version != kProcessProtocolVersion) {
    return ResultCode::StateConflict;
  }
  const ResultCode task_validation = generate::validate_task(message.task);
  if (task_validation != ResultCode::Ok) return task_validation;
  std::vector<std::uint8_t> masks;
  std::vector<std::uint8_t> references;
  if (!append_string_list(message.task.mask_ids, masks, kMaxTaskList) ||
      !append_string_list(message.task.reference_image_ids, references, kMaxTaskList)) {
    return ResultCode::StateConflict;
  }
  MessageWriter writer(kGenerationSubmit, message.protocol_version);
  if (!writer.text(1, message.task.task_id, kMaxId) || !writer.text(2, message.task.image_id, kMaxId) ||
      !writer.text(4, message.task.tool.tool_id, kMaxId) ||
      !writer.text(5, message.task.tool.parameter_mode_id, kMaxId) ||
      !writer.text(7, message.task.tool.billing_rule_id, kMaxId) ||
      !writer.text(14, message.task.idempotency_key, kMaxId, true) ||
      !writer.blob(11, message.task.parameter_payload, kMaxTaskPayload) ||
      !writer.blob(12, masks, kMaxTaskList) || !writer.blob(13, references, kMaxTaskList)) {
    return ResultCode::StateConflict;
  }
  writer.u64(3, message.task.source_revision);
  writer.u32(6, static_cast<std::uint32_t>(message.task.tool.result_shape));
  writer.u32(8, message.task.tool.output_count);
  writer.u32(9, message.task.tool.cancellable ? 1u : 0u);
  writer.u32(10, message.task.parameter_schema_version);
  return finish(writer, out);
}

ResultCode decode_generation_submit(const std::vector<std::uint8_t>& bytes, GenerationSubmitMessage& out) {
  MessageReader reader;
  if (const ResultCode opened = reader.open(bytes, kGenerationSubmit, kProcessProtocolVersion,
                                            kMaxProcessMessageBytes);
      opened != ResultCode::Ok) {
    return opened;
  }
  GenerationSubmitMessage message;
  auto& task = message.task;
  std::uint32_t shape = 0;
  std::uint32_t output_count = 0;
  std::uint32_t cancellable = 0;
  task.parameter_schema_version = generate::kGenerationParameterSchemaVersion;
  std::vector<std::uint8_t> masks;
  std::vector<std::uint8_t> references;
  if (!reader.text(1, task.task_id, kMaxId) || !reader.text(2, task.image_id, kMaxId) ||
      !reader.u64(3, task.source_revision) || !reader.text(4, task.tool.tool_id, kMaxId) ||
      !reader.text(5, task.tool.parameter_mode_id, kMaxId) || !reader.u32(6, shape) ||
      !reader.text(7, task.tool.billing_rule_id, kMaxId) || !reader.u32(8, output_count) ||
      !reader.u32(9, cancellable) || cancellable > 1 || !valid_shape(shape) || output_count == 0 ||
      (reader.has(10) && !reader.u32(10, task.parameter_schema_version)) ||
      (reader.has(11) && !reader.blob(11, task.parameter_payload, kMaxTaskPayload)) ||
      (reader.has(12) && !reader.blob(12, masks, kMaxTaskList)) ||
      (reader.has(13) && !reader.blob(13, references, kMaxTaskList)) ||
      (reader.has(14) && !reader.text(14, task.idempotency_key, kMaxId, true))) {
    return ResultCode::StateConflict;
  }
  if ((reader.has(12) && !read_string_list(masks, task.mask_ids, kMaxTaskList)) ||
      (reader.has(13) && !read_string_list(references, task.reference_image_ids, kMaxTaskList))) {
    return ResultCode::StateConflict;
  }
  task.tool.result_shape = static_cast<generate::ResultShape>(shape);
  task.tool.output_count = output_count;
  task.tool.cancellable = cancellable != 0;
  const ResultCode task_validation = generate::validate_task(task);
  if (task_validation != ResultCode::Ok) return task_validation;
  out = std::move(message);
  return ResultCode::Ok;
}

ResultCode encode_generation_result_confirm(const GenerationResultConfirmMessage& message,
                                            std::vector<std::uint8_t>& out) {
  out.clear();
  if (message.protocol_version != kProcessProtocolVersion || !valid_code(static_cast<std::uint32_t>(message.code))) {
    return ResultCode::StateConflict;
  }
  MessageWriter writer(kGenerationResultConfirm, message.protocol_version);
  if (!writer.text(1, message.task_id, kMaxId) || !writer.text(2, message.result_id, kMaxId, true) ||
      !writer.text(3, message.checksum, kMaxId, true)) {
    return ResultCode::StateConflict;
  }
  writer.u32(4, static_cast<std::uint32_t>(message.code));
  return finish(writer, out);
}

ResultCode decode_generation_result_confirm(const std::vector<std::uint8_t>& bytes,
                                            GenerationResultConfirmMessage& out) {
  MessageReader reader;
  if (const ResultCode opened = reader.open(bytes, kGenerationResultConfirm, kProcessProtocolVersion,
                                            kMaxProcessMessageBytes);
      opened != ResultCode::Ok) {
    return opened;
  }
  GenerationResultConfirmMessage message;
  std::uint32_t code = 0;
  if (!reader.text(1, message.task_id, kMaxId) || !reader.text(2, message.result_id, kMaxId, true) ||
      !reader.text(3, message.checksum, kMaxId, true) || !reader.u32(4, code) || !valid_code(code)) {
    return ResultCode::StateConflict;
  }
  message.code = static_cast<ResultCode>(code);
  out = std::move(message);
  return ResultCode::Ok;
}

ResultCode encode_task_cancel(const TaskCancelMessage& message, std::vector<std::uint8_t>& out) {
  out.clear();
  if (message.protocol_version != kProcessProtocolVersion ||
      (message.kind != TaskKind::Export && message.kind != TaskKind::Generation)) {
    return ResultCode::StateConflict;
  }
  MessageWriter writer(kTaskCancel, message.protocol_version);
  writer.u32(1, static_cast<std::uint32_t>(message.kind));
  if (!writer.text(2, message.task_id, kMaxId)) return ResultCode::StateConflict;
  return finish(writer, out);
}

ResultCode decode_task_cancel(const std::vector<std::uint8_t>& bytes, TaskCancelMessage& out) {
  MessageReader reader;
  if (const ResultCode opened = reader.open(bytes, kTaskCancel, kProcessProtocolVersion, kMaxProcessMessageBytes);
      opened != ResultCode::Ok) {
    return opened;
  }
  std::uint32_t kind = 0;
  TaskCancelMessage message;
  if (!reader.u32(1, kind) || (kind != static_cast<std::uint32_t>(TaskKind::Export) &&
                               kind != static_cast<std::uint32_t>(TaskKind::Generation)) ||
      !reader.text(2, message.task_id, kMaxId)) {
    return ResultCode::StateConflict;
  }
  message.kind = static_cast<TaskKind>(kind);
  out = std::move(message);
  return ResultCode::Ok;
}

ResultCode encode_heartbeat(const HeartbeatMessage& message, std::vector<std::uint8_t>& out) {
  out.clear();
  if (message.protocol_version != kProcessProtocolVersion || message.sequence == 0) return ResultCode::StateConflict;
  MessageWriter writer(kHeartbeat, message.protocol_version);
  if (!writer.text(1, message.process, kMaxId)) return ResultCode::StateConflict;
  writer.u64(2, message.sequence);
  writer.u64(3, message.sent_at_ms);
  return finish(writer, out);
}

ResultCode decode_heartbeat(const std::vector<std::uint8_t>& bytes, HeartbeatMessage& out) {
  MessageReader reader;
  if (const ResultCode opened = reader.open(bytes, kHeartbeat, kProcessProtocolVersion, kMaxProcessMessageBytes);
      opened != ResultCode::Ok) {
    return opened;
  }
  HeartbeatMessage message;
  if (!reader.text(1, message.process, kMaxId) || !reader.u64(2, message.sequence) || message.sequence == 0 ||
      !reader.u64(3, message.sent_at_ms)) {
    return ResultCode::StateConflict;
  }
  out = std::move(message);
  return ResultCode::Ok;
}

}  // namespace pixcake::ipc
