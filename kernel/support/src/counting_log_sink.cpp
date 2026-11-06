#include "pixcake/support/counting_log_sink.hpp"

namespace pixcake {

void CountingLogSink::write(const LogRecord& record) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++counts_[{record.event, record.code}];
  }
  write_log(forward_, record);
}

std::uint64_t CountingLogSink::count(std::string_view event) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::uint64_t total = 0;
  for (const auto& entry : counts_) {
    if (entry.first.first == event) total += entry.second;
  }
  return total;
}

std::uint64_t CountingLogSink::count(std::string_view event, ResultCode code) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = counts_.find({std::string(event), code});
  return found == counts_.end() ? 0 : found->second;
}

}  // namespace pixcake
