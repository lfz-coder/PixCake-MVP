#include "pixcake/ledger/credit_ledger.hpp"
#include "pixcake/ledger/ledger_book.hpp"
#include "pixcake/storage/sqlite_ledger_log.hpp"
#include "pixcake/support/counting_log_sink.hpp"
#include "support/temp_dir.hpp"

#include <sqlite3.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

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

void test_b103_balances_stay_separate_and_queue_cancel_is_free() {
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


void test_b64_accelerate_is_charged_once_and_strokes_dedupe() {
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

void test_b109_b110_export_sheets_count_originals_once_per_settlement() {
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

  // The same image again: charged once only (test build, 2026-11-07 evidence
  // export-credit-observation).  A new image is charged.
  std::vector<pixcake::ExportProduct> again = {{"exp-e", "orig-1", false, true}};
  EXPECT(ledger.settle_exports(again) == pixcake::ResultCode::Ok);
  EXPECT(ledger.export_sheets() == 1);
  EXPECT(ledger.original_charged("orig-1") && ledger.export_settled("exp-e"));
  std::vector<pixcake::ExportProduct> other = {{"exp-f", "orig-3", false, true}};
  EXPECT(ledger.settle_exports(other) == pixcake::ResultCode::Ok);
  EXPECT(ledger.export_sheets() == 0);
  // Originals and free-tool products are recorded but never charged.
  auto plain = charged(version_930(), 10, 1);
  pixcake::ExportProduct free_effect{"exp-g", "orig-4", false, true};
  free_effect.chargeable = false;
  EXPECT(plain.settle_exports({free_effect}) == pixcake::ResultCode::Ok);
  EXPECT(plain.export_sheets() == 1 && plain.export_settled("exp-g") && !plain.original_charged("orig-4"));
  // ...and a later refined export of that image is its first charge.
  EXPECT(plain.settle_exports({{"exp-h", "orig-4", false, true}}) == pixcake::ResultCode::Ok);
  EXPECT(plain.export_sheets() == 0 && plain.original_charged("orig-4"));

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

  // A new batch of the same image is not charged again (once per image).
  std::vector<pixcake::ExportProduct> new_batch = {
      {"batch-b-1", "orig-1", false, true, "batch-b"},
  };
  EXPECT(ledger.settle_exports(new_batch) == pixcake::ResultCode::Ok);
  EXPECT(ledger.export_sheets() == 2);
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

struct LedgerRecords final : pixcake::LogSink {
  std::vector<pixcake::LogRecord> records;
  void write(const pixcake::LogRecord& record) override { records.push_back(record); }
  const pixcake::LogRecord* last(const std::string& event, const std::string& task_id) const {
    const pixcake::LogRecord* found = nullptr;
    for (const auto& record : records) {
      if (record.event == event && record.task_id == task_id) found = &record;
    }
    return found;
  }
};

void test_o09_cancel_position_and_charge_are_logged() {
  LedgerRecords kept;
  pixcake::CountingLogSink log(&kept);
  auto ledger = charged(version_930(), 100, 1);
  ledger.set_log(&log);
  EXPECT(ledger.quote(quote_of("queued", "官方", 30)) == pixcake::ResultCode::Ok);
  EXPECT(ledger.enqueue("queued") == pixcake::ResultCode::Ok);
  EXPECT(ledger.cancel("queued") == pixcake::ResultCode::Ok);
  const auto* queued = kept.last("ledger.task.terminal", "queued");
  EXPECT(queued != nullptr && queued->phase == "排队中" && !queued->charged && queued->module == "ledger");

  EXPECT(ledger.quote(quote_of("running", "官方", 30)) == pixcake::ResultCode::Ok);
  EXPECT(ledger.enqueue("running") == pixcake::ResultCode::Ok);
  EXPECT(ledger.start_generating("running") == pixcake::ResultCode::Ok);
  EXPECT(ledger.cancel("running") == pixcake::ResultCode::Ok);
  const auto* running = kept.last("ledger.task.terminal", "running");
  EXPECT(running != nullptr && running->phase == "生成中" && running->charged);
  EXPECT(ledger.creative_points() == 70);
  EXPECT(log.count("ledger.task.terminal") == 2);
}


void test_o12_repeated_success_is_deduplicated() {
  LedgerRecords kept;
  pixcake::CountingLogSink log(&kept);
  auto ledger = charged(version_930(), 100, 1);
  ledger.set_log(&log);
  EXPECT(ledger.quote(quote_of("task-1", "官方", 30)) == pixcake::ResultCode::Ok);
  EXPECT(ledger.enqueue("task-1") == pixcake::ResultCode::Ok);
  EXPECT(ledger.start_generating("task-1") == pixcake::ResultCode::Ok);
  EXPECT(ledger.start_generating("task-1") == pixcake::ResultCode::Ok);
  EXPECT(ledger.succeed("task-1") == pixcake::ResultCode::Ok);
  EXPECT(ledger.succeed("task-1") == pixcake::ResultCode::Ok);
  EXPECT(ledger.creative_points() == 70);
  EXPECT(log.count("ledger.dedup") == 2);
  EXPECT(log.count("ledger.task.terminal") == 1);
  const auto* success = kept.last("ledger.task.terminal", "task-1");
  EXPECT(success != nullptr && success->phase == "生成中" && success->message.find("成功") != std::string::npos &&
         success->charged);
  const auto* dedup = kept.last("ledger.dedup", "task-1");
  EXPECT(dedup != nullptr && dedup->phase == "task");
}

// P-23：当前状态快照随事件增量维护，与重开时全量折叠的结果一致（随机操作序列）。
void test_p23_cached_snapshot_matches_full_fold() {
  auto ledger = charged(version_930(), 500, 20);
  unsigned seed = 7;
  const auto next = [&seed]() { seed = seed * 1103515245u + 12345u; return (seed >> 8) % 1000u; };
  const char* origins[] = {"官方", "工具包", "大师精选"};
  for (int step = 0; step < 600; ++step) {
    const std::string task = "t" + std::to_string(next() % 40);
    switch (next() % 9) {
      case 0: { auto q = quote_of(task, origins[next() % 3], static_cast<std::int64_t>(next() % 50)); q.tool_id = "tool"; q.member = next() % 2; q.subscribed = next() % 2; ledger.quote(q); break; }
      case 1: ledger.enqueue(task); break;
      case 2: ledger.start_generating(task); break;
      case 3: ledger.accelerate(task); break;
      case 4: ledger.cancel(task); break;
      case 5: ledger.crash(task); break;
      case 6: ledger.succeed(task); break;
      case 7: ledger.stroke(task, "s" + std::to_string(next() % 60)); break;
      default: ledger.settle_exports({pixcake::ExportProduct("e" + std::to_string(next() % 30), "o" + std::to_string(next() % 5), false, true)}); break;
    }
    const auto reopened = pixcake::CreditLedger::open(version_930(), ledger.events());
    EXPECT(reopened.creative_points() == ledger.creative_points());
    EXPECT(reopened.export_sheets() == ledger.export_sheets());
    EXPECT(reopened.task_statuses() == ledger.task_statuses());
  }
}

// P-23：余额查询与任务操作不随事件数线性变慢（2 万事件、2 万次查询，原实现每次全量折叠）。
void test_p23_balance_queries_do_not_refold() {
  auto ledger = charged(version_930(), 1000000, 0);
  const auto began = std::chrono::steady_clock::now();
  for (int index = 0; index < 5000; ++index) {
    const std::string task = "task-" + std::to_string(index);
    EXPECT(ledger.quote(quote_of(task, "官方", 1)) == pixcake::ResultCode::Ok);
    EXPECT(ledger.enqueue(task) == pixcake::ResultCode::Ok);
    EXPECT(ledger.start_generating(task) == pixcake::ResultCode::Ok);
    EXPECT(ledger.succeed(task) == pixcake::ResultCode::Ok);
  }
  std::int64_t sum = 0;
  for (int index = 0; index < 20000; ++index) sum += ledger.creative_points();
  const auto elapsed = std::chrono::steady_clock::now() - began;
  EXPECT(sum > 0);
  EXPECT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() < 1000);
}

// P-23：被移走的账本对象仍可安全查询（快照为空，不是空指针）。
void test_p23_moved_from_ledger_is_empty() {
  auto ledger = charged(version_930(), 100, 3);
  pixcake::CreditLedger moved = std::move(ledger);
  EXPECT(moved.creative_points() == 100 && moved.export_sheets() == 3);
  EXPECT(ledger.creative_points() == 0 && ledger.export_sheets() == 0);  // NOLINT(bugprone-use-after-move)
  EXPECT(ledger.task_statuses().empty() && ledger.events().empty());
  EXPECT(ledger.credit_points(5) == pixcake::ResultCode::Ok && ledger.creative_points() == 5);
  pixcake::CreditLedger assigned = charged(version_930(), 1, 0);
  assigned = std::move(moved);
  EXPECT(assigned.creative_points() == 100 && moved.creative_points() == 0);  // NOLINT(bugprone-use-after-move)
}

int main() {
  test_p23_moved_from_ledger_is_empty();
  test_p23_cached_snapshot_matches_full_fold();
  test_p23_balance_queries_do_not_refold();
  test_o09_cancel_position_and_charge_are_logged();
  test_o12_repeated_success_is_deduplicated();
  test_b103_balances_stay_separate_and_queue_cancel_is_free();
  test_b64_accelerate_is_charged_once_and_strokes_dedupe();
  test_free_queue_switches_and_master_allowance();
  test_b109_b110_export_sheets_count_originals_once_per_settlement();
  test_export_batch_identity_deduplicates_partial_completion_but_not_new_batch();
  test_sqlite_handles_reject_stale_duplicate_charge();
  test_master_acceleration_does_not_bypass_subscription();
  if (g_failures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  return 0;
}
