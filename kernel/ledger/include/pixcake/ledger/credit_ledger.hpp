#pragma once

#include "pixcake/support/log_sink.hpp"
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
  std::string batch_id;

  ExportProduct() = default;
  ExportProduct(std::string export_id_value, std::string original_id_value, bool watermark_free_value,
                bool product_exists_value, std::string batch_id_value = {})
      : export_id(std::move(export_id_value)),
        original_id(std::move(original_id_value)),
        watermark_free_second(watermark_free_value),
        product_exists(product_exists_value),
        batch_id(std::move(batch_id_value)) {}
};

struct LedgerEvent {
  std::string kind;
  std::string task_id;
  std::string tool_id;
  std::string stroke_id;
  std::string origin;
  std::string original_id;
  std::string export_id;
  std::string batch_id;
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

// 排队中取消不扣，生成中取消不退，同一任务编号和笔触编号不双扣
class CreditLedger {
 public:
  static CreditLedger open(FreeQueuePolicy policy, std::vector<LedgerEvent> events);

  // 日志可以为空。不为空时：生成任务成功、失败、取消写 ledger.task.terminal（phase 为转换前状态，
  // message 为终态，charged 为这个任务是否扣过点）；崩溃写 ledger.task.crash；同一任务编号、
  // 笔触编号或导出编号的重复记账被挡住时写 ledger.dedup（phase 为 task/stroke/export）。
  void set_log(LogSink* log) { log_ = log; }

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
  LogSink* log_ = nullptr;

  bool task_charged(const std::string& task_id) const;
  void note_terminal(const char* event, const std::string& task_id, const std::string& before,
                     const char* after) const;
  void note_dedup(const std::string& id, const char* kind) const;
};

}  // namespace pixcake
