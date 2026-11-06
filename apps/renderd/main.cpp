#include "pixcake/renderd/render_service.hpp"
#include "pixcake/logging/process_log.hpp"

#include <iostream>

int main(int argc, char** argv) {
  const auto arguments = pixcake::logging::parse_process_arguments(argc, argv);
  if (!arguments) {
    std::cerr << "usage: pixcake_app_renderd [--health] [--log-dir <dir>]\n";
    return 2;
  }
  // 需求 §3.8：诊断日志写到应用支持目录的 logs/；内核只拿到 LogSink，不决定位置。
  pixcake::logging::ProcessLog log(arguments->log_directory, "renderd");
  if (log.sink() == nullptr) {
    std::cerr << "pixcake_app_renderd: log directory unavailable (" << pixcake::result_code_label(log.code())
              << "): " << arguments->log_directory << '\n';
  }
  if (arguments->health) {
    pixcake::renderd::RenderService service;
    std::cout << "renderd boundary ready; pending=" << (service.has_pending() ? "1" : "0") << '\n';
    return log.sink() == nullptr ? 3 : 0;
  }
  std::cerr << "pixcake_app_renderd is a process boundary. Use --health for a self-check.\n";
  return log.sink() == nullptr ? 3 : 0;
}
