#pragma once

#include "pixcake/generate/generation_task.hpp"
#include "pixcake/ipc/process_messages.hpp"
#include "pixcake/ledger/ledger_book.hpp"
#include "pixcake/persist/file_system.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pixcake::generated {

// 远端生成工具的回答。生成进程按工具编号调用远程服务（架构第 505 行）；超时、断网由调用方映射为
// NetworkFailed。像素是 RGBA8（每像素 4 字节）；没有像素、尺寸为 0、字节数与宽高不符都视为坏返回。
inline constexpr std::uint64_t kBytesPerPixel = 4;

struct RemoteReply {
  ResultCode code = ResultCode::Ok;
  std::vector<std::uint8_t> image;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

class RemoteTool {
 public:
  virtual ~RemoteTool() = default;
  virtual RemoteReply run(const generate::GenerationTask& task) = 0;
};

// 生成进程的服务（架构第 507–511 行）：读取主进程已排队的任务，进入“生成中”，等远端结果，
// 把结果像素写进 creative/，再追加“成功”。不链接 Qt 和图形接口，也不碰图层文档；落层由主进程做。
// 任务在等待中被取消时，晚到的结果不写文件、不追加就绪，点不退。
class GenerationService {
 public:
  GenerationService(LedgerBook& ledger, FileSystem& files, std::string creative_directory, RemoteTool& tool);

  // 返回给主进程的生成进度（kernel/ipc 的消息结构，不另定义字段）。
  ipc::GenerationProgressMessage run(const generate::GenerationTask& task);

 private:
  LedgerBook& ledger_;
  FileSystem& files_;
  std::string creative_directory_;
  RemoteTool& tool_;
};

}  // namespace pixcake::generated
