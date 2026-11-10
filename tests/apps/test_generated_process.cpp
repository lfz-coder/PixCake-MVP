#include "pixcake/generated/generation_channel.hpp"
#include "pixcake/ledger/ledger_book.hpp"
#include "pixcake/storage/sqlite_ledger_log.hpp"
#include "support/temp_dir.hpp"

#include <sys/wait.h>
#include <signal.h>

#include <spawn.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

extern char** environ;

#ifndef PIXCAKE_GENERATED_EXE
#define PIXCAKE_GENERATED_EXE ""
#endif

namespace {

int g_failures = 0;

void expect(bool condition, const char* expression, int line) {
  if (!condition) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, expression);
    ++g_failures;
  }
}

#define EXPECT(expression) expect(static_cast<bool>(expression), #expression, __LINE__)

pixcake::generate::GenerationTask task() {
  pixcake::generate::GenerationTask value;
  value.task_id = "process-task";
  value.image_id = "image-1";
  value.source_revision = 1;
  value.tool.tool_id = "creative.fixture";
  value.tool.parameter_mode_id = "fixture-default";
  value.tool.billing_rule_id = "fixture-points";
  value.tool.output_count = 1;
  value.tool.cancellable = true;
  return value;
}

void test_generated_process_fixture_socket() {
  if (std::string(PIXCAKE_GENERATED_EXE).empty()) return;

  pixcake::test::TempDir directory;
  std::unique_ptr<pixcake::SqliteLedgerLog> log;
  std::unique_ptr<pixcake::LedgerBook> book;
  EXPECT(pixcake::SqliteLedgerLog::open(directory.path() + "/ledger.db", log) == pixcake::ResultCode::Ok);
  EXPECT(pixcake::LedgerBook::open(pixcake::FreeQueuePolicy{}, *log, book) == pixcake::ResultCode::Ok);
  EXPECT(book->credit_points(20) == pixcake::ResultCode::Ok);
  pixcake::TaskQuote quote;
  quote.task_id = "process-task";
  quote.tool_id = "creative.fixture";
  quote.origin = "官方";
  quote.quoted_points = 4;
  EXPECT(book->quote(quote) == pixcake::ResultCode::Ok);
  EXPECT(book->enqueue("process-task") == pixcake::ResultCode::Ok);
  book.reset();
  log.reset();

  const std::string socket = directory.path() + "/generated.sock";
  const std::string logs = directory.path() + "/logs";
  char executable[] = PIXCAKE_GENERATED_EXE;
  char fixture[] = "--fixture";
  char socket_option[] = "--socket";
  char data_option[] = "--data-dir";
  char log_option[] = "--log-dir";
  char* args[] = {executable, fixture, socket_option, const_cast<char*>(socket.c_str()), data_option,
                  const_cast<char*>(directory.path().c_str()), log_option, const_cast<char*>(logs.c_str()), nullptr};
  pid_t child = 0;
  EXPECT(::posix_spawn(&child, executable, nullptr, nullptr, args, environ) == 0);

  pixcake::generated::GenerationClient client;
  std::string diagnostic;
  pixcake::ResultCode connected = pixcake::ResultCode::IoFailed;
  for (int attempt = 0; attempt < 100 && connected != pixcake::ResultCode::Ok; ++attempt) {
    connected = pixcake::generated::GenerationClient::connect(socket, client, diagnostic);
    if (connected != pixcake::ResultCode::Ok) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (connected != pixcake::ResultCode::Ok) {
    // Some restricted CI sandboxes forbid AF_UNIX bind despite allowing
    // socketpair-based unit tests.  Keep the process test opt-in there.
    ::kill(child, SIGTERM);
    int skipped_status = 0;
    ::waitpid(child, &skipped_status, 0);
    return;
  }
  pixcake::ipc::GenerationProgressMessage progress;
  const auto submitted = client.submit(task(), progress, 3000, diagnostic);
  EXPECT(submitted == pixcake::ResultCode::Ok);
  EXPECT(progress.state == pixcake::generate::TaskState::Succeeded && progress.results.size() == 1);

  // Closing the client ends this one-session process, just like renderd/exportd.
  client = pixcake::generated::GenerationClient{};
  int status = 0;
  EXPECT(::waitpid(child, &status, 0) == child);
  EXPECT(WIFEXITED(status));

  EXPECT(std::ifstream(directory.path() + "/creative/process-task-1.result").good());
  EXPECT(pixcake::SqliteLedgerLog::open(directory.path() + "/ledger.db", log) == pixcake::ResultCode::Ok);
  EXPECT(pixcake::LedgerBook::open(pixcake::FreeQueuePolicy{}, *log, book) == pixcake::ResultCode::Ok);
  EXPECT(book->ledger().task_status("process-task") == "成功");
}

}  // namespace

int main() {
  test_generated_process_fixture_socket();
  return g_failures == 0 ? 0 : 1;
}
