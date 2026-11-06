#pragma once

#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <string>

namespace pixcake {

// 日志可以记路径和结果码。这里没有像素缓冲区，也不接受整份精修文档。
struct LogRecord {
  ResultCode code = ResultCode::Ok;
  std::string message;
  std::string path;
  std::uint64_t revision = 0;
  std::string capability_id;
  std::string phase;
  std::uint64_t duration_ms = 0;
  bool deduplicated = false;
  // 下面几项让日志能按模块和事件数出来（运维清单 O-01、O-06–O-12）。
  std::string module;       // 写记录的模块，如 "persist"、"exportq"
  std::string event;        // "模块.动作"，如 "persist.write.start"
  std::string task_id;      // 导出编号或生成任务编号
  std::string document_id;  // 文档标识；精修文档用正式文件路径
  bool charged = false;     // 生成任务终态时是否已经扣点
};


class LogSink {
 public:
  virtual ~LogSink() = default;
  virtual void write(const LogRecord& record) = 0;
};

// sink 可以为空：为空时什么都不做。
inline void write_log(LogSink* sink, const LogRecord& record) {
  if (sink != nullptr) sink->write(record);
}

}  // namespace pixcake
