#pragma once

#include "pixcake/exportq/export_queue.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace pixcake {

class ExportEventLog {
 public:
  virtual ~ExportEventLog() = default;
  virtual ResultCode load(std::vector<ExportEvent>& out) = 0;
  virtual ResultCode append(const std::vector<ExportEvent>& events) = 0;
  virtual ResultCode read_cache(std::optional<ExportCache>& out) = 0;
  virtual ResultCode write_cache(const ExportCache& cache) = 0;
  virtual ResultCode in_transaction(const std::function<ResultCode()>& body) = 0;
};

class ExportBook {
 public:
  static ResultCode open(std::string cache_directory, ExportEventLog& log, FileSystem& files,
                         std::unique_ptr<ExportBook>& out);
  ExportQueue& queue();

 private:
  struct Sink : ExportEventSink {
    explicit Sink(ExportEventLog& log) : log(log) {}
    ResultCode persist(const ExportEvent& event, const ExportCache& cache) override;
    ExportEventLog& log;
  };

  ExportBook(ExportEventLog& log, ExportQueue queue);
  ExportEventLog& log_;
  Sink sink_;
  ExportQueue queue_;
};

}  // namespace pixcake
