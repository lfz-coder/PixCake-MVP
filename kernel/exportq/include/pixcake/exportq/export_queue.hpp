#pragma once

#include "pixcake/persist/file_system.hpp"
#include "pixcake/support/fast_export_latch.hpp"
#include "pixcake/support/log_sink.hpp"
#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace pixcake {

struct ExportJob {
  std::string export_id;
  std::string original_id;
  std::string batch_id;
  std::uint64_t document_revision = 0;
  std::vector<std::uint8_t> document_snapshot;
  std::string destination_path;
  std::string destination_root;
  FileIdentity destination_identity;
  std::string settings_snapshot;
  bool watermark_free_second = false;
  bool print_requested = false;
  ResultCode failure_code = ResultCode::Ok;
  std::string phase;
  std::uint64_t duration_ms = 0;
  bool deduplicated = false;
};

struct ExportEvent {
  std::string kind;
  std::string export_id;
  std::string original_id;
  std::string batch_id;
  std::uint64_t document_revision = 0;
  std::vector<std::uint8_t> document_snapshot;
  std::string destination_path;
  std::string destination_root;
  FileIdentity destination_identity;
  std::string settings_snapshot;
  bool watermark_free_second = false;
  bool print_requested = false;
  ResultCode failure_code = ResultCode::Ok;
  std::string phase;
  std::uint64_t duration_ms = 0;
  bool deduplicated = false;
  std::uint64_t checksum = 0;
};

struct ExportProductFact {
  std::string export_id;
  std::string original_id;
  bool watermark_free_second = false;
  bool product_exists = false;
  std::string batch_id;
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
  // 日志可以为空。不为空时：commit_product 碰文件前写 export.commit.start，换名并校验后写
  // export.commit.finish，失败写 export.commit.failed；任务进入成功、失败、取消时写 export.terminal。
  void set_log(LogSink* log);

  bool enabled() const override;
  ResultCode set_fast_export(bool on);
  ResultCode enqueue(const ExportJob& job);
  ResultCode prepare(const std::string& export_id);
  ResultCode start_rendering(const std::string& export_id);
  ResultCode commit_product(const std::string& export_id, const std::vector<std::uint8_t>& bytes,
                            FileSystem& files);
  ResultCode fail(const std::string& export_id);
  // 同上，并记下失败的结果码和给用户看的原因（需求第 19 节：导出列表显示失败原因）。
  // 原因存在 fail 事件的 phase 里，重启后仍在。
  ResultCode fail(const std::string& export_id, ResultCode code, const std::string& reason);
  ResultCode cancel(const std::string& export_id);
  ResultCode retry(const std::string& export_id);
  ResultCode request_print(const std::string& export_id, bool printer_available, bool& unavailable);
  ResultCode recover(FileSystem& files);

  std::string status(const std::string& export_id) const;
  ResultCode failure_reason(const std::string& export_id) const;
  // 最近一次失败记下的原因；没有失败或没记原因时为空。
  std::string failure_text(const std::string& export_id) const;
  bool find_job(const std::string& export_id, ExportJob& out) const;
  std::vector<ExportProductFact> products() const;
  ExportCache cache() const;
  const std::vector<ExportEvent>& events() const;

 private:
  ExportQueue(std::string cache_directory, std::vector<ExportEvent> events);

  ResultCode note(ExportEvent event);
  ResultCode commit_product_steps(const std::string& export_id, const std::vector<std::uint8_t>& bytes,
                                  FileSystem& files);
  void log_terminal(const ExportEvent& event) const;
  ResultCode recover_one(const std::string& export_id, FileSystem& files);
  // The temporary file of a task.  It must be on the destination's device so
  // publishing is a link and the published file is the temporary file (same
  // identity): in the cache directory when that is on the destination's
  // device, otherwise a hidden file next to the destination (an external
  // drive).  Recovery looks in both places.
  std::string temp_path(const std::string& export_id) const;
  std::string temp_path_for(const ExportJob& job, FileSystem& files) const;

  std::string cache_directory_;
  std::vector<ExportEvent> events_;
  ExportEventSink* sink_ = nullptr;
  LogSink* log_ = nullptr;
};

}  // namespace pixcake
