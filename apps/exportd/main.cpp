#include "pixcake/exportd/export_server.hpp"
#include "pixcake/exportd/export_service.hpp"
#include "pixcake/logging/process_log.hpp"
#include "pixcake/persist/file_system.hpp"
#include "pixcake/preview_channel/channel.hpp"
#include "pixcake/storage/sqlite_export_log.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <iostream>
#include <memory>
#include <string>

namespace {

namespace channel = pixcake::preview_channel;

// Exit codes: 2 usage, 3 log unavailable (health), 4 no shell connected,
// 5 socket or export log unavailable, 6 the session broke.
int serve_exports(const std::string& socket_path, const std::string& data_directory, pixcake::LogSink* log) {
  // The shell owns <data>/exports; it creates the directory before starting us.
  const std::string exports = data_directory + "/exports";
  ::mkdir(exports.c_str(), 0700);
  std::unique_ptr<pixcake::SqliteExportLog> export_log;
  if (pixcake::SqliteExportLog::open(exports + "/export.db", export_log) != pixcake::ResultCode::Ok) {
    std::cerr << "pixcake_app_exportd: export log unavailable: " << exports << "/export.db\n";
    return 5;
  }
  std::string diagnostic;
  channel::UniqueFd listener;
  if (channel::listen_at(socket_path, listener, diagnostic) != pixcake::ResultCode::Ok) {
    std::cerr << "pixcake_app_exportd: " << diagnostic << '\n';
    return 5;
  }
  channel::UniqueFd connection;
  const auto accepted = channel::accept_same_user(listener.get(), 30'000, connection, diagnostic);
  listener.reset();
  ::unlink(socket_path.c_str());
  if (accepted != pixcake::ResultCode::Ok) {
    std::cerr << "pixcake_app_exportd: " << diagnostic << '\n';
    return 4;
  }
  pixcake::PosixFileSystem files;
  pixcake::exportd::ExportService service;
  pixcake::exportd::ExportServer server(service, *export_log, exports + "/cache", files, log);
  channel::Channel session{std::move(connection)};
  if (server.serve(session, diagnostic) != pixcake::ResultCode::Ok) {
    std::cerr << "pixcake_app_exportd: " << diagnostic << '\n';
    return 6;
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const auto arguments = pixcake::logging::parse_process_arguments(argc, argv);
  if (!arguments) {
    std::cerr << "usage: pixcake_app_exportd [--health] [--log-dir <dir>] [--socket <path> --data-dir <dir>]\n";
    return 2;
  }
  // 需求 §3.8：诊断日志写到应用支持目录的 logs/；内核只拿到 LogSink，不决定位置。
  pixcake::logging::ProcessLog log(arguments->log_directory, "exportd");
  if (log.sink() == nullptr) {
    std::cerr << "pixcake_app_exportd: log directory unavailable (" << pixcake::result_code_label(log.code())
              << "): " << arguments->log_directory << '\n';
  }
  if (arguments->health) {
    pixcake::exportd::ExportService service;
    std::cout << "exportd boundary ready; pending=" << service.pending_count() << '\n';
    return log.sink() == nullptr ? 3 : 0;
  }
  if (!arguments->socket_path.empty()) {
    if (arguments->data_directory.empty()) {
      std::cerr << "pixcake_app_exportd: --socket needs --data-dir\n";
      return 2;
    }
    return serve_exports(arguments->socket_path, arguments->data_directory, log.sink());
  }
  std::cerr << "pixcake_app_exportd is a process boundary. Use --health for a self-check.\n";
  return log.sink() == nullptr ? 3 : 0;
}
