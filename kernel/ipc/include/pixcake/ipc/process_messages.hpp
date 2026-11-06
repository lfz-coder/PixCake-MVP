#pragma once

#include "pixcake/generate/generation_task.hpp"
#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pixcake::ipc {

// 进程间消息的唯一定义（契约 C-30、质量清单 Q-25，架构第 354 行）：预览请求与结果、导出进度、
// 生成进度、取消、心跳。四个进程都用这里的结构，不各写一套字段。编码见 src/message_codec.hpp。
inline constexpr std::uint32_t kEnvelopeRecord = 0x10000;
inline constexpr std::uint32_t kProcessProtocolVersion = 1;
inline constexpr std::size_t kMaxProcessMessageBytes = 64 * 1024;

// 导出进度。status 是导出队列的状态文字（kernel/exportq），不另起一套状态。
struct ExportProgressMessage {
  std::uint32_t protocol_version = kProcessProtocolVersion;
  std::string export_id;
  std::string status;
  std::uint32_t completed = 0;
  std::uint32_t total = 0;
  ResultCode code = ResultCode::Ok;
};

// 生成进度。state 是 kernel/generate 的任务状态；结果文件与校验和随成功一起带回。
struct GenerationProgressMessage {
  std::uint32_t protocol_version = kProcessProtocolVersion;
  std::string task_id;
  generate::TaskState state = generate::TaskState::Queued;
  std::uint32_t percent = 0;
  std::string result_id;
  std::string pixel_file;
  std::string checksum;
  ResultCode code = ResultCode::Ok;
};

enum class TaskKind : std::uint32_t { Export = 1, Generation = 2 };

struct TaskCancelMessage {
  std::uint32_t protocol_version = kProcessProtocolVersion;
  TaskKind kind = TaskKind::Generation;
  std::string task_id;
};

struct HeartbeatMessage {
  std::uint32_t protocol_version = kProcessProtocolVersion;
  std::string process;
  std::uint64_t sequence = 0;
  std::uint64_t sent_at_ms = 0;
};

ResultCode encode_export_progress(const ExportProgressMessage& message, std::vector<std::uint8_t>& out);
ResultCode decode_export_progress(const std::vector<std::uint8_t>& bytes, ExportProgressMessage& out);
ResultCode encode_generation_progress(const GenerationProgressMessage& message, std::vector<std::uint8_t>& out);
ResultCode decode_generation_progress(const std::vector<std::uint8_t>& bytes, GenerationProgressMessage& out);
ResultCode encode_task_cancel(const TaskCancelMessage& message, std::vector<std::uint8_t>& out);
ResultCode decode_task_cancel(const std::vector<std::uint8_t>& bytes, TaskCancelMessage& out);
ResultCode encode_heartbeat(const HeartbeatMessage& message, std::vector<std::uint8_t>& out);
ResultCode decode_heartbeat(const std::vector<std::uint8_t>& bytes, HeartbeatMessage& out);

}  // namespace pixcake::ipc
