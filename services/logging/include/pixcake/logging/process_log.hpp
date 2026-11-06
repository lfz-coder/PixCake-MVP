#pragma once

#include "pixcake/logging/file_log_sink.hpp"

#include <memory>
#include <optional>
#include <string>

namespace pixcake::logging {

// 进程入口的命令行：--health 和 --log-dir <目录> 顺序不限。认不出的参数让 parsed 为空。
struct ProcessArguments {
  bool health = false;
  std::string log_directory;  // 没给时为 default_log_directory()
};
std::optional<ProcessArguments> parse_process_arguments(int argc, char** argv);

// 打开 <log_directory>/<process>.log，并写一条 process.start；析构时写 process.exit。
// 打不开时 sink() 为空，code() 说明原因，进程照常运行但要报告日志不可用。
class ProcessLog {
 public:
  ProcessLog(const std::string& log_directory, std::string process);
  ~ProcessLog();
  ProcessLog(const ProcessLog&) = delete;
  ProcessLog& operator=(const ProcessLog&) = delete;

  LogSink* sink() const { return sink_.get(); }
  ResultCode code() const { return code_; }

 private:
  void note(const char* event) const;

  std::string process_;
  ResultCode code_ = ResultCode::Ok;
  std::unique_ptr<FileLogSink> sink_;
};

}  // namespace pixcake::logging
