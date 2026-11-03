#pragma once

#include "pixcake/exportq/export_book.hpp"

#include <memory>
#include <string>

namespace pixcake {

class SqliteExportLog : public ExportEventLog {
 public:
  static ResultCode open(const std::string& path, std::unique_ptr<SqliteExportLog>& out);
  ~SqliteExportLog() override;

  ResultCode load(std::vector<ExportEvent>& out) override;
  ResultCode append(const std::vector<ExportEvent>& events) override;
  ResultCode read_cache(std::optional<ExportCache>& out) override;
  ResultCode write_cache(const ExportCache& cache) override;
  ResultCode in_transaction(const std::function<ResultCode()>& body) override;

 private:
  struct Impl;
  explicit SqliteExportLog(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace pixcake
