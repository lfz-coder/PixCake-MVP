#include "pixcake/exportd/export_server.hpp"

#include "pixcake/editdoc/edit_document.hpp"
#include "pixcake/exportq/export_settings.hpp"
#include "pixcake/render/render_description.hpp"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace pixcake::exportd {
namespace {

// Quality when the settings carry neither quality nor a size limit: 12, what
// the test build's dialog showed (docs/superpowers/evidence/
// 2026-11-06-export-dialog-observation.md).
constexpr std::uint8_t kDefaultQuality = 12;
// The product must fit the encoder's buffer; far above any real photo.
constexpr std::uint64_t kMaxOutputBytes = 4ull << 30;

ExportFormat format_of(pixcake::ExportFormat format) {
  switch (format) {
    case pixcake::ExportFormat::Jpg: return ExportFormat::Jpeg;
    case pixcake::ExportFormat::Tiff: return ExportFormat::Tiff;
    case pixcake::ExportFormat::Tiff16: return ExportFormat::Tiff16;
    case pixcake::ExportFormat::Original: break;
  }
  return ExportFormat::Original;
}

}  // namespace

ResultCode work_from_job(const ExportJob& job, const std::string& source_path, ExportWork& out,
                         std::string& diagnostic, const std::string& watermark_directory) {
  out = {};
  const auto document = EditDocument::decode(job.document_snapshot);
  if (!document) {
    diagnostic = "导出任务的参数快照无法读取";
    return ResultCode::StateConflict;
  }
  auto compiled = render::compile(*document);
  if (compiled.code != ResultCode::Ok) {
    diagnostic = "导出任务的参数无法编译成绘制描述";
    return compiled.code;
  }
  ExportSettings settings;
  if (parse_export_settings(job.settings_snapshot, settings) != ResultCode::Ok) {
    diagnostic = "导出设置无法读取";
    return ResultCode::StateConflict;
  }
  if (settings.print_copies > 0) {
    diagnostic = "排版印刷还没有接入";
    return ResultCode::UnsupportedFormat;
  }
  ExportWork work;
  work.job = job;
  work.description = std::move(compiled.description);
  // The snapshot's image id is the job's original id; the revision the job's.
  work.description.image_id = job.original_id;
  work.description.document_revision = job.document_revision;
  work.source_path = source_path;
  work.encode.format = format_of(settings.format);
  if (work.encode.format == ExportFormat::Original) {
    // "Original format": the shell named the product after the original's
    // container; write what the name says so the two never disagree.
    const auto dot = job.destination_path.rfind('.');
    std::string extension = dot == std::string::npos ? std::string() : job.destination_path.substr(dot + 1);
    for (char& c : extension) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    if (extension == "png") work.encode.format = ExportFormat::Png;
    else if (extension == "tif" || extension == "tiff") work.encode.format = ExportFormat::Tiff;
    else if (extension == "jpg" || extension == "jpeg") work.encode.format = ExportFormat::Jpeg;
  }
  work.encode.quality = static_cast<std::uint8_t>(settings.quality.value_or(kDefaultQuality));
  // 文件大小限制 replaces 品质 (the settings carry one or the other).
  if (settings.size_limit_kb) {
    work.encode.size_limit_bytes = std::min<std::uint64_t>(*settings.size_limit_kb, kMaxOutputBytes / 1024) * 1024;
  }
  work.encode.color_space = settings.convert_to_srgb ? "sRGB" : "";
  work.encode.limits.max_output_bytes = kMaxOutputBytes;
  if (settings.watermark) {
    if (watermark_directory.empty()) {
      diagnostic = "水印素材目录没有配置";
      return ResultCode::StateConflict;
    }
    // The material name is validated by the settings parser (no path separators).
    work.watermark_path = watermark_directory + "/" + settings.watermark->material;
    work.watermark = std::move(settings.watermark);
  }
  work.layout = settings.layout;
  out = std::move(work);
  diagnostic.clear();
  return ResultCode::Ok;
}

ExportServer::ExportServer(ExportService& service, ExportEventLog& log, std::string cache_directory,
                           FileSystem& files, LogSink* process_log)
    : service_(service),
      log_(log),
      cache_directory_(std::move(cache_directory)),
      files_(files),
      process_log_(process_log) {}

void ExportServer::note(const char* event, ResultCode code, const std::string& message) {
  if (process_log_ == nullptr) return;
  LogRecord record;
  record.module = "exportd";
  record.event = event;
  record.code = code;
  record.message = message;
  process_log_->write(record);
}

ResultCode ExportServer::report(preview_channel::Channel& channel, const std::string& export_id,
                                const std::string& batch_id, const std::string& status, ResultCode code,
                                const std::string& reason, std::string& diagnostic) {
  ipc::ExportProgressMessage progress;
  progress.export_id = export_id;
  progress.status = status.empty() ? "失败" : status;
  const auto counts = batches_[batch_id];
  progress.completed = std::min(counts.first, counts.second);
  progress.total = counts.second;
  progress.code = code;
  progress.reason = reason;
  std::vector<std::uint8_t> bytes;
  if (ipc::encode_export_progress(progress, bytes) != ResultCode::Ok) {
    // An over-long reason must not lose the report.
    progress.reason = progress.reason.substr(0, 1024);
    if (ipc::encode_export_progress(progress, bytes) != ResultCode::Ok) return ResultCode::StateConflict;
  }
  return channel.send(bytes, -1, diagnostic);
}

ResultCode ExportServer::read(preview_channel::Channel& channel, int timeout_ms, std::string& diagnostic) {
  bool any = false;
  for (;;) {
    std::vector<std::uint8_t> bytes;
    preview_channel::UniqueFd ignored;
    const ResultCode received =
        channel.receive(bytes, ignored, ipc::kMaxProcessMessageBytes, any ? 0 : timeout_ms, diagnostic);
    if (received == ResultCode::Cancelled) return any ? ResultCode::Ok : ResultCode::Cancelled;
    if (received != ResultCode::Ok) return received;
    any = true;
    if (ipc::is_export_start(bytes)) {
      ipc::ExportStartMessage start;
      if (ipc::decode_export_start(bytes, start) != ResultCode::Ok) {
        note("exportd.message", ResultCode::StateConflict, "malformed export start");
        continue;
      }
      std::unique_ptr<ExportBook> book;
      ExportJob job;
      if (ExportBook::open(cache_directory_, log_, files_, book) == ResultCode::Ok &&
          book->queue().find_job(start.export_id, job)) {
        ++batches_[job.batch_id].second;
        waiting_.push_back(std::move(start));
      } else {
        const ResultCode sent = report(channel, start.export_id, {}, "失败", ResultCode::StateConflict,
                                       "导出队列里没有这个任务", diagnostic);
        if (sent != ResultCode::Ok) return sent;
      }
    } else if (ipc::is_task_cancel(bytes)) {
      ipc::TaskCancelMessage cancel;
      if (ipc::decode_task_cancel(bytes, cancel) != ResultCode::Ok || cancel.kind != ipc::TaskKind::Export) continue;
      // Only a task that has not started can be cancelled; a running one
      // finishes (the queue refuses cancelling past rendering anyway).
      const auto found = std::find_if(waiting_.begin(), waiting_.end(),
                                      [&](const ipc::ExportStartMessage& m) { return m.export_id == cancel.task_id; });
      if (found == waiting_.end()) continue;
      waiting_.erase(found);
      std::unique_ptr<ExportBook> book;
      ExportJob job;
      if (ExportBook::open(cache_directory_, log_, files_, book) != ResultCode::Ok ||
          !book->queue().find_job(cancel.task_id, job)) {
        continue;
      }
      const ResultCode cancelled = book->queue().cancel(cancel.task_id);
      auto& counts = batches_[job.batch_id];
      if (counts.second > 0) --counts.second;
      const ResultCode sent = report(channel, cancel.task_id, job.batch_id, book->queue().status(cancel.task_id),
                                     cancelled, {}, diagnostic);
      if (sent != ResultCode::Ok) return sent;
    } else {
      note("exportd.message", ResultCode::StateConflict, "unexpected message kind");
    }
  }
}

ResultCode ExportServer::run(preview_channel::Channel& channel, const ipc::ExportStartMessage& start,
                             std::string& diagnostic) {
  std::unique_ptr<ExportBook> book;
  ResultCode code = ExportBook::open(cache_directory_, log_, files_, book);
  ExportJob job;
  if (code != ResultCode::Ok || !book->queue().find_job(start.export_id, job)) {
    return report(channel, start.export_id, {}, "失败", code == ResultCode::Ok ? ResultCode::StateConflict : code,
                  "导出队列无法读取", diagnostic);
  }
  ExportQueue& queue = book->queue();
  auto& counts = batches_[job.batch_id];
  const auto finish = [&](ResultCode result, const std::string& reason) {
    ++counts.first;
    note("exportd.export", result, start.export_id + (reason.empty() ? "" : ": " + reason));
    return report(channel, start.export_id, job.batch_id, queue.status(start.export_id), result, reason, diagnostic);
  };

  ExportWork work;
  std::string reason;
  code = work_from_job(job, start.source_path, work, reason, watermark_directory_);
  if (code == ResultCode::Ok) code = service_.submit(std::move(work), reason);
  if (code != ResultCode::Ok) {
    queue.fail(start.export_id, code, reason);  // from 排队: nothing was drawn
    return finish(code, reason);
  }
  const ResultCode rendering = report(channel, start.export_id, job.batch_id, "渲染", ResultCode::Ok, {}, diagnostic);
  if (rendering != ResultCode::Ok) return rendering;
  const ExportResult result = service_.process_next(queue, files_);
  service_.clear_pending();
  return finish(result.code, result.code == ResultCode::Ok ? std::string() : result.diagnostic);
}

ResultCode ExportServer::step(preview_channel::Channel& channel, int timeout_ms, std::string& diagnostic) {
  const ResultCode read_code = read(channel, waiting_.empty() ? timeout_ms : 0, diagnostic);
  if (read_code != ResultCode::Ok && read_code != ResultCode::Cancelled) return read_code;
  if (waiting_.empty()) return read_code;
  const ipc::ExportStartMessage start = std::move(waiting_.front());
  waiting_.pop_front();
  return run(channel, start, diagnostic);
}

ResultCode ExportServer::serve(preview_channel::Channel& channel, std::string& diagnostic) {
  for (;;) {
    const ResultCode code = step(channel, -1, diagnostic);
    if (code == ResultCode::Ok || code == ResultCode::Cancelled) continue;
    if (code == ResultCode::IoFailed) {
      // The shell closed the connection: tasks not started stay queued in the
      // export log and are handed over again by the next session.
      note("exportd.session", ResultCode::Ok, diagnostic);
      diagnostic.clear();
      return ResultCode::Ok;
    }
    note("exportd.session", code, diagnostic);
    return code;
  }
}

}  // namespace pixcake::exportd
