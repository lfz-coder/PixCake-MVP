#pragma once

#include "pixcake/generate/generation_task.hpp"
#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pixcake::ipc {

// 进程间消息的唯一定义（契约 C-30、质量清单 Q-25，架构第 354 行）：预览请求与结果、导出进度、
// 生成提交/进度/结果确认、取消、心跳。四个进程都用这里的结构，不各写一套字段。编码见 src/message_codec.hpp。
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
  // 失败时给用户看的原因（需求第 19 节：失败要保留原因）；成功时为空。
  std::string reason;
};

// 壳层 → 导出进程：开始一个已排进导出队列的任务。任务本身（文档快照、目标路径、设置）在
// 导出队列的事件里，消息只带编号和壳层解析书签后得到的原图路径。
struct ExportStartMessage {
  std::uint32_t protocol_version = kProcessProtocolVersion;
  std::string export_id;
  std::string source_path;
};

// A generated pixel result written by apps/generated.  The progress message
// carries a vector for multi-output tools; the legacy scalar fields below are
// kept as a mirror of the first item for older process readers.
struct GenerationResultInfo {
  std::uint32_t ordinal = 0;
  std::string result_id;
  std::string pixel_file;
  std::string checksum;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint8_t> continuation_parameters;
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
  std::vector<GenerationResultInfo> results;
};

// 主进程 → generated：提交一个已经报价并排队的生成任务。任务字段复用
// kernel/generate 的 provider-neutral 契约；供应商 JSON 不进入 IPC。
struct GenerationSubmitMessage {
  std::uint32_t protocol_version = kProcessProtocolVersion;
  generate::GenerationTask task;
};

// 主进程 → generated：确认主进程已经验证并落层（或拒绝）结果。确认消息使生成进程
// 可以区分“结果文件已写好”和“结果已经进入图层文档”。
struct GenerationResultConfirmMessage {
  std::uint32_t protocol_version = kProcessProtocolVersion;
  std::string task_id;
  std::string result_id;
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
ResultCode encode_export_start(const ExportStartMessage& message, std::vector<std::uint8_t>& out);
ResultCode decode_export_start(const std::vector<std::uint8_t>& bytes, ExportStartMessage& out);
// The message kind in the envelope, so a reader can dispatch before decoding.
bool is_export_start(const std::vector<std::uint8_t>& bytes);
bool is_export_progress(const std::vector<std::uint8_t>& bytes);
bool is_generation_progress(const std::vector<std::uint8_t>& bytes);
bool is_generation_submit(const std::vector<std::uint8_t>& bytes);
bool is_generation_result_confirm(const std::vector<std::uint8_t>& bytes);
bool is_task_cancel(const std::vector<std::uint8_t>& bytes);
ResultCode encode_generation_progress(const GenerationProgressMessage& message, std::vector<std::uint8_t>& out);
ResultCode decode_generation_progress(const std::vector<std::uint8_t>& bytes, GenerationProgressMessage& out);
ResultCode encode_generation_submit(const GenerationSubmitMessage& message, std::vector<std::uint8_t>& out);
ResultCode decode_generation_submit(const std::vector<std::uint8_t>& bytes, GenerationSubmitMessage& out);
ResultCode encode_generation_result_confirm(const GenerationResultConfirmMessage& message,
                                            std::vector<std::uint8_t>& out);
ResultCode decode_generation_result_confirm(const std::vector<std::uint8_t>& bytes,
                                            GenerationResultConfirmMessage& out);
ResultCode encode_task_cancel(const TaskCancelMessage& message, std::vector<std::uint8_t>& out);
ResultCode decode_task_cancel(const std::vector<std::uint8_t>& bytes, TaskCancelMessage& out);
ResultCode encode_heartbeat(const HeartbeatMessage& message, std::vector<std::uint8_t>& out);
ResultCode decode_heartbeat(const std::vector<std::uint8_t>& bytes, HeartbeatMessage& out);

}  // namespace pixcake::ipc
