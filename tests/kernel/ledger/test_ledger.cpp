#include "pixcake/ledger/credit_ledger.hpp"

#include <cstdio>
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
  EXPECT(ledger.credit_sheets(sheets) == pixcake::ResultCode::Ok);
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
  const auto before = poor.events().size();
  EXPECT(poor.settle_exports(again) == pixcake::ResultCode::InsufficientBalance);
  EXPECT(poor.export_sheets() == 0);
  EXPECT(poor.events().size() == before);
}

}  // namespace

int main() {
  test_balances_stay_separate_and_queue_cancel_is_free();
  test_generating_cancel_and_crash_do_not_refund();
  test_accelerate_is_charged_once_and_strokes_dedupe();
  test_free_queue_switches_and_master_allowance();
  test_export_sheets_count_originals_once_per_settlement();
  if (g_failures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  return 0;
}
