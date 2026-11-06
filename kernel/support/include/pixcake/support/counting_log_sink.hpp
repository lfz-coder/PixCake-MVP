#pragma once

#include "pixcake/support/log_sink.hpp"

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace pixcake {

// 本机运行时计数：按事件名和结果码累计，再把记录原样转发给下游（可以为空）。
// 运维清单第 2 节的指标就从这里数出来，不需要远程仪表盘。
class CountingLogSink : public LogSink {
 public:
  explicit CountingLogSink(LogSink* forward = nullptr) : forward_(forward) {}

  void write(const LogRecord& record) override;
  std::uint64_t count(std::string_view event) const;
  std::uint64_t count(std::string_view event, ResultCode code) const;

 private:
  LogSink* forward_;
  mutable std::mutex mutex_;
  std::map<std::pair<std::string, ResultCode>, std::uint64_t> counts_;
};

}  // namespace pixcake
