#include "pixcake/generated/generation_service.hpp"

#include "pixcake/persist/atomic_file.hpp"
#include "pixcake/support/checksum.hpp"

#include <cstdio>
#include <optional>
#include <string>
#include <utility>

namespace pixcake::generated {
namespace {

std::string hex(std::uint64_t value) {
  char buffer[17];
  std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(value));
  return buffer;
}

}  // namespace

GenerationService::GenerationService(LedgerBook& ledger, FileSystem& files, std::string creative_directory,
                                     RemoteTool& tool)
    : ledger_(ledger), files_(files), creative_directory_(std::move(creative_directory)), tool_(tool) {}

ResultCode GenerationService::cancel(const std::string& task_id) {
  if (task_id.empty() || task_id.find('\0') != std::string::npos) return ResultCode::StateConflict;
  std::lock_guard<std::mutex> lock(state_mutex_);
  if (active_task_id_ == task_id) {
    cancel_requested_.insert(task_id);
    return ResultCode::Ok;
  }
  if (!active_task_id_.empty()) return ResultCode::StateConflict;
  // There is no worker touching this task, so a queued cancellation can be
  // committed immediately.  An active task is handled cooperatively above;
  // this avoids concurrent LedgerBook mutation while the provider call runs.
  return ledger_.cancel(task_id);
}

ResultCode GenerationService::confirm(const ipc::GenerationResultConfirmMessage& message) const {
  const auto valid_text = [](const std::string& value, bool required) {
    return (!required && value.empty()) || (!value.empty() && value.find('\0') == std::string::npos);
  };
  if (!valid_text(message.task_id, true) || !valid_text(message.result_id, false) ||
      !valid_text(message.checksum, false)) {
    return ResultCode::StateConflict;
  }
  std::lock_guard<std::mutex> lock(state_mutex_);
  const auto found = completed_results_.find(message.task_id);
  if (found == completed_results_.end() || found->second.empty()) return ResultCode::StateConflict;
  if (message.code == ResultCode::Ok) {
    if (message.result_id.empty() || message.checksum.empty() || found->second.front().result_id != message.result_id ||
        found->second.front().checksum != message.checksum) {
      return ResultCode::StateConflict;
    }
    // The main process has durably placed the result.  The generated process
    // no longer needs to retain the acknowledgement entry.
    completed_results_.erase(found);
    return ResultCode::Ok;
  }

  // Placement can fail after the provider has already written its result
  // files (for example because the layer document became stale).  The main
  // process explicitly confirms that failure, so reclaim the files here and
  // keep creative/ from accumulating orphaned pixels.
  for (const auto& result : found->second) files_.remove_file(result.pixel_file);
  completed_results_.erase(found);
  return ResultCode::Ok;
}

ipc::GenerationProgressMessage GenerationService::run(const generate::GenerationTask& task) {
  ipc::GenerationProgressMessage progress;
  progress.task_id = task.task_id;
  const auto failed = [&](ResultCode code, bool record) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    for (const auto& result : progress.results) files_.remove_file(result.pixel_file);
    if (record) ledger_.fail(task.task_id);
    if (active_task_id_ == task.task_id) active_task_id_.clear();
    progress.state = generate::TaskState::Failed;
    progress.code = code;
    return progress;
  };
  if (const ResultCode validation = generate::validate_task(task); validation != ResultCode::Ok) {
    return failed(validation, false);
  }
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    active_task_id_ = task.task_id;
  }
  const auto clear_active = [&] {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (active_task_id_ == task.task_id) active_task_id_.clear();
  };
  if (ledger_.ledger().task_status(task.task_id) == "排队取消") {
    clear_active();
    progress.state = generate::TaskState::CancelledQueued;
    progress.code = ResultCode::Cancelled;
    return progress;
  }
  if (ledger_.ledger().task_status(task.task_id) != "排队中") {
    clear_active();
    return failed(ResultCode::StateConflict, false);
  }

  const auto take_cancel = [&] {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return cancel_requested_.erase(task.task_id) != 0;
  };
  if (take_cancel()) {
    const ResultCode cancelled = ledger_.cancel(task.task_id);
    clear_active();
    progress.state = cancelled == ResultCode::Ok ? generate::TaskState::CancelledQueued
                                                   : generate::TaskState::Failed;
    progress.code = cancelled == ResultCode::Ok ? ResultCode::Cancelled : cancelled;
    return progress;
  }
  if (const ResultCode started = ledger_.start_generating(task.task_id); started != ResultCode::Ok) {
    clear_active();
    return failed(started, false);
  }
  const RemoteReply reply = tool_.run(task);
  const auto cancelled_progress = [&]() -> std::optional<ipc::GenerationProgressMessage> {
    if (!take_cancel()) return std::nullopt;
    for (const auto& result : progress.results) files_.remove_file(result.pixel_file);
    const ResultCode cancelled = ledger_.cancel(task.task_id);
    clear_active();
    // 已被主进程取消：晚到的结果丢弃，不第二次记账。
    progress.state = generate::TaskState::CancelledRunning;
    progress.code = cancelled == ResultCode::Ok ? ResultCode::Cancelled : cancelled;
    return progress;
  };
  if (const auto cancelled = cancelled_progress(); cancelled.has_value()) return *cancelled;
  if (ledger_.ledger().task_status(task.task_id) != "生成中") {
    // 账本恢复或外部终止已改变状态：晚到的结果丢弃，不第二次记账。
    clear_active();
    progress.state = generate::TaskState::CancelledRunning;
    progress.code = ResultCode::Cancelled;
    return progress;
  }
  if (reply.code != ResultCode::Ok) {
    return failed(reply.code, true);
  }
  if (reply.results.size() != task.tool.output_count) {
    return failed(ResultCode::ImageCorrupt, true);
  }

  for (std::size_t index = 0; index < reply.results.size(); ++index) {
    if (const auto cancelled = cancelled_progress(); cancelled.has_value()) return *cancelled;
    const auto& remote = reply.results[index];
    // 远端给的是 RGBA8 像素：字节数必须正好是 宽 × 高 × 4（测试清单 T-03“尺寸不符”）。
    const std::uint64_t expected_bytes = static_cast<std::uint64_t>(remote.width) * remote.height * kBytesPerPixel;
    if (remote.image.empty() || remote.width == 0 || remote.height == 0 || remote.image.size() != expected_bytes ||
        remote.continuation_parameters.size() > kMaxContinuationParameters) {
      return failed(ResultCode::ImageCorrupt, true);
    }

    generate::GenerationResult result;
    result.task_id = task.task_id;
    result.result_id = task.task_id + "-" + std::to_string(index + 1);
    result.shape = task.tool.result_shape;
    result.ordinal = static_cast<std::uint32_t>(index);
    result.pixel_file = creative_directory_ + "/" + result.result_id + ".result";
    result.checksum = hex(checksum_fnv1a64(remote.image.data(), remote.image.size()));
    result.continuation_parameters = remote.continuation_parameters;
    result.width = remote.width;
    result.height = remote.height;
    if (generate::validate_result(task, result) != ResultCode::Ok) {
      return failed(ResultCode::StateConflict, true);
    }
    if (const ResultCode written = atomic_replace(files_, result.pixel_file, remote.image); written != ResultCode::Ok) {
      return failed(written, true);
    }

    ipc::GenerationResultInfo info;
    info.ordinal = result.ordinal;
    info.result_id = result.result_id;
    info.pixel_file = result.pixel_file;
    info.checksum = result.checksum;
    info.width = result.width;
    info.height = result.height;
    info.continuation_parameters = result.continuation_parameters;
    progress.results.push_back(std::move(info));
  }

  if (const auto cancelled = cancelled_progress(); cancelled.has_value()) return *cancelled;
  if (const ResultCode ready = ledger_.succeed(task.task_id); ready != ResultCode::Ok) {
    clear_active();
    return failed(ready, false);
  }
  clear_active();
  progress.state = generate::TaskState::Succeeded;
  progress.percent = 100;
  if (!progress.results.empty()) {
    progress.result_id = progress.results.front().result_id;
    progress.pixel_file = progress.results.front().pixel_file;
    progress.checksum = progress.results.front().checksum;
  }
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    completed_results_[task.task_id] = progress.results;
  }
  return progress;
}

}  // namespace pixcake::generated
