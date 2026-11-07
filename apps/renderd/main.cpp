#include "pixcake/renderd/preview_server.hpp"
#include "pixcake/renderd/render_service.hpp"
#include "pixcake/logging/process_log.hpp"

#ifdef PIXCAKE_HAS_INFERENCE
#include "pixcake/renderd/portrait_inference.hpp"
#endif

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>
#include <unistd.h>

namespace {

// The preview service's semantic segmenter, and a one-line status for --health.
std::shared_ptr<pixcake::renderd::SemanticSegmenter> make_segmenter(std::string& status) {
#ifdef PIXCAKE_HAS_INFERENCE
  std::shared_ptr<pixcake::renderd::SemanticSegmenter> segmenter;
  std::string diagnostic;
  const std::string directory = pixcake::renderd::model_directory_near_executable(PIXCAKE_DEFAULT_MODEL_DIR);
  const auto code = pixcake::renderd::create_portrait_segmenter(directory, 4, segmenter, diagnostic);
  status = code == pixcake::ResultCode::Ok
               ? "semantic segmentation available; models " + directory
               : std::string("semantic segmentation unavailable (") + pixcake::result_code_label(code) + "): " +
                     diagnostic;
  return segmenter;
#else
  status = "semantic segmentation unavailable: inference not built (PIXCAKE_ENABLE_INFERENCE=OFF)";
  return nullptr;
#endif
}

// Listens at `socket_path`, serves the one shell that launched this process,
// and exits when that connection ends (the shell starts a new renderd if it
// needs one).  Exit codes: 0 session ended, 4 no shell connected in time,
// 5 the socket could not be opened, 6 the channel broke.
int serve_preview(const std::string& socket_path, pixcake::LogSink* log) {
  namespace channel = pixcake::preview_channel;
  std::string diagnostic;
  channel::UniqueFd listener;
  if (channel::listen_at(socket_path, listener, diagnostic) != pixcake::ResultCode::Ok) {
    std::cerr << "pixcake_app_renderd: " << diagnostic << '\n';
    return 5;
  }
  std::string segmentation;
  pixcake::renderd::RenderService service(make_segmenter(segmentation));
  auto decoder = pixcake::renderd::make_system_image_decoder();
  pixcake::renderd::PreviewServer server(service, decoder.get(), log);
  if (log != nullptr) {
    pixcake::LogRecord record;
    record.module = "renderd";
    record.event = "renderd.listen";
    record.path = socket_path;
    record.message = segmentation;
    log->write(record);
  }
  channel::UniqueFd connection;
  const auto accepted = channel::accept_same_user(listener.get(), 30'000, connection, diagnostic);
  listener.reset();
  ::unlink(socket_path.c_str());
  if (accepted != pixcake::ResultCode::Ok) {
    std::cerr << "pixcake_app_renderd: " << diagnostic << '\n';
    return 4;
  }
  channel::Channel session{std::move(connection)};
  const auto served = server.serve(session, diagnostic);
  if (served != pixcake::ResultCode::Ok) {
    std::cerr << "pixcake_app_renderd: " << diagnostic << '\n';
    return 6;
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const auto arguments = pixcake::logging::parse_process_arguments(argc, argv);
  if (!arguments) {
    std::cerr << "usage: pixcake_app_renderd [--health] [--log-dir <dir>] [--socket <path>]\n";
    return 2;
  }
  // 需求 §3.8：诊断日志写到应用支持目录的 logs/；内核只拿到 LogSink，不决定位置。
  pixcake::logging::ProcessLog log(arguments->log_directory, "renderd");
  if (log.sink() == nullptr) {
    std::cerr << "pixcake_app_renderd: log directory unavailable (" << pixcake::result_code_label(log.code())
              << "): " << arguments->log_directory << '\n';
  }
  if (arguments->health) {
    std::string segmentation;
    pixcake::renderd::RenderService service(make_segmenter(segmentation));
    std::cout << "renderd boundary ready; pending=" << (service.has_pending() ? "1" : "0") << '\n';
    std::cout << segmentation << '\n';
    return log.sink() == nullptr ? 3 : 0;
  }
  if (arguments->socket_path.empty()) {
    std::cerr << "pixcake_app_renderd is a process boundary. Use --health for a self-check or --socket <path>.\n";
    return log.sink() == nullptr ? 3 : 0;
  }
  return serve_preview(arguments->socket_path, log.sink());
}
