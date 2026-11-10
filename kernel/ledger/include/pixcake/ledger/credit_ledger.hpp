#pragma once

#include "pixcake/support/log_sink.hpp"
#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace pixcake {

inline constexpr char kLedgerOriginOfficial[] = "官方";
inline constexpr char kLedgerOriginPackage[] = "工具包";
inline constexpr char kLedgerOriginMaster[] = "大师精选";

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

// 一次导出成品。original_id 是计费用的“同一图片”标识：同一个 original_id 只扣一次张数
// （test 版规则：同一商家下同一图片只计费一次，虚拟副本算同一张；
// docs/superpowers/evidence/2026-11-07-export-credit-observation.md）。
// chargeable 为假表示原图或只用了免费功能，不扣。
struct ExportProduct {
  std::string export_id;
  std::string original_id;
  bool watermark_free_second = false;
  bool product_exists = false;
  std::string batch_id;
  bool chargeable = true;

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
  ~CreditLedger();
  CreditLedger(const CreditLedger& other);
  CreditLedger& operator=(const CreditLedger& other);
  CreditLedger(CreditLedger&& other) noexcept;
  CreditLedger& operator=(CreditLedger&& other) noexcept;

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
  // 已有成品的导出都记一笔（以后同一导出编号不再处理）；每个 original_id 第一次出现可计费成品时扣 1 张，
  // 之后不再扣。张数不够时什么都不记，返回 InsufficientBalance。
  ResultCode settle_exports(const std::vector<ExportProduct>& products);
  bool export_settled(const std::string& export_id) const;
  // 这一次导出扣了张数（导出明细的“实际计费”）。
  bool export_charged(const std::string& export_id) const;
  bool original_charged(const std::string& original_id) const;
  std::vector<std::string> charged_originals() const;

  std::int64_t creative_points() const;
  std::int64_t export_sheets() const;
  std::string task_status(const std::string& task_id) const;
  std::vector<std::pair<std::string, std::string>> task_statuses() const;
  const std::vector<LedgerEvent>& events() const;

 private:
  CreditLedger(FreeQueuePolicy policy, std::vector<LedgerEvent> events);

  // 当前状态快照（性能清单 P-23）：open 时折叠一次，之后每追加一条事件只应用这一条。
  struct Snapshot;
  static void apply(Snapshot& shot, const LedgerEvent& event);
  void append(LedgerEvent event);

  FreeQueuePolicy policy_;
  std::vector<LedgerEvent> events_;
  std::unique_ptr<Snapshot> snapshot_;
  LogSink* log_ = nullptr;

  bool task_charged(const std::string& task_id) const;
  void note_terminal(const char* event, const std::string& task_id, const std::string& before,
                     const char* after) const;
  void note_dedup(const std::string& id, const char* kind) const;
};

}  // namespace pixcake
