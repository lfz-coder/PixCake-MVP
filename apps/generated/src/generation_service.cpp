#include "pixcake/generated/generation_service.hpp"

#include "pixcake/persist/atomic_file.hpp"
#include "pixcake/support/checksum.hpp"

#include <cstdio>
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

ipc::GenerationProgressMessage GenerationService::run(const generate::GenerationTask& task) {
  ipc::GenerationProgressMessage progress;
  progress.task_id = task.task_id;
  const auto failed = [&](ResultCode code, bool record) {
    if (record) ledger_.fail(task.task_id);
    progress.state = generate::TaskState::Failed;
    progress.code = code;
    return progress;
  };
  if (generate::validate_task(task) != ResultCode::Ok || ledger_.ledger().task_status(task.task_id) != "排队中") {
    return failed(ResultCode::StateConflict, false);
  }
  if (const ResultCode started = ledger_.start_generating(task.task_id); started != ResultCode::Ok) {
    return failed(started, false);
  }
  const RemoteReply reply = tool_.run(task);
  if (ledger_.ledger().task_status(task.task_id) != "生成中") {
    // 已被主进程取消：晚到的结果丢弃，不第二次记账。
    progress.state = generate::TaskState::CancelledRunning;
    progress.code = ResultCode::Cancelled;
    return progress;
  }
  if (reply.code != ResultCode::Ok) return failed(reply.code, true);
  // 远端给的是 RGBA8 像素：字节数必须正好是 宽 × 高 × 4（测试清单 T-03“尺寸不符”）。
  const std::uint64_t expected_bytes = static_cast<std::uint64_t>(reply.width) * reply.height * kBytesPerPixel;
  if (reply.image.empty() || reply.width == 0 || reply.height == 0 || reply.image.size() != expected_bytes) {
    return failed(ResultCode::ImageCorrupt, true);
  }

  generate::GenerationResult result;
  result.task_id = task.task_id;
  result.result_id = task.task_id + "-1";
  result.shape = task.tool.result_shape;
  result.pixel_file = creative_directory_ + "/" + result.result_id + ".result";
  result.checksum = hex(checksum_fnv1a64(reply.image.data(), reply.image.size()));
  result.width = reply.width;
  result.height = reply.height;
  if (generate::validate_result(task, result) != ResultCode::Ok) return failed(ResultCode::StateConflict, true);
  if (const ResultCode written = atomic_replace(files_, result.pixel_file, reply.image); written != ResultCode::Ok) {
    return failed(written, true);
  }
  if (const ResultCode ready = ledger_.succeed(task.task_id); ready != ResultCode::Ok) {
    files_.remove_file(result.pixel_file);
    return failed(ready, false);
  }
  progress.state = generate::TaskState::Succeeded;
  progress.percent = 100;
  progress.result_id = result.result_id;
  progress.pixel_file = result.pixel_file;
  progress.checksum = result.checksum;
  return progress;
}

}  // namespace pixcake::generated
