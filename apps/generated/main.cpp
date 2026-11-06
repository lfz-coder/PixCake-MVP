#include "pixcake/generated/generation_service.hpp"
#include "pixcake/logging/process_log.hpp"

#include <iostream>

int main(int argc, char** argv) {
  const auto arguments = pixcake::logging::parse_process_arguments(argc, argv);
  if (!arguments) {
    std::cerr << "usage: pixcake_app_generated [--health] [--log-dir <dir>]\n";
    return 2;
  }
  // 需求 §3.8：诊断日志写到应用支持目录的 logs/；内核只拿到 LogSink，不决定位置。
  pixcake::logging::ProcessLog log(arguments->log_directory, "generated");
  if (log.sink() == nullptr) {
    std::cerr << "pixcake_app_generated: log directory unavailable (" << pixcake::result_code_label(log.code())
              << "): " << arguments->log_directory << '\n';
  }
  if (arguments->health) {
    std::cout << "generated boundary ready\n";
    return log.sink() == nullptr ? 3 : 0;
  }
  std::cerr << "pixcake_app_generated is a process boundary. Use --health for a self-check.\n";
  return log.sink() == nullptr ? 3 : 0;
}
