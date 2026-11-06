#pragma once

#include "pixcake/support/log_sink.hpp"
#include "pixcake/support/result_code.hpp"

#include <cstdio>
#include <memory>
#include <mutex>
#include <string>

namespace pixcake::logging {

// 进程入口用的日志文件：<directory>/<process_name>.log，一条记录一行，追加写入。
// 行格式（制表符分隔）：UTC 时间、进程、模块、事件、结果码、任务编号、文档标识、修订号、
// 路径、阶段、耗时毫秒、是否去重、是否扣点、说明。字段里的制表符和换行替换成空格。
// 只写 LogRecord 里有的字段，不接受像素或文档正文（运维清单 O-02、O-03）。
class FileLogSink : public LogSink {
 public:
  static std::unique_ptr<FileLogSink> open(const std::string& directory, std::string process_name,
                                           ResultCode& code);
  ~FileLogSink() override;

  FileLogSink(const FileLogSink&) = delete;
  FileLogSink& operator=(const FileLogSink&) = delete;

  void write(const LogRecord& record) override;

 private:
  FileLogSink(std::FILE* file, std::string process_name);

  std::FILE* file_;
  std::string process_name_;
  std::mutex mutex_;
};

// 需求 §3.8：应用支持目录下的 logs/。macOS 为 $HOME/Library/Application Support/PixCake/logs；
// 拿不到 HOME 时返回空字符串，由调用方报告日志不可用。
std::string default_log_directory();

}  // namespace pixcake::logging
