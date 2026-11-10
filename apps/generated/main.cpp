#include "pixcake/generated/fixture_wire_codec.hpp"
#include "pixcake/generated/generation_channel.hpp"
#include "pixcake/generated/generation_service.hpp"
#include "pixcake/generated/remote_tool_router.hpp"
#include "pixcake/generate/generation_tool_catalog.hpp"
#include "pixcake/logging/process_log.hpp"
#include "pixcake/persist/file_system.hpp"
#include "pixcake/preview_channel/channel.hpp"
#include "pixcake/storage/sqlite_ledger_log.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>

namespace {

bool ensure_directory(const std::string& path) {
  return ::mkdir(path.c_str(), 0700) == 0 || errno == EEXIST;
}

pixcake::generated::RemoteReply fixture_reply() {
  pixcake::generated::RemoteReply reply;
  reply.results.push_back({{255, 255, 255, 255}, 1, 1, {}});
  return reply;
}

int serve_fixture(const std::string& socket_path, const std::string& data_directory,
                  pixcake::LogSink* log) {
  if (data_directory.empty()) {
    std::cerr << "pixcake_app_generated: --fixture --socket needs --data-dir\n";
    return 2;
  }
  if (!ensure_directory(data_directory) || !ensure_directory(data_directory + "/creative")) {
    std::cerr << "pixcake_app_generated: data directory unavailable: " << data_directory << '\n';
    return 5;
  }

  std::unique_ptr<pixcake::SqliteLedgerLog> ledger_log;
  if (pixcake::SqliteLedgerLog::open(data_directory + "/ledger.db", ledger_log) != pixcake::ResultCode::Ok) {
    std::cerr << "pixcake_app_generated: ledger unavailable\n";
    return 5;
  }
  std::unique_ptr<pixcake::LedgerBook> ledger;
  if (pixcake::LedgerBook::open(pixcake::FreeQueuePolicy{}, *ledger_log, ledger) != pixcake::ResultCode::Ok) {
    std::cerr << "pixcake_app_generated: ledger state unavailable\n";
    return 5;
  }

  pixcake::PosixFileSystem files;
  pixcake::generated::FixtureWireCodec codec;
  pixcake::generated::FixtureHttpTransport transport(fixture_reply());
  pixcake::generated::HttpRemoteTool fixture_remote(transport, codec, std::chrono::seconds(30));
  const auto catalog = pixcake::generate::ToolCatalog::development_fixture();
  const auto* fixture_descriptor = catalog.find(pixcake::generate::kDevelopmentFixtureToolId);
  pixcake::generated::RemoteToolRegistry registry;
  if (fixture_descriptor == nullptr || registry.add(*fixture_descriptor, fixture_remote) != pixcake::ResultCode::Ok) {
    std::cerr << "pixcake_app_generated: fixture provider registration failed\n";
    return 5;
  }
  pixcake::generated::RoutedRemoteTool remote(registry);
  pixcake::generated::GenerationService service(*ledger, files, data_directory + "/creative", remote);

  std::string diagnostic;
  pixcake::preview_channel::UniqueFd listener;
  if (pixcake::preview_channel::listen_at(socket_path, listener, diagnostic) != pixcake::ResultCode::Ok) {
    std::cerr << "pixcake_app_generated: " << diagnostic << '\n';
    return 5;
  }
  pixcake::preview_channel::UniqueFd connection;
  const auto accepted =
      pixcake::preview_channel::accept_same_user(listener.get(), 30'000, connection, diagnostic);
  listener.reset();
  ::unlink(socket_path.c_str());
  if (accepted != pixcake::ResultCode::Ok) {
    std::cerr << "pixcake_app_generated: " << diagnostic << '\n';
    return 4;
  }

  if (log != nullptr) {
    pixcake::LogRecord record;
    record.module = "generated";
    record.event = "generated.listen";
    record.path = socket_path;
    record.message = "fixture provider";
    log->write(record);
  }
  pixcake::preview_channel::Channel session{std::move(connection)};
  pixcake::generated::GenerationServer server(service);
  const auto served = server.serve(session, -1, diagnostic);
  if (served != pixcake::ResultCode::Ok) {
    std::cerr << "pixcake_app_generated: " << diagnostic << '\n';
    return 6;
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const auto arguments = pixcake::logging::parse_process_arguments(argc, argv);
  if (!arguments) {
    std::cerr << "usage: pixcake_app_generated [--health] [--fixture --socket <path> --data-dir <dir>] "
                 "[--log-dir <dir>]\n";
    return 2;
  }
  // 需求 §3.8：诊断日志写到应用支持目录的 logs/；内核只拿到 LogSink，不决定位置。
  pixcake::logging::ProcessLog log(arguments->log_directory, "generated");
  if (log.sink() == nullptr) {
    std::cerr << "pixcake_app_generated: log directory unavailable (" << pixcake::result_code_label(log.code())
              << "): " << arguments->log_directory << '\n';
  }
  if (arguments->health) {
    std::cout << "generated boundary ready; fixture=" << (arguments->fixture ? "1" : "0") << '\n';
    return log.sink() == nullptr ? 3 : 0;
  }
  if (!arguments->socket_path.empty()) {
    if (!arguments->fixture) {
      std::cerr << "pixcake_app_generated: no official provider is configured; use --fixture for the local test provider\n";
      return 2;
    }
    return serve_fixture(arguments->socket_path, arguments->data_directory, log.sink());
  }
  std::cerr << "pixcake_app_generated is a process boundary. Use --health or --fixture --socket <path>.\n";
  return log.sink() == nullptr ? 3 : 0;
}
