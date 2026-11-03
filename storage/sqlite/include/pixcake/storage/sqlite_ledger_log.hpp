#pragma once

#include "pixcake/ledger/ledger_book.hpp"

#include <memory>
#include <string>

namespace pixcake {

class SqliteLedgerLog : public LedgerEventLog {
 public:
  static ResultCode open(const std::string& path, std::unique_ptr<SqliteLedgerLog>& out);
  ~SqliteLedgerLog() override;

  ResultCode load(std::vector<LedgerEvent>& out) override;
  ResultCode append(const std::vector<LedgerEvent>& events) override;
  ResultCode read_cache(std::optional<LedgerCache>& out) override;
  ResultCode write_cache(const LedgerCache& cache) override;
  ResultCode in_transaction(const std::function<ResultCode()>& body) override;

 private:
  struct Impl;
  explicit SqliteLedgerLog(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace pixcake
