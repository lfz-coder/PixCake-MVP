#include "pixcake/ledger/credit_ledger.hpp"
#include "pixcake/ledger/ledger_book.hpp"
#include "pixcake/storage/sqlite_ledger_log.hpp"
#include "support/temp_dir.hpp"

#include <sqlite3.h>

#include <cstdio>
#include <memory>
#include <string>

namespace {

int g_failures = 0;

void expect(bool cond, const char* text, int line) {
  if (!cond) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, text);
    ++g_failures;
  }
}

#define EXPECT(cond) expect(static_cast<bool>(cond), #cond, __LINE__)

pixcake::FreeQueuePolicy version_930() {
  pixcake::FreeQueuePolicy policy;
  policy.official_member_free_queue = false;
  policy.subscribed_package_non_high_compute_free_queue = true;
  return policy;
}

pixcake::TaskQuote quote_of(std::string task_id, std::string origin, std::int64_t points) {
  pixcake::TaskQuote quote;
  quote.task_id = std::move(task_id);
  quote.origin = std::move(origin);
  quote.quoted_points = points;
  quote.acceleration_points = points;
  quote.member = true;
  quote.subscribed = true;
  return quote;
}

pixcake::CreditLedger charged(pixcake::FreeQueuePolicy policy, std::int64_t points, std::int64_t sheets) {
  pixcake::CreditLedger ledger = pixcake::CreditLedger::open(policy, {});
  EXPECT(ledger.credit_points(points) == pixcake::ResultCode::Ok);
  if (sheets > 0) {
    EXPECT(ledger.credit_sheets(sheets) == pixcake::ResultCode::Ok);
  }
  return ledger;
}

void test_balances_stay_separate_and_queue_cancel_is_free() {
  auto ledger = charged(version_930(), 100, 4);
  auto quote = quote_of("task-1", "官方", 30);
  EXPECT(ledger.quote(quote) == pixcake::ResultCode::Ok);
  EXPECT(ledger.enqueue("task-1") == pixcake::ResultCode::Ok);
  EXPECT(ledger.task_status("task-1") == "排队中");
  EXPECT(ledger.cancel("task-1") == pixcake::ResultCode::Ok);
  EXPECT(ledger.task_status("task-1") == "排队取消");
  EXPECT(ledger.creative_points() == 100);
  EXPECT(ledger.export_sheets() == 4);
  EXPECT(ledger.quote(quote) == pixcake::ResultCode::Ok);
  EXPECT(ledger.task_status("task-1") == "排队取消");
  EXPECT(ledger.creative_points() == 100);

  auto queued = charged(version_930(), 100, 4);
  EXPECT(queued.quote(quote_of("task-q", "官方", 30)) == pixcake::ResultCode::Ok);
  EXPECT(queued.enqueue("task-q") == pixcake::ResultCode::Ok);
  EXPECT(queued.crash("task-q") == pixcake::ResultCode::Ok);
  EXPECT(queued.task_status("task-q") == "排队中");
  EXPECT(queued.creative_points() == 100);
}

void test_generating_cancel_and_crash_do_not_refund() {
  auto policy = version_930();
  auto ledger = charged(policy, 100, 1);
  auto quote = quote_of("task-1", "官方", 30);
  EXPECT(ledger.quote(quote) == pixcake::ResultCode::Ok);
  EXPECT(ledger.enqueue("task-1") == pixcake::ResultCode::Ok);
  EXPECT(ledger.start_generating("task-1") == pixcake::ResultCode::Ok);
  EXPECT(ledger.creative_points() == 70);
  EXPECT(ledger.task_status("task-1") == "生成中");
  EXPECT(ledger.start_generating("task-1") == pixcake::ResultCode::Ok);
  EXPECT(ledger.creative_points() == 70);
  EXPECT(ledger.cancel("task-1") == pixcake::ResultCode::Ok);
  EXPECT(ledger.task_status("task-1") == "生成中取消");
  EXPECT(ledger.creative_points() == 70);

  auto crashed = charged(policy, 100, 1);
  EXPECT(crashed.quote(quote_of("task-2", "官方", 30)) == pixcake::ResultCode::Ok);
  EXPECT(crashed.enqueue("task-2") == pixcake::ResultCode::Ok);
  EXPECT(crashed.start_generating("task-2") == pixcake::ResultCode::Ok);
  EXPECT(crashed.crash("task-2") == pixcake::ResultCode::Ok);
  EXPECT(crashed.task_status("task-2") == "失败");
  EXPECT(crashed.creative_points() == 70);
  EXPECT(crashed.succeed("task-2") == pixcake::ResultCode::StateConflict);
  EXPECT(crashed.creative_points() == 70);
}

void test_accelerate_is_charged_once_and_strokes_dedupe() {
  auto ledger = charged(version_930(), 100, 1);
  auto quote = quote_of("task-1", "官方", 30);
  quote.acceleration_points = 40;
  EXPECT(ledger.quote(quote) == pixcake::ResultCode::Ok);
  EXPECT(ledger.enqueue("task-1") == pixcake::ResultCode::Ok);
  EXPECT(ledger.accelerate("task-1") == pixcake::ResultCode::Ok);
  EXPECT(ledger.creative_points() == 60);
  EXPECT(ledger.task_status("task-1") == "排队中");
  EXPECT(ledger.accelerate("task-1") == pixcake::ResultCode::Ok);
  EXPECT(ledger.creative_points() == 60);
  EXPECT(ledger.start_generating("task-1") == pixcake::ResultCode::Ok);
  EXPECT(ledger.creative_points() == 60);
  EXPECT(ledger.stroke("task-1", "stroke-1") == pixcake::ResultCode::Ok);
  EXPECT(ledger.creative_points() == 50);
  EXPECT(ledger.stroke("task-1", "stroke-1") == pixcake::ResultCode::Ok);
  EXPECT(ledger.creative_points() == 50);
  EXPECT(ledger.stroke("task-2", "stroke-1") == pixcake::ResultCode::Ok);
  EXPECT(ledger.creative_points() == 50);

  auto poor = charged(version_930(), 5, 1);
  EXPECT(poor.quote(quote_of("task-3", "官方", 30)) == pixcake::ResultCode::Ok);
  EXPECT(poor.enqueue("task-3") == pixcake::ResultCode::Ok);
  EXPECT(poor.start_generating("task-3") == pixcake::ResultCode::InsufficientBalance);
  EXPECT(poor.task_status("task-3") == "排队中");
  EXPECT(poor.creative_points() == 5);
  EXPECT(poor.export_sheets() == 1);
}

void test_free_queue_switches_and_master_allowance() {
  auto policy = version_930();
  auto official = charged(policy, 100, 1);
  EXPECT(official.quote(quote_of("off", "官方", 30)) == pixcake::ResultCode::Ok);
  EXPECT(official.enqueue("off") == pixcake::ResultCode::Ok);
  EXPECT(official.start_generating("off") == pixcake::ResultCode::Ok);
  EXPECT(official.creative_points() == 70);

  policy.official_member_free_queue = true;
  auto member = charged(policy, 100, 1);
  auto member_quote = quote_of("mem", "官方", 30);
  EXPECT(member.quote(member_quote) == pixcake::ResultCode::Ok);
  EXPECT(member.enqueue("mem") == pixcake::ResultCode::Ok);
  EXPECT(member.start_generating("mem") == pixcake::ResultCode::Ok);
  EXPECT(member.creative_points() == 100);

  auto package = charged(version_930(), 100, 1);
  EXPECT(package.quote(quote_of("pkg", "工具包", 30)) == pixcake::ResultCode::Ok);
  EXPECT(package.enqueue("pkg") == pixcake::ResultCode::Ok);
  EXPECT(package.start_generating("pkg") == pixcake::ResultCode::Ok);
  EXPECT(package.creative_points() == 100);

  auto heavy = quote_of("heavy", "工具包", 30);
  heavy.high_compute = true;
  auto heavy_ledger = charged(version_930(), 100, 1);
  EXPECT(heavy_ledger.quote(heavy) == pixcake::ResultCode::Ok);
  EXPECT(heavy_ledger.enqueue("heavy") == pixcake::ResultCode::Ok);
  EXPECT(heavy_ledger.start_generating("heavy") == pixcake::ResultCode::Ok);
  EXPECT(heavy_ledger.creative_points() == 70);

  auto master = charged(version_930(), 100, 1);
  for (int index = 1; index <= 3; ++index) {
    auto quote = quote_of("m" + std::to_string(index), "大师精选", 30);
    quote.tool_id = "master-tool";
    EXPECT(master.quote(quote) == pixcake::ResultCode::Ok);
    EXPECT(master.enqueue(quote.task_id) == pixcake::ResultCode::Ok);
    EXPECT(master.start_generating(quote.task_id) == pixcake::ResultCode::Ok);
  }
  EXPECT(master.creative_points() == 100);
  auto fourth = quote_of("m4", "大师精选", 30);
  fourth.tool_id = "master-tool";
  fourth.subscribed = false;
  EXPECT(master.quote(fourth) == pixcake::ResultCode::Ok);
  EXPECT(master.enqueue("m4") == pixcake::ResultCode::Ok);
  EXPECT(master.start_generating("m4") == pixcake::ResultCode::StateConflict);
  EXPECT(master.task_status("m4") == "排队中");
  EXPECT(master.creative_points() == 100);
}

void test_export_sheets_count_originals_once_per_settlement() {
  auto ledger = charged(version_930(), 10, 2);
  std::vector<pixcake::ExportProduct> batch = {
      {"exp-a", "orig-1", false, true},
      {"exp-b", "orig-1", false, true},
      {"exp-c", "orig-1", true, true},
      {"exp-d", "orig-2", false, false},
  };
  EXPECT(ledger.settle_exports(batch) == pixcake::ResultCode::Ok);
  EXPECT(ledger.export_sheets() == 1);
  EXPECT(ledger.creative_points() == 10);
  EXPECT(ledger.settle_exports(batch) == pixcake::ResultCode::Ok);
  EXPECT(ledger.export_sheets() == 1);

  std::vector<pixcake::ExportProduct> again = {{"exp-e", "orig-1", false, true}};
  EXPECT(ledger.settle_exports(again) == pixcake::ResultCode::Ok);
  EXPECT(ledger.export_sheets() == 0);

  auto poor = charged(version_930(), 10, 0);
  EXPECT(poor.credit_sheets(0) == pixcake::ResultCode::StateConflict);
  const auto before = poor.events().size();
  EXPECT(poor.settle_exports(again) == pixcake::ResultCode::InsufficientBalance);
  EXPECT(poor.export_sheets() == 0);
  EXPECT(poor.events().size() == before);
}

void test_export_batch_identity_deduplicates_partial_completion_but_not_new_batch() {
  auto ledger = charged(version_930(), 10, 3);
  std::vector<pixcake::ExportProduct> first_part = {
      {"batch-a-1", "orig-1", false, true, "batch-a"},
  };
  std::vector<pixcake::ExportProduct> second_part = {
      {"batch-a-1", "orig-1", false, true, "batch-a"},
      {"batch-a-2", "orig-1", false, true, "batch-a"},
  };
  EXPECT(ledger.settle_exports(first_part) == pixcake::ResultCode::Ok);
  EXPECT(ledger.export_sheets() == 2);
  EXPECT(ledger.settle_exports(second_part) == pixcake::ResultCode::Ok);
  EXPECT(ledger.export_sheets() == 2);

  std::vector<pixcake::ExportProduct> new_batch = {
      {"batch-b-1", "orig-1", false, true, "batch-b"},
  };
  EXPECT(ledger.settle_exports(new_batch) == pixcake::ResultCode::Ok);
  EXPECT(ledger.export_sheets() == 1);
}

void test_export_batch_identity_survives_sqlite_reopen() {
  pixcake::test::TempDir dir;
  const std::string path = dir.path() + "/ledger.db";
  auto policy = version_930();
  {
    std::unique_ptr<pixcake::SqliteLedgerLog> log;
    EXPECT(pixcake::SqliteLedgerLog::open(path, log) == pixcake::ResultCode::Ok);
    std::unique_ptr<pixcake::LedgerBook> book;
    EXPECT(pixcake::LedgerBook::open(policy, *log, book) == pixcake::ResultCode::Ok);
    EXPECT(book->credit_sheets(3) == pixcake::ResultCode::Ok);
    EXPECT(book->settle_exports({{"batch-a-1", "orig-1", false, true, "batch-a"}}) == pixcake::ResultCode::Ok);
    EXPECT(book->ledger().export_sheets() == 2);
  }

  std::unique_ptr<pixcake::SqliteLedgerLog> reopened_log;
  EXPECT(pixcake::SqliteLedgerLog::open(path, reopened_log) == pixcake::ResultCode::Ok);
  std::unique_ptr<pixcake::LedgerBook> reopened;
  EXPECT(pixcake::LedgerBook::open(policy, *reopened_log, reopened) == pixcake::ResultCode::Ok);
  EXPECT(reopened->settle_exports({
            {"batch-a-2", "orig-1", false, true, "batch-a"},
            {"batch-b-1", "orig-1", false, true, "batch-b"},
        }) == pixcake::ResultCode::Ok);
  EXPECT(reopened->ledger().export_sheets() == 1);

  sqlite3* raw = nullptr;
  EXPECT(sqlite3_open(path.c_str(), &raw) == SQLITE_OK);
  sqlite3_stmt* statement = nullptr;
  EXPECT(sqlite3_prepare_v2(raw, "SELECT b.batch_id FROM ledger_event e JOIN ledger_export_batch b ON b.export_id = e.export_id AND b.original_id = e.original_id WHERE e.kind = 'export-sheet' ORDER BY e.seq;", -1,
                            &statement, nullptr) == SQLITE_OK);
  EXPECT(sqlite3_step(statement) == SQLITE_ROW);
  EXPECT(std::string(reinterpret_cast<const char*>(sqlite3_column_text(statement, 0))) == "batch-a");
  EXPECT(sqlite3_step(statement) == SQLITE_ROW);
  EXPECT(std::string(reinterpret_cast<const char*>(sqlite3_column_text(statement, 0))) == "batch-b");
  EXPECT(sqlite3_step(statement) == SQLITE_DONE);
  sqlite3_finalize(statement);
  sqlite3_close(raw);
}

void test_reopen_replays_events_and_repairs_cache() {
  pixcake::test::TempDir dir;
  const std::string path = dir.path() + "/ledger.db";
  auto policy = version_930();
  {
    std::unique_ptr<pixcake::SqliteLedgerLog> log;
    EXPECT(pixcake::SqliteLedgerLog::open(path, log) == pixcake::ResultCode::Ok);
    std::unique_ptr<pixcake::LedgerBook> book;
    EXPECT(pixcake::LedgerBook::open(policy, *log, book) == pixcake::ResultCode::Ok);
    EXPECT(book->credit_points(80) == pixcake::ResultCode::Ok);
    EXPECT(book->credit_sheets(3) == pixcake::ResultCode::Ok);
    EXPECT(book->quote(quote_of("task-1", "官方", 30)) == pixcake::ResultCode::Ok);
    EXPECT(book->enqueue("task-1") == pixcake::ResultCode::Ok);
    EXPECT(book->start_generating("task-1") == pixcake::ResultCode::Ok);
    EXPECT(book->ledger().creative_points() == 50);
    EXPECT(book->ledger().export_sheets() == 3);
    EXPECT(book->ledger().task_status("task-1") == "生成中");
  }

  sqlite3* raw = nullptr;
  EXPECT(sqlite3_open(path.c_str(), &raw) == SQLITE_OK);
  EXPECT(sqlite3_exec(raw, "UPDATE ledger_cache SET creative_points = 1, export_sheets = 9 WHERE id = 1;",
                      nullptr, nullptr, nullptr) == SQLITE_OK);
  EXPECT(sqlite3_exec(raw, "UPDATE ledger_task_cache SET status = '排队中' WHERE task_id = 'task-1';",
                      nullptr, nullptr, nullptr) == SQLITE_OK);
  sqlite3_close(raw);

  std::unique_ptr<pixcake::SqliteLedgerLog> reopened_log;
  EXPECT(pixcake::SqliteLedgerLog::open(path, reopened_log) == pixcake::ResultCode::Ok);
  std::unique_ptr<pixcake::LedgerBook> reopened;
  EXPECT(pixcake::LedgerBook::open(policy, *reopened_log, reopened) == pixcake::ResultCode::Ok);
  EXPECT(reopened->ledger().creative_points() == 50);
  EXPECT(reopened->ledger().export_sheets() == 3);
  EXPECT(reopened->ledger().task_status("task-1") == "生成中");

  sqlite3_stmt* cache = nullptr;
  EXPECT(sqlite3_open(path.c_str(), &raw) == SQLITE_OK);
  EXPECT(sqlite3_prepare_v2(raw, "SELECT creative_points, export_sheets FROM ledger_cache WHERE id = 1;", -1,
                            &cache, nullptr) == SQLITE_OK);
  EXPECT(sqlite3_step(cache) == SQLITE_ROW);
  EXPECT(sqlite3_column_int64(cache, 0) == 50);
  EXPECT(sqlite3_column_int64(cache, 1) == 3);
  sqlite3_finalize(cache);
  sqlite3_stmt* task_cache = nullptr;
  EXPECT(sqlite3_prepare_v2(raw, "SELECT status FROM ledger_task_cache WHERE task_id = ?;", -1, &task_cache,
                            nullptr) == SQLITE_OK);
  EXPECT(sqlite3_bind_text(task_cache, 1, "task-1", -1, SQLITE_TRANSIENT) == SQLITE_OK);
  EXPECT(sqlite3_step(task_cache) == SQLITE_ROW);
  const auto* repaired_status = reinterpret_cast<const char*>(sqlite3_column_text(task_cache, 0));
  EXPECT(repaired_status != nullptr && std::string(repaired_status) == "生成中");
  sqlite3_finalize(task_cache);
  sqlite3_close(raw);

  pixcake::test::TempDir future_dir;
  const std::string future_path = future_dir.path() + "/ledger.db";
  EXPECT(sqlite3_open(future_path.c_str(), &raw) == SQLITE_OK);
  EXPECT(sqlite3_exec(raw, "PRAGMA user_version=2;", nullptr, nullptr, nullptr) == SQLITE_OK);
  sqlite3_close(raw);
  std::unique_ptr<pixcake::SqliteLedgerLog> future;
  EXPECT(pixcake::SqliteLedgerLog::open(future_path, future) == pixcake::ResultCode::StateConflict);
  EXPECT(future == nullptr);
  EXPECT(sqlite3_open(future_path.c_str(), &raw) == SQLITE_OK);
  sqlite3_stmt* version = nullptr;
  EXPECT(sqlite3_prepare_v2(raw, "PRAGMA user_version;", -1, &version, nullptr) == SQLITE_OK);
  EXPECT(sqlite3_step(version) == SQLITE_ROW);
  EXPECT(sqlite3_column_int(version, 0) == 2);
  sqlite3_finalize(version);
  sqlite3_close(raw);
}

void test_sqlite_handles_reject_stale_duplicate_charge() {
  pixcake::test::TempDir dir;
  const std::string path = dir.path() + "/ledger.db";
  auto policy = version_930();
  std::unique_ptr<pixcake::SqliteLedgerLog> first_log;
  std::unique_ptr<pixcake::SqliteLedgerLog> second_log;
  EXPECT(pixcake::SqliteLedgerLog::open(path, first_log) == pixcake::ResultCode::Ok);
  EXPECT(pixcake::SqliteLedgerLog::open(path, second_log) == pixcake::ResultCode::Ok);
  std::unique_ptr<pixcake::LedgerBook> first;
  std::unique_ptr<pixcake::LedgerBook> second;
  EXPECT(pixcake::LedgerBook::open(policy, *first_log, first) == pixcake::ResultCode::Ok);
  EXPECT(first->credit_points(100) == pixcake::ResultCode::Ok);
  EXPECT(first->quote(quote_of("task", "官方", 30)) == pixcake::ResultCode::Ok);
  EXPECT(first->enqueue("task") == pixcake::ResultCode::Ok);
  EXPECT(pixcake::LedgerBook::open(policy, *second_log, second) == pixcake::ResultCode::Ok);
  EXPECT(first->start_generating("task") == pixcake::ResultCode::Ok);
  EXPECT(second->start_generating("task") == pixcake::ResultCode::StateConflict);

  std::unique_ptr<pixcake::SqliteLedgerLog> reopened_log;
  std::unique_ptr<pixcake::LedgerBook> reopened;
  EXPECT(pixcake::SqliteLedgerLog::open(path, reopened_log) == pixcake::ResultCode::Ok);
  EXPECT(pixcake::LedgerBook::open(policy, *reopened_log, reopened) == pixcake::ResultCode::Ok);
  EXPECT(reopened->ledger().creative_points() == 70);
  EXPECT(reopened->ledger().task_status("task") == "生成中");
}

void test_master_acceleration_does_not_bypass_subscription() {
  auto policy = version_930();
  auto ledger = charged(policy, 100, 1);
  for (int index = 1; index <= 3; ++index) {
    auto quote = quote_of("free-" + std::to_string(index), "大师精选", 30);
    quote.tool_id = "master-tool";
    EXPECT(ledger.quote(quote) == pixcake::ResultCode::Ok);
    EXPECT(ledger.enqueue(quote.task_id) == pixcake::ResultCode::Ok);
    EXPECT(ledger.start_generating(quote.task_id) == pixcake::ResultCode::Ok);
  }
  auto blocked = quote_of("blocked", "大师精选", 30);
  blocked.tool_id = "master-tool";
  blocked.subscribed = false;
  blocked.acceleration_points = 5;
  EXPECT(ledger.quote(blocked) == pixcake::ResultCode::Ok);
  EXPECT(ledger.enqueue(blocked.task_id) == pixcake::ResultCode::Ok);
  EXPECT(ledger.accelerate(blocked.task_id) == pixcake::ResultCode::StateConflict);
  EXPECT(ledger.start_generating(blocked.task_id) == pixcake::ResultCode::StateConflict);
  EXPECT(ledger.creative_points() == 100);
}

}  // namespace

int main() {
  test_balances_stay_separate_and_queue_cancel_is_free();
  test_generating_cancel_and_crash_do_not_refund();
  test_accelerate_is_charged_once_and_strokes_dedupe();
  test_free_queue_switches_and_master_allowance();
  test_export_sheets_count_originals_once_per_settlement();
  test_export_batch_identity_deduplicates_partial_completion_but_not_new_batch();
  test_export_batch_identity_survives_sqlite_reopen();
  test_reopen_replays_events_and_repairs_cache();
  test_sqlite_handles_reject_stale_duplicate_charge();
  test_master_acceleration_does_not_bypass_subscription();
  if (g_failures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  return 0;
}
