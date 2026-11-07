#include "pixcake/renderd/render_service.hpp"
#include "pixcake/logging/process_log.hpp"

#ifdef PIXCAKE_HAS_INFERENCE
#include "pixcake/renderd/portrait_inference.hpp"
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>

namespace {

#ifdef PIXCAKE_HAS_INFERENCE
// Inside an app bundle the models ship in Contents/Resources/models next to
// Contents/MacOS/<this executable>; a development build uses the directory the
// configure step downloaded and verified them into.
std::string model_directory() {
#ifdef __APPLE__
  char buffer[4096];
  std::uint32_t size = sizeof(buffer);
  if (_NSGetExecutablePath(buffer, &size) == 0) {
    std::error_code error;
    const auto executable = std::filesystem::weakly_canonical(buffer, error);
    if (!error) {
      const auto bundled = executable.parent_path().parent_path() / "Resources" / "models";
      if (std::filesystem::is_directory(bundled, error)) return bundled.string();
    }
  }
#endif
  return PIXCAKE_DEFAULT_MODEL_DIR;
}
#endif

// The preview service's semantic segmenter, and a one-line status for --health.
std::shared_ptr<pixcake::renderd::SemanticSegmenter> make_segmenter(std::string& status) {
#ifdef PIXCAKE_HAS_INFERENCE
  std::shared_ptr<pixcake::renderd::SemanticSegmenter> segmenter;
  std::string diagnostic;
  const std::string directory = model_directory();
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

}  // namespace

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
    std::string segmentation;
    pixcake::renderd::RenderService service(make_segmenter(segmentation));
    std::cout << "renderd boundary ready; pending=" << (service.has_pending() ? "1" : "0") << '\n';
    std::cout << segmentation << '\n';
    return log.sink() == nullptr ? 3 : 0;
  }
  std::cerr << "pixcake_app_renderd is a process boundary. Use --health for a self-check.\n";
  return log.sink() == nullptr ? 3 : 0;
}
