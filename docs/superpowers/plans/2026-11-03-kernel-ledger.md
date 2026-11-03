# 创意点数与导出张数 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 用一张追加事件记录创意点数和导出张数，任务状态按契约折叠；重开时重放事件，缓存和重放不一致就改缓存。

**Architecture:** `kernel/ledger` 只折叠事件，不查工具名，也不打开 SQLite。报价上的点数、是否高算力、是否批量、会员和订阅都由调用方传入。两套免点开关是两个独立布尔值。`storage/sqlite` 把事件和缓存写进调用方给的库文件。项目库的 `user_version` 仍是 3，账本不改那份表。

**Tech Stack:** C++17、CMake、CTest、系统自带的 `sqlite3`。测试是独立可执行文件，不引入第三方测试框架。

## Global Constraints

- 两套账：`creative_points` 是创意点数，`export_sheets` 是导出张数。没有一个字段或一个函数同时表示两套余额（需求第 17.1 节、契约 C-46）。
- 任务状态只有：已报价、排队中、生成中、成功、失败、排队取消、生成中取消（架构第 6.11 节）。
- 排队中取消不扣点。生成中取消不退已扣的点。生成进程崩溃视同生成中取消的记账：不退点，状态写成失败（架构第 6.11 节、契约 C-47、边界 B-103、B-105）。
- 排队中崩溃且还没进入生成：回到排队，不新增扣点，已扣的加速点不退（架构第 11 节、边界 B-104）。
- 同一任务编号再次报价、再次排队或再次开始生成：不第二次扣点，状态保持第一次的结果（契约 C-47、边界 B-63）。
- 排队中改加速：立刻扣报价上的 `acceleration_points`，并离开免点。之后的开始生成不再扣 `quoted_points`。加速消耗为 0 时也算离开免点（架构第 6.11 节）。
- 每笔自动消除在松开时扣 10 点。10 是架构表里的规则，不是工具价。笔触编号全局去重，第二次不扣（契约 C-47、边界 B-64）。
- 点数不够：返回点数或张数不足，不追加事件，余额和状态保持调用前（契约 C-20）。
- 张数只在导出成品结算时扣，不在入队时扣。同一次结算里，同一原图只要有一张不是「无水印第二份」的成品，就扣 1 张；同一原图的其他成品不再加。没有成品不扣。同一导出任务编号重放不第二次扣。另一次结算使用新的导出任务编号，再按同样规则扣（契约 C-48、边界 B-109、B-110、B-63）。
- 免点资格在账本里算，不用一个「是否免点」布尔值代替两套开关。官方工具还要是会员，工具包工具还要已订阅。高算力或批量生成永远按 `quoted_points` 扣。9.3.0 的两个开关值由测试传入，不写死在账本里（需求第 17.1 节、契约 C-09）。
- 大师精选：同一 `tool_id` 前 3 次开始生成不扣点；第 4 次起未订阅则状态冲突，已订阅且不是高算力、不是批量则不扣点。这 3 次不由上面两套版本开关决定（需求第 17.3 节）。
- 报价、加速点数、是否高算力来自调用方。账本源码不出现需求第 17.2 节的工具名。唯一允许的点数字面量是自动消除的 10（契约 C-08、质量 Q-22）。
- 来源不是 `官方`、`工具包`、`大师精选`，或点数为负，或任务编号、导出编号、原图编号、大师精选的工具编号、笔触编号为空：状态冲突，不追加事件。
- `kernel/ledger` 不包含 Qt 头，不包含 `sqlite3.h`，不包含 `exportq` 的私有头，不调用 `system`。
- 执行时只在本地提交，不推远端，不建 Pull Request。

本计划明确不做：需求第 17.2 节和附录 A 的默认点数表、导出队列、成品字节、图层落层、支付、命令入口、`kernel-cli`、极速导出开关。项目库 schema 不升版。

---

### Task 1: 折叠两套余额和任务状态

**Files:**
- Create: `kernel/ledger/include/pixcake/ledger/credit_ledger.hpp`
- Create: `kernel/ledger/src/credit_ledger.cpp`
- Create: `kernel/ledger/CMakeLists.txt`
- Create: `tests/kernel/ledger/test_ledger.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `ResultCode`
- Produces: `FreeQueuePolicy`、`TaskQuote`、`ExportProduct`、`LedgerEvent`、`CreditLedger`

- [ ] **Step 1: Write the failing test**

把 `tests/kernel/ledger/test_ledger.cpp` 写成下面的文件。根 `CMakeLists.txt` 在 `kernel/sync` 后加入 `add_subdirectory(kernel/ledger)`。`tests/CMakeLists.txt` 在 `test_sync` 后加入 `test_ledger`，链接 `pixcake_ledger`。`kernel/ledger/CMakeLists.txt` 先只编译一个空的 `src/credit_ledger.cpp`，让失败停在缺少声明。

```cpp
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
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake -S . -B build && cmake --build build --target test_ledger`

Expected: 编译失败，因为还没有 `pixcake/ledger/credit_ledger.hpp`。

- [ ] **Step 3: Write minimal implementation**

`kernel/ledger/include/pixcake/ledger/credit_ledger.hpp`：

```cpp
#pragma once

#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace pixcake {

struct FreeQueuePolicy {
  bool official_member_free_queue = false;
  bool subscribed_package_non_high_compute_free_queue = false;
};

struct TaskQuote {
  std::string task_id;
  std::string tool_id;
  std::string origin;
  bool high_compute = false;
  bool batch_generation = false;
  bool member = false;
  bool subscribed = false;
  std::int64_t quoted_points = 0;
  std::int64_t acceleration_points = 0;
};

struct ExportProduct {
  std::string export_id;
  std::string original_id;
  bool watermark_free_second = false;
  bool product_exists = false;
};

struct LedgerEvent {
  std::string kind;
  std::string task_id;
  std::string tool_id;
  std::string stroke_id;
  std::string origin;
  std::string original_id;
  std::string export_id;
  bool high_compute = false;
  bool batch_generation = false;
  bool member = false;
  bool subscribed = false;
  bool consumed_master_free = false;
  std::int64_t quoted_points = 0;
  std::int64_t acceleration_points = 0;
  std::int64_t points_delta = 0;
  std::int64_t sheets_delta = 0;
};

class CreditLedger {
 public:
  static CreditLedger open(FreeQueuePolicy policy, std::vector<LedgerEvent> events);

  ResultCode credit_points(std::int64_t amount);
  ResultCode credit_sheets(std::int64_t amount);
  ResultCode quote(const TaskQuote& quote);
  ResultCode enqueue(const std::string& task_id);
  ResultCode start_generating(const std::string& task_id);
  ResultCode accelerate(const std::string& task_id);
  ResultCode cancel(const std::string& task_id);
  ResultCode crash(const std::string& task_id);
  ResultCode succeed(const std::string& task_id);
  ResultCode fail(const std::string& task_id);
  ResultCode stroke(const std::string& task_id, const std::string& stroke_id);
  ResultCode settle_exports(const std::vector<ExportProduct>& products);

  std::int64_t creative_points() const;
  std::int64_t export_sheets() const;
  std::string task_status(const std::string& task_id) const;
  std::vector<std::pair<std::string, std::string>> task_statuses() const;
  const std::vector<LedgerEvent>& events() const;

 private:
  CreditLedger(FreeQueuePolicy policy, std::vector<LedgerEvent> events);

  FreeQueuePolicy policy_;
  std::vector<LedgerEvent> events_;
};

}  // namespace pixcake
```

`kernel/ledger/src/credit_ledger.cpp` 整文件如下。自动消除的 10 只出现在 `kStrokePoints`。

```cpp
#include "pixcake/ledger/credit_ledger.hpp"

#include <map>
#include <set>
#include <utility>

namespace pixcake {
namespace {

// 架构第 6.11 节：每一笔自动消除松开记 10 点。
constexpr std::int64_t kStrokePoints = 10;

struct TaskView {
  std::string status;
  LedgerEvent quote;
  bool accelerated = false;
};

struct Snapshot {
  std::int64_t points = 0;
  std::int64_t sheets = 0;
  std::map<std::string, TaskView> tasks;
  std::map<std::string, int> master_free;
  std::set<std::string> strokes;
  std::set<std::string> settled_exports;
};

Snapshot fold(const std::vector<LedgerEvent>& events) {
  Snapshot shot;
  for (const auto& event : events) {
    shot.points += event.points_delta;
    shot.sheets += event.sheets_delta;
    if (event.kind == "quote") {
      TaskView task;
      task.status = "已报价";
      task.quote = event;
      shot.tasks[event.task_id] = std::move(task);
    } else if (event.kind == "queue") {
      shot.tasks[event.task_id].status = "排队中";
    } else if (event.kind == "start") {
      shot.tasks[event.task_id].status = "生成中";
      if (event.consumed_master_free) {
        shot.master_free[event.tool_id] += 1;
      }
    } else if (event.kind == "accelerate") {
      shot.tasks[event.task_id].status = "排队中";
      shot.tasks[event.task_id].accelerated = true;
    } else if (event.kind == "cancel-queued") {
      shot.tasks[event.task_id].status = "排队取消";
    } else if (event.kind == "cancel-generating") {
      shot.tasks[event.task_id].status = "生成中取消";
    } else if (event.kind == "crash") {
      if (shot.tasks[event.task_id].status == "生成中") {
        shot.tasks[event.task_id].status = "失败";
      }
    } else if (event.kind == "succeed") {
      shot.tasks[event.task_id].status = "成功";
    } else if (event.kind == "fail") {
      shot.tasks[event.task_id].status = "失败";
    } else if (event.kind == "stroke") {
      shot.strokes.insert(event.stroke_id);
    } else if (event.kind == "export-sheet") {
      shot.settled_exports.insert(event.export_id);
    }
  }
  return shot;
}

bool known_origin(const std::string& origin) {
  return origin == "官方" || origin == "工具包" || origin == "大师精选";
}

bool free_queue(const FreeQueuePolicy& policy, const LedgerEvent& quote) {
  if (quote.high_compute || quote.batch_generation) {
    return false;
  }
  if (quote.origin == "官方") {
    return policy.official_member_free_queue && quote.member;
  }
  if (quote.origin == "工具包") {
    return policy.subscribed_package_non_high_compute_free_queue && quote.subscribed;
  }
  return false;
}

LedgerEvent taken_from(const TaskQuote& quote) {
  LedgerEvent event;
  event.kind = "quote";
  event.task_id = quote.task_id;
  event.tool_id = quote.tool_id;
  event.origin = quote.origin;
  event.high_compute = quote.high_compute;
  event.batch_generation = quote.batch_generation;
  event.member = quote.member;
  event.subscribed = quote.subscribed;
  event.quoted_points = quote.quoted_points;
  event.acceleration_points = quote.acceleration_points;
  return event;
}

}  // namespace

CreditLedger::CreditLedger(FreeQueuePolicy policy, std::vector<LedgerEvent> events)
    : policy_(std::move(policy)), events_(std::move(events)) {}

CreditLedger CreditLedger::open(FreeQueuePolicy policy, std::vector<LedgerEvent> events) {
  return CreditLedger(std::move(policy), std::move(events));
}

const std::vector<LedgerEvent>& CreditLedger::events() const { return events_; }

std::int64_t CreditLedger::creative_points() const { return fold(events_).points; }

std::int64_t CreditLedger::export_sheets() const { return fold(events_).sheets; }

std::string CreditLedger::task_status(const std::string& task_id) const {
  const auto shot = fold(events_);
  const auto found = shot.tasks.find(task_id);
  if (found == shot.tasks.end()) {
    return "";
  }
  return found->second.status;
}

std::vector<std::pair<std::string, std::string>> CreditLedger::task_statuses() const {
  std::vector<std::pair<std::string, std::string>> statuses;
  for (const auto& item : fold(events_).tasks) {
    statuses.emplace_back(item.first, item.second.status);
  }
  return statuses;
}

ResultCode CreditLedger::credit_points(std::int64_t amount) {
  if (amount <= 0) {
    return ResultCode::StateConflict;
  }
  LedgerEvent event;
  event.kind = "credit-points";
  event.points_delta = amount;
  events_.push_back(std::move(event));
  return ResultCode::Ok;
}

ResultCode CreditLedger::credit_sheets(std::int64_t amount) {
  if (amount <= 0) {
    return ResultCode::StateConflict;
  }
  LedgerEvent event;
  event.kind = "credit-sheets";
  event.sheets_delta = amount;
  events_.push_back(std::move(event));
  return ResultCode::Ok;
}

ResultCode CreditLedger::quote(const TaskQuote& quote) {
  if (quote.task_id.empty()) {
    return ResultCode::StateConflict;
  }
  const auto shot = fold(events_);
  if (shot.tasks.find(quote.task_id) != shot.tasks.end()) {
    return ResultCode::Ok;
  }
  if (!known_origin(quote.origin) || quote.quoted_points < 0 || quote.acceleration_points < 0) {
    return ResultCode::StateConflict;
  }
  if (quote.origin == "大师精选" && quote.tool_id.empty()) {
    return ResultCode::StateConflict;
  }
  events_.push_back(taken_from(quote));
  return ResultCode::Ok;
}

ResultCode CreditLedger::enqueue(const std::string& task_id) {
  const auto shot = fold(events_);
  const auto found = shot.tasks.find(task_id);
  if (found == shot.tasks.end()) {
    return ResultCode::StateConflict;
  }
  if (found->second.status == "排队中") {
    return ResultCode::Ok;
  }
  if (found->second.status != "已报价") {
    return ResultCode::StateConflict;
  }
  LedgerEvent event;
  event.kind = "queue";
  event.task_id = task_id;
  events_.push_back(std::move(event));
  return ResultCode::Ok;
}

ResultCode CreditLedger::start_generating(const std::string& task_id) {
  const auto shot = fold(events_);
  const auto found = shot.tasks.find(task_id);
  if (found == shot.tasks.end()) {
    return ResultCode::StateConflict;
  }
  if (found->second.status == "已报价") {
    return ResultCode::StateConflict;
  }
  if (found->second.status != "排队中") {
    return ResultCode::Ok;
  }
  const LedgerEvent& quoted = found->second.quote;
  LedgerEvent event;
  event.kind = "start";
  event.task_id = task_id;
  event.tool_id = quoted.tool_id;
  std::int64_t charge = quoted.quoted_points;
  if (found->second.accelerated) {
    charge = 0;
  } else if (quoted.origin == "大师精选" && !quoted.high_compute && !quoted.batch_generation) {
    const auto used = shot.master_free.find(quoted.tool_id);
    const int count = used == shot.master_free.end() ? 0 : used->second;
    if (count < 3) {
      charge = 0;
      event.consumed_master_free = true;
    } else if (!quoted.subscribed) {
      return ResultCode::StateConflict;
    } else {
      charge = 0;
    }
  } else if (free_queue(policy_, quoted)) {
    charge = 0;
  }
  if (shot.points < charge) {
    return ResultCode::InsufficientBalance;
  }
  event.points_delta = -charge;
  events_.push_back(std::move(event));
  return ResultCode::Ok;
}

ResultCode CreditLedger::accelerate(const std::string& task_id) {
  const auto shot = fold(events_);
  const auto found = shot.tasks.find(task_id);
  if (found == shot.tasks.end() || found->second.status != "排队中") {
    return ResultCode::StateConflict;
  }
  if (found->second.accelerated) {
    return ResultCode::Ok;
  }
  const std::int64_t cost = found->second.quote.acceleration_points;
  if (shot.points < cost) {
    return ResultCode::InsufficientBalance;
  }
  LedgerEvent event;
  event.kind = "accelerate";
  event.task_id = task_id;
  event.points_delta = -cost;
  events_.push_back(std::move(event));
  return ResultCode::Ok;
}

ResultCode CreditLedger::cancel(const std::string& task_id) {
  const auto shot = fold(events_);
  const auto found = shot.tasks.find(task_id);
  if (found == shot.tasks.end()) {
    return ResultCode::StateConflict;
  }
  LedgerEvent event;
  event.task_id = task_id;
  if (found->second.status == "排队中") {
    event.kind = "cancel-queued";
  } else if (found->second.status == "生成中") {
    event.kind = "cancel-generating";
  } else {
    return ResultCode::StateConflict;
  }
  events_.push_back(std::move(event));
  return ResultCode::Ok;
}

ResultCode CreditLedger::crash(const std::string& task_id) {
  const auto shot = fold(events_);
  const auto found = shot.tasks.find(task_id);
  if (found == shot.tasks.end()) {
    return ResultCode::StateConflict;
  }
  if (found->second.status != "排队中" && found->second.status != "生成中") {
    return ResultCode::StateConflict;
  }
  LedgerEvent event;
  event.kind = "crash";
  event.task_id = task_id;
  events_.push_back(std::move(event));
  return ResultCode::Ok;
}

ResultCode CreditLedger::succeed(const std::string& task_id) {
  const auto shot = fold(events_);
  const auto found = shot.tasks.find(task_id);
  if (found == shot.tasks.end()) {
    return ResultCode::StateConflict;
  }
  if (found->second.status == "成功") {
    return ResultCode::Ok;
  }
  if (found->second.status != "生成中") {
    return ResultCode::StateConflict;
  }
  LedgerEvent event;
  event.kind = "succeed";
  event.task_id = task_id;
  events_.push_back(std::move(event));
  return ResultCode::Ok;
}

ResultCode CreditLedger::fail(const std::string& task_id) {
  const auto shot = fold(events_);
  const auto found = shot.tasks.find(task_id);
  if (found == shot.tasks.end()) {
    return ResultCode::StateConflict;
  }
  if (found->second.status == "失败") {
    return ResultCode::Ok;
  }
  if (found->second.status != "排队中" && found->second.status != "生成中") {
    return ResultCode::StateConflict;
  }
  LedgerEvent event;
  event.kind = "fail";
  event.task_id = task_id;
  events_.push_back(std::move(event));
  return ResultCode::Ok;
}

ResultCode CreditLedger::stroke(const std::string& task_id, const std::string& stroke_id) {
  if (stroke_id.empty()) {
    return ResultCode::StateConflict;
  }
  const auto shot = fold(events_);
  if (shot.strokes.count(stroke_id) != 0) {
    return ResultCode::Ok;
  }
  const auto found = shot.tasks.find(task_id);
  if (found == shot.tasks.end() || found->second.status != "生成中") {
    return ResultCode::StateConflict;
  }
  if (shot.points < kStrokePoints) {
    return ResultCode::InsufficientBalance;
  }
  LedgerEvent event;
  event.kind = "stroke";
  event.task_id = task_id;
  event.stroke_id = stroke_id;
  event.points_delta = -kStrokePoints;
  events_.push_back(std::move(event));
  return ResultCode::Ok;
}

ResultCode CreditLedger::settle_exports(const std::vector<ExportProduct>& products) {
  std::set<std::string> seen;
  for (const auto& product : products) {
    if (product.export_id.empty() || product.original_id.empty() || !seen.insert(product.export_id).second) {
      return ResultCode::StateConflict;
    }
  }
  const auto shot = fold(events_);
  std::map<std::string, std::vector<const ExportProduct*>> groups;
  for (const auto& product : products) {
    if (shot.settled_exports.count(product.export_id) != 0) {
      continue;
    }
    groups[product.original_id].push_back(&product);
  }
  std::vector<LedgerEvent> added;
  std::int64_t needed = 0;
  for (const auto& group : groups) {
    std::vector<const ExportProduct*> chargeable;
    for (const ExportProduct* product : group.second) {
      if (product->product_exists && !product->watermark_free_second) {
        chargeable.push_back(product);
      }
    }
    if (chargeable.empty()) {
      continue;
    }
    needed += 1;
    for (std::size_t index = 0; index < chargeable.size(); ++index) {
      LedgerEvent event;
      event.kind = "export-sheet";
      event.export_id = chargeable[index]->export_id;
      event.original_id = chargeable[index]->original_id;
      event.sheets_delta = index == 0 ? -1 : 0;
      added.push_back(std::move(event));
    }
  }
  if (shot.sheets < needed) {
    return ResultCode::InsufficientBalance;
  }
  events_.insert(events_.end(), added.begin(), added.end());
  return ResultCode::Ok;
}

}  // namespace pixcake
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_ledger && ctest --test-dir build -R '^test_ledger$' --output-on-failure`

Expected: `test_ledger` 通过。

- [ ] **Step 5: Commit**

```bash
git add kernel/ledger tests/kernel/ledger CMakeLists.txt tests/CMakeLists.txt docs/superpowers/plans/2026-11-03-kernel-ledger.md
git commit -m "$(cat <<'EOF'
feat: fold creative points and export sheets from ledger events

EOF
)"
```

执行时只在本地提交，不推远端，不建 Pull Request。

---

### Task 2: 重放事件并改正缓存

**Files:**
- Create: `kernel/ledger/include/pixcake/ledger/ledger_book.hpp`
- Create: `kernel/ledger/src/ledger_book.cpp`
- Create: `storage/sqlite/include/pixcake/storage/sqlite_ledger_log.hpp`
- Create: `storage/sqlite/src/sqlite_ledger_log.cpp`
- Modify: `kernel/ledger/CMakeLists.txt`
- Modify: `storage/sqlite/CMakeLists.txt`
- Modify: `tests/kernel/ledger/test_ledger.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `tests/arch/check_dependencies.cmake`

**Interfaces:**
- Consumes: `CreditLedger`、`LedgerEvent`
- Produces: `LedgerEventLog`、`LedgerBook`、`SqliteLedgerLog`

- [ ] **Step 1: Write the failing test**

在 `test_ledger.cpp` 增加持久化测试，并从 `main` 调用。测试链接改为 `pixcake_ledger pixcake_storage`。包含 `support/temp_dir.hpp`。

```cpp
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
```

测试文件要包含 `sqlite3.h`。项目库测试里的 `user_version == 3` 不能因为这份账本改掉。

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_ledger`

Expected: 编译失败，因为还没有 `LedgerBook`。

- [ ] **Step 3: Write minimal implementation**

`LedgerEventLog` 提供 `load`、`append`、`read_cache`、`write_cache`、`in_transaction`。缓存是点数、张数，以及每个任务编号到状态的列表。没有缓存行时 `read_cache` 成功并给出空 optional。

`LedgerBook::open` 读出全部事件，交给 `CreditLedger::open`。缓存缺失，或点数、张数、任务状态和折叠结果不同：在同一个事务里用折叠结果覆盖缓存。写缓存失败则 `open` 失败，不交出一本已经改过内存、库却没改成的账。

`LedgerBook` 的每个改动方法先在副本上调用 `CreditLedger`。副本失败则原账本和库都不改。副本成功则在一个事务里只追加新增的事件尾部，再写缓存。事务失败时内存中的 `CreditLedger` 仍是调用前那份。

SQLite 文件由调用方给路径。新库执行：

```sql
CREATE TABLE ledger_event (
  seq INTEGER PRIMARY KEY,
  kind TEXT NOT NULL,
  task_id TEXT NOT NULL,
  tool_id TEXT NOT NULL,
  stroke_id TEXT NOT NULL,
  origin TEXT NOT NULL,
  original_id TEXT NOT NULL,
  export_id TEXT NOT NULL,
  high_compute INTEGER NOT NULL,
  batch_generation INTEGER NOT NULL,
  member INTEGER NOT NULL,
  subscribed INTEGER NOT NULL,
  consumed_master_free INTEGER NOT NULL,
  quoted_points INTEGER NOT NULL,
  acceleration_points INTEGER NOT NULL,
  points_delta INTEGER NOT NULL,
  sheets_delta INTEGER NOT NULL
);
CREATE TABLE ledger_cache (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  creative_points INTEGER NOT NULL,
  export_sheets INTEGER NOT NULL
);
CREATE TABLE ledger_task_cache (
  task_id TEXT PRIMARY KEY,
  status TEXT NOT NULL
);
```

`user_version` 为 0 时建表并写成 1。已经是 1 则直接开。其他版本返回状态冲突，不执行建表，也不改 `user_version`。文本列只用 `sqlite3_bind_text`，不把任务编号拼进 SQL。

`kernel/ledger` 的源文件不包含 `sqlite3.h`。SQLite 只出现在 `storage/sqlite/src/sqlite_ledger_log.cpp`。`pixcake_storage` 额外编译这个文件，并公开链接 `pixcake_ledger`。

`tests/arch/check_dependencies.cmake` 在规则 11 后增加：

```cmake
# 12. 账本不链接 SQLite，也不抄工具点数表。
scan("${SOURCE_DIR}/kernel/ledger" "rule 12" "sqlite3.h" "面部重绘")
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_ledger test_project_paths && ctest --test-dir build --output-on-failure`

Expected: 全量测试通过。`test_ledger` 覆盖重放和缓存改正。`test_project_paths` 仍要求项目库的未来版本是 4，当前版本是 3。`arch_dependencies` 通过。

- [ ] **Step 5: Commit**

```bash
git add kernel/ledger storage/sqlite tests/kernel/ledger tests/CMakeLists.txt tests/arch/check_dependencies.cmake docs/superpowers/plans/2026-11-03-kernel-ledger.md
git commit -m "$(cat <<'EOF'
feat: replay the ledger log and repair its cached balances

EOF
)"
```

执行时只在本地提交，不推远端，不建 Pull Request。
