#include "pixcake/generation_session/generation_session.hpp"

#include "pixcake/account/account_session.hpp"
#include "pixcake/generate/generation_tool_catalog.hpp"
#include "support/temp_dir.hpp"

#include <cstdio>
#include <memory>

namespace {

int g_failures = 0;

void expect(bool condition, const char* expression, int line) {
  if (!condition) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, expression);
    ++g_failures;
  }
}

#define EXPECT(expression) expect(static_cast<bool>(expression), #expression, __LINE__)

void test_startup_recovers_stale_running_tasks() {
  pixcake::test::TempDir directory;
  pixcake::account::AccountSession account;
  pixcake::account::AccountSnapshot snapshot;
  snapshot.account_id = "generation-test";
  snapshot.session_handle = "session";
  EXPECT(account.sign_in(snapshot) == pixcake::ResultCode::Ok);

  std::unique_ptr<pixcake::SqliteLedgerLog> log;
  std::unique_ptr<pixcake::LedgerBook> book;
  EXPECT(pixcake::SqliteLedgerLog::open(directory.path() + "/ledger.db", log) == pixcake::ResultCode::Ok);
  EXPECT(pixcake::LedgerBook::open(pixcake::FreeQueuePolicy{}, *log, book) == pixcake::ResultCode::Ok);
  EXPECT(book->credit_points(10) == pixcake::ResultCode::Ok);
  pixcake::TaskQuote quote;
  quote.task_id = "stale-task";
  quote.tool_id = pixcake::generate::kDevelopmentFixtureToolId;
  quote.origin = pixcake::kLedgerOriginPackage;
  EXPECT(book->quote(quote) == pixcake::ResultCode::Ok);
  EXPECT(book->enqueue("stale-task") == pixcake::ResultCode::Ok);
  EXPECT(book->start_generating("stale-task") == pixcake::ResultCode::Ok);
  book.reset();
  log.reset();

  pixcake::generation_session::GenerationSessionOptions options;
  options.process.executable = "/missing/generated";
  options.process.data_directory = directory.path();
  std::unique_ptr<pixcake::generation_session::GenerationSession> session;
  std::string diagnostic;
  EXPECT(pixcake::generation_session::GenerationSession::open(account, std::move(options), session, diagnostic) ==
         pixcake::ResultCode::Ok);
  EXPECT(session != nullptr);
  EXPECT(session != nullptr && session->task_statuses().front().second == "失败");
}

void test_cancel_before_queue_and_failed_start_are_terminal() {
  pixcake::test::TempDir directory;
  pixcake::account::AccountSession account;
  pixcake::account::AccountSnapshot snapshot;
  snapshot.account_id = "generation-test";
  snapshot.session_handle = "session";
  EXPECT(account.sign_in(snapshot) == pixcake::ResultCode::Ok);
  pixcake::generation_session::GenerationSessionOptions options;
  options.process.executable = "/missing/generated";
  options.process.data_directory = directory.path();
  options.opening_points = 20;
  std::unique_ptr<pixcake::generation_session::GenerationSession> session;
  std::string diagnostic;
  EXPECT(pixcake::generation_session::GenerationSession::open(account, options, session, diagnostic) == pixcake::ResultCode::Ok);
  if (!session) return;
  const auto catalog = pixcake::generate::ToolCatalog::development_fixture();
  pixcake::generate::GenerationTask task;
  EXPECT(catalog.build_task(pixcake::generate::kDevelopmentFixtureToolId, "cancel-before-queue", "image-1", 1,
                           {}, {}, {}, "", task) == pixcake::ResultCode::Ok);
  pixcake::TaskQuote quote;
  quote.task_id = task.task_id;
  quote.tool_id = task.tool.tool_id;
  quote.origin = pixcake::kLedgerOriginPackage;
  quote.quoted_points = 4;
  pixcake::ipc::GenerationProgressMessage progress;
  std::atomic<bool> cancel_requested{true};
  EXPECT(session->generate(task, quote, progress, 1000, diagnostic, &cancel_requested) == pixcake::ResultCode::Cancelled);
  EXPECT(session->history().empty() && session->task_statuses().empty() && session->creative_points() == 20);
  cancel_requested.store(false);
  EXPECT(session->generate(task, quote, progress, 1000, diagnostic, &cancel_requested) == pixcake::ResultCode::SourceMissing);
  EXPECT(session->creative_points() == 20);
  EXPECT(session->history().size() == 1 && session->history().front().status == "失败");
  EXPECT(session->task_statuses().size() == 1 && session->task_statuses().front().second == "失败");
}

}  // namespace

int main() {
  test_startup_recovers_stale_running_tasks();
  test_cancel_before_queue_and_failed_start_are_terminal();
  return g_failures == 0 ? 0 : 1;
}
