#include "pixcake/ipc/process_messages.hpp"

#include "message_codec.hpp"

namespace pixcake::ipc {
namespace {

using detail::MessageReader;
using detail::MessageWriter;

constexpr std::uint32_t kExportProgress = 4;
constexpr std::uint32_t kGenerationProgress = 5;
constexpr std::uint32_t kTaskCancel = 6;
constexpr std::uint32_t kHeartbeat = 7;
constexpr std::uint32_t kExportStart = 12;
constexpr std::size_t kMaxId = 1024;
constexpr std::size_t kMaxText = 4096;

bool valid_code(std::uint32_t value) { return value <= static_cast<std::uint32_t>(ResultCode::IoFailed); }

bool valid_state(std::uint32_t value) {
  return value <= static_cast<std::uint32_t>(generate::TaskState::CancelledRunning);
}

ResultCode finish(const MessageWriter& writer, std::vector<std::uint8_t>& out) {
  out = writer.bytes();
  return out.size() <= kMaxProcessMessageBytes ? ResultCode::Ok : ResultCode::StateConflict;
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
bool is_task_cancel(const std::vector<std::uint8_t>& bytes) { return kind_is(bytes, kTaskCancel); }

ResultCode encode_generation_progress(const GenerationProgressMessage& message, std::vector<std::uint8_t>& out) {
  out.clear();
  if (message.protocol_version != kProcessProtocolVersion || message.percent > 100) return ResultCode::StateConflict;
  MessageWriter writer(kGenerationProgress, message.protocol_version);
  if (!writer.text(1, message.task_id, kMaxId) || !writer.text(4, message.result_id, kMaxId, true) ||
      !writer.text(5, message.pixel_file, kMaxText, true) || !writer.text(6, message.checksum, kMaxId, true)) {
    return ResultCode::StateConflict;
  }
  writer.u32(2, static_cast<std::uint32_t>(message.state));
  writer.u32(3, message.percent);
  writer.u32(7, static_cast<std::uint32_t>(message.code));
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
  GenerationProgressMessage message;
  if (!reader.text(1, message.task_id, kMaxId) || !reader.u32(2, state) || !valid_state(state) ||
      !reader.u32(3, message.percent) || message.percent > 100 || !reader.text(4, message.result_id, kMaxId, true) ||
      !reader.text(5, message.pixel_file, kMaxText, true) || !reader.text(6, message.checksum, kMaxId, true) ||
      !reader.u32(7, code) || !valid_code(code)) {
    return ResultCode::StateConflict;
  }
  message.state = static_cast<generate::TaskState>(state);
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
