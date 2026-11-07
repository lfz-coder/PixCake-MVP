#include "pixcake/exportq/export_book.hpp"

#include <map>
#include <utility>

namespace pixcake {

namespace {

std::string current_status(const std::vector<ExportEvent>& events, const std::string& export_id) {
  std::string status;
  for (const auto& event : events) {
    if (event.export_id != export_id) continue;
    if (event.kind == "enqueue" || event.kind == "requeue") status = "排队";
    else if (event.kind == "prepare") status = "准备";
    else if (event.kind == "render") status = "渲染";
    else if (event.kind == "temp-begin" || event.kind == "temp-end") status = "写临时文件";
    else if (event.kind == "commit-begin") status = "提交到目标路径";
    else if (event.kind == "commit-end") status = "成功";
    else if (event.kind == "fail") status = "失败";
    else if (event.kind == "cleanup-pending") status = "待清理临时文件";
    else if (event.kind == "cancel") status = "取消";
  }
  return status;
}

bool event_allowed(const std::vector<ExportEvent>& events, const ExportEvent& event) {
  if (event.kind == "fast-on" || event.kind == "fast-off") return true;
  const std::string status = current_status(events, event.export_id);
  if (event.kind == "enqueue") return status.empty();
  if (event.kind == "prepare") return status == "排队";
  if (event.kind == "render") return status == "准备";
  if (event.kind == "temp-begin") return status == "渲染";
  if (event.kind == "temp-end" || event.kind == "commit-begin") return status == "写临时文件";
  if (event.kind == "commit-end") return status == "提交到目标路径";
  if (event.kind == "fail") {
    return status == "排队" || status == "准备" || status == "渲染" || status == "写临时文件" ||
           status == "提交到目标路径" || status == "待清理临时文件";
  }
  if (event.kind == "cleanup-pending") {
    return status == "写临时文件";
  }
  if (event.kind == "cancel") return status == "排队" || status == "准备" || status == "渲染";
  if (event.kind == "requeue") return status == "失败";
  return false;
}

}  // namespace

ResultCode ExportBook::Sink::persist(const ExportEvent& event, const ExportCache& cache) {
  return log.in_transaction([&] {
    // The queue object can outlive another process or another open handle.
    // Re-read the event stream while holding the log transaction so a stale
    // handle cannot append after cancellation, failure, or completion.
    std::vector<ExportEvent> current;
    const ResultCode loaded = log.load(current);
    if (loaded != ResultCode::Ok) return loaded;
    if (!event_allowed(current, event)) return ResultCode::StateConflict;
    const ResultCode appended = log.append({event});
    if (appended != ResultCode::Ok) {
      return appended;
    }
    return log.write_cache(cache);
  });
}

ExportBook::ExportBook(ExportEventLog& log, ExportQueue queue)
    : log_(log), sink_(log_), queue_(std::move(queue)) {
  queue_.set_sink(&sink_);
}

ExportQueue& ExportBook::queue() { return queue_; }

ResultCode ExportBook::open(std::string cache_directory, ExportEventLog& log, FileSystem& files,
                            std::unique_ptr<ExportBook>& out, Recovery recovery) {
  out.reset();
  std::vector<ExportEvent> events;
  std::optional<ExportCache> cache;
  const ResultCode opened = log.in_transaction([&] {
    const ResultCode loaded = log.load(events);
    if (loaded != ResultCode::Ok) return loaded;
    const ResultCode read = log.read_cache(cache);
    if (read != ResultCode::Ok) return read;
    const ExportQueue folded_queue = ExportQueue::open(cache_directory, events);
    const ExportCache folded = folded_queue.cache();
    if (!cache || cache->fast_export != folded.fast_export || cache->statuses != folded.statuses) {
      return log.write_cache(folded);
    }
    return ResultCode::Ok;
  });
  if (opened != ResultCode::Ok) return opened;
  ExportQueue queue = ExportQueue::open(std::move(cache_directory), std::move(events));
  auto book = std::unique_ptr<ExportBook>(new ExportBook(log, std::move(queue)));
  if (recovery == Recovery::Skip) {
    out = std::move(book);
    return ResultCode::Ok;
  }
  const ResultCode recovered = book->queue_.recover(files);
  // Recovery is task-local.  A missing directory, permission error or
  // collision leaves that task retryable, but must not hide unrelated queue
  // entries from the caller on reopen.
  (void)recovered;
  out = std::move(book);
  return ResultCode::Ok;
}

}  // namespace pixcake
