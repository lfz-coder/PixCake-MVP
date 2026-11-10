#pragma once

#include "pixcake/generated/remote_tool.hpp"
#include "pixcake/ipc/process_messages.hpp"
#include "pixcake/ledger/ledger_book.hpp"
#include "pixcake/persist/file_system.hpp"

#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace pixcake::generated {

// 生成进程的服务（架构第 507–511 行）：读取主进程已排队的任务，进入“生成中”，等远端结果，
// 把结果像素写进 creative/，再追加“成功”。不链接 Qt 和图形接口，也不碰图层文档；落层由主进程做。
// 任务在等待中被取消时，晚到的结果不写文件、不追加就绪，点不退。
class GenerationService {
 public:
  GenerationService(LedgerBook& ledger, FileSystem& files, std::string creative_directory, RemoteTool& tool);

  // 返回给主进程的生成进度（kernel/ipc 的消息结构，不另定义字段）。
  ipc::GenerationProgressMessage run(const generate::GenerationTask& task);

  // The main process owns cancellation and result confirmation.  These small
  // entry points let the generated IPC server apply those messages without
  // exposing the ledger or layer document to the transport layer.
  ResultCode cancel(const std::string& task_id);
  ResultCode confirm(const ipc::GenerationResultConfirmMessage& message) const;

 private:
  LedgerBook& ledger_;
  FileSystem& files_;
  std::string creative_directory_;
  RemoteTool& tool_;
  mutable std::mutex state_mutex_;
  std::set<std::string> cancel_requested_;
  mutable std::map<std::string, std::vector<ipc::GenerationResultInfo>> completed_results_;
  std::string active_task_id_;
};

}  // namespace pixcake::generated
