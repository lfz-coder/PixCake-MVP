#pragma once

#include "pixcake/ledger/credit_ledger.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pixcake {

struct LedgerCache {
  std::int64_t creative_points = 0;
  std::int64_t export_sheets = 0;
  std::vector<std::pair<std::string, std::string>> task_statuses;
};

class LedgerEventLog {
 public:
  virtual ~LedgerEventLog() = default;

  virtual ResultCode load(std::vector<LedgerEvent>& out) = 0;
  virtual ResultCode append(const std::vector<LedgerEvent>& events) = 0;
  virtual ResultCode read_cache(std::optional<LedgerCache>& out) = 0;
  virtual ResultCode write_cache(const LedgerCache& cache) = 0;
  virtual ResultCode in_transaction(const std::function<ResultCode()>& body) = 0;
};

class LedgerBook {
 public:
  static ResultCode open(FreeQueuePolicy policy, LedgerEventLog& log, std::unique_ptr<LedgerBook>& out);

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

  const CreditLedger& ledger() const;

 private:
  LedgerBook(LedgerEventLog& log, CreditLedger ledger);
  ResultCode persist(CreditLedger updated);

  LedgerEventLog& log_;
  CreditLedger ledger_;
};

}  // namespace pixcake
