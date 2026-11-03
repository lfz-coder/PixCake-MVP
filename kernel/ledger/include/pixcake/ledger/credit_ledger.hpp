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
