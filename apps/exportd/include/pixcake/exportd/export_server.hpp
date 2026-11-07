#pragma once

#include "pixcake/exportd/export_service.hpp"
#include "pixcake/exportq/export_book.hpp"
#include "pixcake/ipc/process_messages.hpp"
#include "pixcake/persist/file_system.hpp"
#include "pixcake/preview_channel/channel.hpp"
#include "pixcake/support/log_sink.hpp"

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <utility>

namespace pixcake::exportd {

// Builds the work of one queued export: the document snapshot compiled to a
// render description, the settings snapshot mapped to encode settings.
// UnsupportedFormat with a reason for settings the export cannot honour yet
// (file size limit, printing).
// `watermark_directory` holds the materials a watermark names (<data>/exports/watermarks).
ResultCode work_from_job(const ExportJob& job, const std::string& source_path, ExportWork& out,
                         std::string& diagnostic, const std::string& watermark_directory = std::string());

// exportd's side of the shell connection (architecture 10: the main process
// enqueues through the command entry and hands the task over by IPC; exportd
// appends its own events to the shared export log and reports progress).
// One task at a time, in arrival order; each reads the export log afresh, so a
// task the shell enqueued after this process started is found.
class ExportServer {
 public:
  // `log` is the shared export event log (<data>/exports/export.db), `cache`
  // the queue's temporary-file directory.
  ExportServer(ExportService& service, ExportEventLog& log, std::string cache_directory, FileSystem& files,
               LogSink* process_log = nullptr);

  // Serves until the peer closes (Ok) or the channel breaks.
  ResultCode serve(preview_channel::Channel& channel, std::string& diagnostic);
  // One round: reads every message waiting (blocking up to timeout_ms for the
  // first when nothing is queued), then runs at most one task.  Cancelled when
  // nothing arrived and nothing was queued.  For tests.
  ResultCode step(preview_channel::Channel& channel, int timeout_ms, std::string& diagnostic);
  std::size_t waiting() const { return waiting_.size(); }
  // Where watermark materials are stored (<data>/exports/watermarks).
  void set_watermark_directory(std::string directory) { watermark_directory_ = std::move(directory); }

 private:
  ResultCode read(preview_channel::Channel& channel, int timeout_ms, std::string& diagnostic);
  ResultCode run(preview_channel::Channel& channel, const ipc::ExportStartMessage& start, std::string& diagnostic);
  ResultCode report(preview_channel::Channel& channel, const std::string& export_id, const std::string& batch_id,
                    const std::string& status, ResultCode code, const std::string& reason, std::string& diagnostic);
  void note(const char* event, ResultCode code, const std::string& message);
  std::string watermark_directory_;

  ExportService& service_;
  ExportEventLog& log_;
  std::string cache_directory_;
  FileSystem& files_;
  LogSink* process_log_;
  std::deque<ipc::ExportStartMessage> waiting_;
  // Per batch: tasks finished (success or failure) and tasks received.
  std::map<std::string, std::pair<std::uint32_t, std::uint32_t>> batches_;
};

}  // namespace pixcake::exportd
