#include "pixcake/exportq/export_book.hpp"

#include <utility>

namespace pixcake {

ResultCode ExportBook::Sink::persist(const ExportEvent& event, const ExportCache& cache) {
  return log.in_transaction([&] {
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
                            std::unique_ptr<ExportBook>& out) {
  out.reset();
  std::vector<ExportEvent> events;
  const ResultCode loaded = log.load(events);
  if (loaded != ResultCode::Ok) {
    return loaded;
  }
  ExportQueue queue = ExportQueue::open(std::move(cache_directory), std::move(events));
  std::optional<ExportCache> cache;
  const ResultCode read = log.read_cache(cache);
  if (read != ResultCode::Ok) {
    return read;
  }
  const ExportCache folded = queue.cache();
  if (!cache || cache->fast_export != folded.fast_export || cache->statuses != folded.statuses) {
    const ResultCode repaired = log.in_transaction([&] { return log.write_cache(folded); });
    if (repaired != ResultCode::Ok) {
      return repaired;
    }
  }
  auto book = std::unique_ptr<ExportBook>(new ExportBook(log, std::move(queue)));
  const ResultCode recovered = book->queue_.recover(files);
  if (recovered != ResultCode::Ok) {
    return recovered;
  }
  out = std::move(book);
  return ResultCode::Ok;
}

}  // namespace pixcake
