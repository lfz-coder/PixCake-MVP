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

struct CreditLedger::Snapshot {
  std::int64_t points = 0;
  std::int64_t sheets = 0;
  std::map<std::string, TaskView> tasks;
  std::map<std::string, int> master_free;
  std::set<std::string> strokes;
  std::set<std::string> settled_exports;
  std::set<std::string> settled_batches;
  std::set<std::string> charged_tasks;
  std::set<std::string> charged_originals;
  std::set<std::string> charged_exports;
};

void CreditLedger::apply(Snapshot& shot, const LedgerEvent& event) {
  shot.points += event.points_delta;
  shot.sheets += event.sheets_delta;
  if (event.points_delta < 0 && !event.task_id.empty()) {
    shot.charged_tasks.insert(event.task_id);
  }
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
    if (event.sheets_delta < 0) {
      shot.charged_originals.insert(event.original_id);
      shot.charged_exports.insert(event.export_id);
    }
    if (!event.batch_id.empty()) {
      shot.settled_batches.insert(event.batch_id + "\n" + event.original_id);
    }
  }
}

void CreditLedger::append(LedgerEvent event) {
  apply(*snapshot_, event);
  events_.push_back(std::move(event));
}

CreditLedger::CreditLedger(FreeQueuePolicy policy, std::vector<LedgerEvent> events)
    : policy_(std::move(policy)), events_(std::move(events)), snapshot_(std::make_unique<Snapshot>()) {
  for (const auto& event : events_) apply(*snapshot_, event);
}

CreditLedger::~CreditLedger() = default;
CreditLedger::CreditLedger(const CreditLedger& other)
    : policy_(other.policy_), events_(other.events_), snapshot_(std::make_unique<Snapshot>(*other.snapshot_)),
      log_(other.log_) {}
CreditLedger& CreditLedger::operator=(const CreditLedger& other) {
  if (this != &other) {
    policy_ = other.policy_;
    events_ = other.events_;
    snapshot_ = std::make_unique<Snapshot>(*other.snapshot_);
    log_ = other.log_;
  }
  return *this;
}
// 被移走的对象留下空事件和空快照，仍可安全查询（不留空指针）。
CreditLedger::CreditLedger(CreditLedger&& other) noexcept
    : policy_(std::move(other.policy_)), events_(std::move(other.events_)), snapshot_(std::move(other.snapshot_)),
      log_(other.log_) {
  other.events_.clear();
  other.snapshot_ = std::make_unique<Snapshot>();
}
CreditLedger& CreditLedger::operator=(CreditLedger&& other) noexcept {
  if (this != &other) {
    policy_ = std::move(other.policy_);
    events_ = std::move(other.events_);
    snapshot_ = std::move(other.snapshot_);
    log_ = other.log_;
    other.events_.clear();
    other.snapshot_ = std::make_unique<Snapshot>();
  }
  return *this;
}

CreditLedger CreditLedger::open(FreeQueuePolicy policy, std::vector<LedgerEvent> events) {
  return CreditLedger(std::move(policy), std::move(events));
}

const std::vector<LedgerEvent>& CreditLedger::events() const { return events_; }

std::int64_t CreditLedger::creative_points() const { return snapshot_->points; }

std::int64_t CreditLedger::export_sheets() const { return snapshot_->sheets; }

std::string CreditLedger::task_status(const std::string& task_id) const {
  const Snapshot& shot = *snapshot_;
  const auto found = shot.tasks.find(task_id);
  if (found == shot.tasks.end()) {
    return "";
  }
  return found->second.status;
}

std::vector<std::pair<std::string, std::string>> CreditLedger::task_statuses() const {
  std::vector<std::pair<std::string, std::string>> statuses;
  for (const auto& item : snapshot_->tasks) {
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
  append(std::move(event));
  return ResultCode::Ok;
}

ResultCode CreditLedger::credit_sheets(std::int64_t amount) {
  if (amount <= 0) {
    return ResultCode::StateConflict;
  }
  LedgerEvent event;
  event.kind = "credit-sheets";
  event.sheets_delta = amount;
  append(std::move(event));
  return ResultCode::Ok;
}

ResultCode CreditLedger::quote(const TaskQuote& quote) {
  if (quote.task_id.empty()) {
    return ResultCode::StateConflict;
  }
  const Snapshot& shot = *snapshot_;
  if (shot.tasks.find(quote.task_id) != shot.tasks.end()) {
    return ResultCode::Ok;
  }
  if (!known_origin(quote.origin) || quote.quoted_points < 0 || quote.acceleration_points < 0) {
    return ResultCode::StateConflict;
  }
  if (quote.origin == "大师精选" && quote.tool_id.empty()) {
    return ResultCode::StateConflict;
  }
  append(taken_from(quote));
  return ResultCode::Ok;
}

ResultCode CreditLedger::enqueue(const std::string& task_id) {
  const Snapshot& shot = *snapshot_;
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
  append(std::move(event));
  return ResultCode::Ok;
}

ResultCode CreditLedger::start_generating(const std::string& task_id) {
  const Snapshot& shot = *snapshot_;
  const auto found = shot.tasks.find(task_id);
  if (found == shot.tasks.end()) {
    return ResultCode::StateConflict;
  }
  if (found->second.status == "已报价") {
    return ResultCode::StateConflict;
  }
  if (found->second.status != "排队中") {
    note_dedup(task_id, "task");
    return ResultCode::Ok;
  }
  const LedgerEvent& quoted = found->second.quote;
  if (quoted.origin == "大师精选" && !quoted.high_compute && !quoted.batch_generation) {
    const auto used = shot.master_free.find(quoted.tool_id);
    const int count = used == shot.master_free.end() ? 0 : used->second;
    if (count >= 3 && !quoted.subscribed) {
      return ResultCode::StateConflict;
    }
  }
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
  append(std::move(event));
  return ResultCode::Ok;
}

ResultCode CreditLedger::accelerate(const std::string& task_id) {
  const Snapshot& shot = *snapshot_;
  const auto found = shot.tasks.find(task_id);
  if (found == shot.tasks.end() || found->second.status != "排队中") {
    return ResultCode::StateConflict;
  }
  if (found->second.accelerated) {
    note_dedup(task_id, "task");
    return ResultCode::Ok;
  }
  // Qualification is part of the decision to charge for acceleration.  In
  // particular, an unsubscribed Master Picks task that has consumed its
  // three free starts must be rejected before an acceleration event is
  // appended; charging first would leave a point debit for an operation that
  // cannot start.
  const LedgerEvent& quoted = found->second.quote;
  if (quoted.origin == "大师精选" && !quoted.high_compute && !quoted.batch_generation &&
      !quoted.subscribed) {
    const auto used = shot.master_free.find(quoted.tool_id);
    const int count = used == shot.master_free.end() ? 0 : used->second;
    if (count >= 3) {
      return ResultCode::StateConflict;
    }
  }
  const std::int64_t cost = found->second.quote.acceleration_points;
  if (shot.points < cost) {
    return ResultCode::InsufficientBalance;
  }
  LedgerEvent event;
  event.kind = "accelerate";
  event.task_id = task_id;
  event.points_delta = -cost;
  append(std::move(event));
  return ResultCode::Ok;
}

ResultCode CreditLedger::cancel(const std::string& task_id) {
  const Snapshot& shot = *snapshot_;
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
  const bool queued = event.kind == "cancel-queued";
  const std::string before = found->second.status;  // 快照随追加更新，先记下转换前状态
  append(std::move(event));
  note_terminal("ledger.task.terminal", task_id, before, queued ? "排队取消" : "生成中取消");
  return ResultCode::Ok;
}

ResultCode CreditLedger::crash(const std::string& task_id) {
  const Snapshot& shot = *snapshot_;
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
  const std::string before = found->second.status;
  append(std::move(event));
  note_terminal("ledger.task.crash", task_id, before, before == "生成中" ? "失败" : "排队中");
  return ResultCode::Ok;
}

ResultCode CreditLedger::succeed(const std::string& task_id) {
  const Snapshot& shot = *snapshot_;
  const auto found = shot.tasks.find(task_id);
  if (found == shot.tasks.end()) {
    return ResultCode::StateConflict;
  }
  if (found->second.status == "成功") {
    note_dedup(task_id, "task");
    return ResultCode::Ok;
  }
  if (found->second.status != "生成中") {
    return ResultCode::StateConflict;
  }
  LedgerEvent event;
  event.kind = "succeed";
  event.task_id = task_id;
  const std::string before = found->second.status;
  append(std::move(event));
  note_terminal("ledger.task.terminal", task_id, before, "成功");
  return ResultCode::Ok;
}

ResultCode CreditLedger::fail(const std::string& task_id) {
  const Snapshot& shot = *snapshot_;
  const auto found = shot.tasks.find(task_id);
  if (found == shot.tasks.end()) {
    return ResultCode::StateConflict;
  }
  if (found->second.status == "失败") {
    note_dedup(task_id, "task");
    return ResultCode::Ok;
  }
  if (found->second.status != "排队中" && found->second.status != "生成中") {
    return ResultCode::StateConflict;
  }
  LedgerEvent event;
  event.kind = "fail";
  event.task_id = task_id;
  const std::string before = found->second.status;
  append(std::move(event));
  note_terminal("ledger.task.terminal", task_id, before, "失败");
  return ResultCode::Ok;
}

ResultCode CreditLedger::stroke(const std::string& task_id, const std::string& stroke_id) {
  if (stroke_id.empty()) {
    return ResultCode::StateConflict;
  }
  const Snapshot& shot = *snapshot_;
  if (shot.strokes.count(stroke_id) != 0) {
    note_dedup(task_id, "stroke");
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
  append(std::move(event));
  return ResultCode::Ok;
}

ResultCode CreditLedger::settle_exports(const std::vector<ExportProduct>& products) {
  std::set<std::string> seen;
  for (const auto& product : products) {
    if (product.export_id.empty() || product.original_id.empty() || !seen.insert(product.export_id).second) {
      return ResultCode::StateConflict;
    }
  }
  const Snapshot& shot = *snapshot_;
  std::map<std::string, std::vector<const ExportProduct*>> groups;
  for (const auto& product : products) {
    const std::string batch_id = product.batch_id.empty() ? product.export_id : product.batch_id;
    if (shot.settled_exports.count(product.export_id) != 0 ||
        (!product.batch_id.empty() && shot.settled_batches.count(batch_id + "\n" + product.original_id) != 0)) {
      note_dedup(product.export_id, "export");
      continue;
    }
    const std::string group_key = product.batch_id.empty()
                                      ? product.original_id
                                      : product.batch_id + "\n" + product.original_id;
    groups[group_key].push_back(&product);
  }
  std::vector<LedgerEvent> added;
  std::int64_t needed = 0;
  std::set<std::string> charging;  // originals charged by this call
  for (const auto& group : groups) {
    std::vector<const ExportProduct*> existing;
    for (const ExportProduct* product : group.second) {
      if (product->product_exists) existing.push_back(product);
    }
    if (existing.empty()) continue;  // nothing was produced: retried later, never charged
    const std::string& original = existing.front()->original_id;
    bool charge = shot.charged_originals.count(original) == 0 && charging.count(original) == 0;
    bool charged_one = false;
    for (const ExportProduct* product : existing) {
      LedgerEvent event;
      event.kind = "export-sheet";
      event.export_id = product->export_id;
      event.original_id = product->original_id;
      event.batch_id = product->batch_id;
      const bool chargeable = product->chargeable && !product->watermark_free_second;
      if (charge && chargeable && !charged_one) {
        event.sheets_delta = -1;
        charged_one = true;
        charging.insert(original);
        needed += 1;
      }
      added.push_back(std::move(event));
    }
  }
  if (shot.sheets < needed) {
    return ResultCode::InsufficientBalance;
  }
  for (auto& event : added) append(std::move(event));
  return ResultCode::Ok;
}

bool CreditLedger::export_settled(const std::string& export_id) const {
  return snapshot_->settled_exports.count(export_id) != 0;
}

bool CreditLedger::export_charged(const std::string& export_id) const {
  return snapshot_->charged_exports.count(export_id) != 0;
}

bool CreditLedger::original_charged(const std::string& original_id) const {
  return snapshot_->charged_originals.count(original_id) != 0;
}

std::vector<std::string> CreditLedger::charged_originals() const {
  return {snapshot_->charged_originals.begin(), snapshot_->charged_originals.end()};
}

bool CreditLedger::task_charged(const std::string& task_id) const {
  return snapshot_->charged_tasks.count(task_id) != 0;
}

void CreditLedger::note_terminal(const char* event, const std::string& task_id, const std::string& before,
                                 const char* after) const {
  if (log_ == nullptr) return;
  LogRecord record;
  record.module = "ledger";
  record.event = event;
  record.task_id = task_id;
  record.phase = before;
  record.message = after;
  record.charged = task_charged(task_id);
  log_->write(record);
}

void CreditLedger::note_dedup(const std::string& id, const char* kind) const {
  if (log_ == nullptr) return;
  LogRecord record;
  record.module = "ledger";
  record.event = "ledger.dedup";
  record.task_id = id;
  record.phase = kind;
  record.message = "重复记账已挡住";
  record.deduplicated = true;
  record.charged = task_charged(id);
  log_->write(record);
}

}  // namespace pixcake
