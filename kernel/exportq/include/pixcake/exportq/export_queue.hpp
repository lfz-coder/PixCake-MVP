#pragma once

#include "pixcake/persist/file_system.hpp"
#include "pixcake/support/fast_export_latch.hpp"
#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace pixcake {

struct ExportJob {
  std::string export_id;
  std::string original_id;
  std::uint64_t document_revision = 0;
  std::string destination_path;
  std::string settings_snapshot;
  bool watermark_free_second = false;
  bool print_requested = false;
};

struct ExportEvent {
  std::string kind;
  std::string export_id;
  std::string original_id;
  std::uint64_t document_revision = 0;
  std::string destination_path;
  std::string settings_snapshot;
  bool watermark_free_second = false;
  bool print_requested = false;
  std::uint64_t checksum = 0;
};

struct ExportProductFact {
  std::string export_id;
  std::string original_id;
  bool watermark_free_second = false;
  bool product_exists = false;
};

struct ExportCache {
  bool fast_export = false;
  std::vector<std::pair<std::string, std::string>> statuses;
};

class ExportQueue;

class ExportEventSink {
 public:
  virtual ~ExportEventSink() = default;
  virtual ResultCode persist(const ExportEvent& event, const ExportCache& cache) = 0;
};

// 极速导出不因为队列空闲而自己关闭
class ExportQueue : public FastExportLatch {
 public:
  static ExportQueue open(std::string cache_directory, std::vector<ExportEvent> events = {});

  ExportQueue(const ExportQueue&) = delete;
  ExportQueue& operator=(const ExportQueue&) = delete;
  ExportQueue(ExportQueue&&) noexcept;
  ExportQueue& operator=(ExportQueue&&) noexcept;

  void set_sink(ExportEventSink* sink);

  bool enabled() const override;
  ResultCode set_fast_export(bool on);
  ResultCode enqueue(const ExportJob& job);
  ResultCode prepare(const std::string& export_id);
  ResultCode start_rendering(const std::string& export_id);
  ResultCode commit_product(const std::string& export_id, const std::vector<std::uint8_t>& bytes,
                            FileSystem& files);
  ResultCode fail(const std::string& export_id);
  ResultCode cancel(const std::string& export_id);
  ResultCode retry(const std::string& export_id);
  ResultCode request_print(const std::string& export_id, bool printer_available, bool& unavailable);
  ResultCode recover(FileSystem& files);

  std::string status(const std::string& export_id) const;
  bool find_job(const std::string& export_id, ExportJob& out) const;
  std::vector<ExportProductFact> products() const;
  ExportCache cache() const;
  const std::vector<ExportEvent>& events() const;

 private:
  ExportQueue(std::string cache_directory, std::vector<ExportEvent> events);

  ResultCode note(ExportEvent event);
  ResultCode recover_one(const std::string& export_id, FileSystem& files);
  std::string temp_path(const std::string& export_id) const;

  std::string cache_directory_;
  std::vector<ExportEvent> events_;
  ExportEventSink* sink_ = nullptr;
};

}  // namespace pixcake
