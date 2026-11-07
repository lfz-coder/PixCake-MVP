#include "pixcake/logging/process_log.hpp"

#include <string_view>

namespace pixcake::logging {

std::optional<ProcessArguments> parse_process_arguments(int argc, char** argv) {
  ProcessArguments parsed;
  bool directory_given = false;
  for (int i = 1; i < argc; ++i) {
    const std::string_view argument(argv[i]);
    if (argument == "--health") {
      parsed.health = true;
    } else if (argument == "--log-dir" && i + 1 < argc && !directory_given) {
      parsed.log_directory = argv[++i];
      directory_given = true;
    } else if (argument == "--socket" && i + 1 < argc && parsed.socket_path.empty() && argv[i + 1][0] != '\0') {
      parsed.socket_path = argv[++i];
    } else {
      return std::nullopt;
    }
  }
  if (!directory_given) parsed.log_directory = default_log_directory();
  return parsed;
}

ProcessLog::ProcessLog(const std::string& log_directory, std::string process) : process_(std::move(process)) {
  sink_ = FileLogSink::open(log_directory, process_, code_);
  note("process.start");
}

ProcessLog::~ProcessLog() { note("process.exit"); }

void ProcessLog::note(const char* event) const {
  if (!sink_) return;
  LogRecord record;
  record.module = "process";
  record.event = event;
  record.message = process_;
  sink_->write(record);
}

}  // namespace pixcake::logging
