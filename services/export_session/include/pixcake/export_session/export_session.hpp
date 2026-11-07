#pragma once

#include "pixcake/exportq/export_book.hpp"
#include "pixcake/exportq/export_settings.hpp"
#include "pixcake/persist/file_system.hpp"
#include "pixcake/preview_channel/export_client.hpp"
#include "pixcake/project_session/project_session.hpp"
#include "pixcake/storage/sqlite_export_log.hpp"
#include "pixcake/support/session_state.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace pixcake::export_session {

using project_session::ProjectSession;

struct ExportOptions {
  std::string exportd_executable;
  std::string log_directory;
  // The app data directory; the export log is <data>/exports/export.db.
  std::string data_directory;
  int connect_timeout_ms = 10'000;
};

// What the export dialog asks for (requirement 19; the test build's dialog,
// docs/superpowers/evidence/2026-11-06-export-dialog-observation.md).
struct ExportRequest {
  std::vector<std::int64_t> photo_ids;
  // 导出至: 指定文件夹 (folder) or 原始照片所在的文件夹 (original_folder).
  bool original_folder = false;
  std::string folder;
  // 导出位置 sub-folder (the project name), empty for none.
  std::string subfolder;
  ExportSettings settings;
};

struct ExportRow {
  std::string export_id;
  std::int64_t photo_id = 0;
  std::string file_name;         // the product's file name
  std::string destination_path;
  std::string status;            // the queue's status: 排队、渲染、成功、失败、取消 ...
  std::string reason;            // failure reason for the user; empty otherwise
  bool succeeded = false;
  bool can_retry = false;
  bool can_cancel = false;
};

// What the dialog remembers: 设为默认导出路径 keeps the folder, 设为默认格式
// the format.  Stored in <data>/exports/defaults (one key=value per line).
struct ExportDefaults {
  std::string folder;
  std::optional<ExportFormat> format;
};

// File extension for a format; Original takes the original's (JPEG, PNG, TIFF
// kept, everything else JPG -- exportd writes what the extension says).
std::string extension_for(ExportFormat format, const std::string& original_format);
// A user-facing reason for a failure code without a recorded reason.
std::string failure_label(ResultCode code);

// The shell's side of exporting (architecture 10): jobs enter the export queue
// through BusinessCommands (signed-in only), exportd draws and commits them and
// appends its own events to the same log; this class reads the log back.
class ExportSession {
 public:
  static ResultCode open(ProjectSession& projects, const SessionState& session, ExportOptions options,
                         std::unique_ptr<ExportSession>& out, std::string& diagnostic);

  // Enqueues one task per photo and hands each to exportd (started on first
  // use).  Photos that cannot be exported (no readable original) are skipped
  // with "file name: reason" in `notices`.
  ResultCode submit(const ExportRequest& request, std::vector<std::string>& notices, std::string& diagnostic);
  ResultCode retry(const std::string& export_id, std::string& diagnostic);
  ResultCode cancel(const std::string& export_id, std::string& diagnostic);
  // Every task in the log, newest first.
  std::vector<ExportRow> rows();
  ExportDefaults defaults() const;
  ResultCode remember_defaults(const ExportDefaults& defaults);
  // Hands queued tasks left by an earlier session to exportd.
  ResultCode resume(std::string& diagnostic);

  // exportd's connection (-1 before it is started or after it exited).
  int fd() const { return client_ ? client_->fd() : -1; }
  // Reads every waiting progress report; true when any arrived.  When exportd
  // has gone, drops the connection (the next submit starts it again).
  bool read_progress();

 private:
  ExportSession(ProjectSession& projects, const SessionState& session, ExportOptions options,
                std::unique_ptr<SqliteExportLog> log);
  ResultCode book(std::unique_ptr<ExportBook>& out);
  ResultCode ensure_exportd(std::string& diagnostic);
  ResultCode hand_over(const ExportJob& job, std::string& diagnostic);
  std::string next_export_id();

  ProjectSession& projects_;
  const SessionState& session_;
  ExportOptions options_;
  std::unique_ptr<SqliteExportLog> log_;
  PosixFileSystem files_;
  std::unique_ptr<preview_channel::ExportClient> client_;
  unsigned sequence_ = 0;
};

}  // namespace pixcake::export_session
