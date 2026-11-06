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
CreditLedger::CreditLedger(CreditLedger&& other) noexcept = default;
CreditLedger& CreditLedger::operator=(CreditLedger&& other) noexcept = default;

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
      event.batch_id = chargeable[index]->batch_id;
      event.sheets_delta = index == 0 ? -1 : 0;
      added.push_back(std::move(event));
    }
  }
  if (shot.sheets < needed) {
    return ResultCode::InsufficientBalance;
  }
  for (auto& event : added) append(std::move(event));
  return ResultCode::Ok;
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
