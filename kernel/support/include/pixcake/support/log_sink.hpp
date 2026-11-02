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
};

class LogSink {
 public:
  virtual ~LogSink() = default;
  virtual void write(const LogRecord& record) = 0;
};

}  // namespace pixcake
