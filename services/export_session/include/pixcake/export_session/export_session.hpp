#pragma once

#include "pixcake/export_session/export_charge.hpp"
#include "pixcake/exportq/export_book.hpp"
#include "pixcake/exportq/export_settings.hpp"
#include "pixcake/persist/file_system.hpp"
#include "pixcake/preview_channel/export_client.hpp"
#include "pixcake/project_session/project_session.hpp"
#include "pixcake/ledger/ledger_book.hpp"
#include "pixcake/storage/sqlite_export_log.hpp"
#include "pixcake/storage/sqlite_ledger_log.hpp"
#include "pixcake/support/session_state.hpp"

#include <cstdint>
#include <map>
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
  // 账号的剩余张数：本机账本（<数据目录>/ledger.db）第一次建立时记入。之后的扣减只记在本机，
  // 没有服务器，跨设备不去重。
  std::int64_t opening_sheets = 0;
};

// 导出弹窗的三类计数和这次要扣的张数（预估：同一张图以前扣过的不再算）。
struct ExportEstimate {
  int refined = 0;
  int free = 0;
  int original = 0;
  std::int64_t sheets = 0;
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
  // 同时导出无水印图片：有水印时，每张出两份，分放“文件夹名_有水印”“文件夹名_无水印”。
  bool watermark_free_copy = false;
};

// 导出明细的计费一栏（test 版“导出明细”的实际计费）：成功的导出记账后才有结论。
enum class ExportCharge {
  None,       // 还没成功
  Pending,    // 成功了，还没记账
  Charged,    // 扣了 1 张
  Repeat,     // 精修效果图，这张图以前扣过
  Free,       // 免费效果图
  Original,   // 原图
  FreeCopy,   // 精修效果图的无水印那份（同时导出无水印图片，不另计）
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
  ExportCharge charge = ExportCharge::None;
};

// What the dialog remembers: 设为默认导出路径 keeps the folder, 设为默认格式
// the format.  Stored in <data>/exports/defaults (one key=value per line).
struct ExportDefaults {
  std::string folder;
  std::optional<ExportFormat> format;
  // 水印：开关与素材、三套摆放都记住（test 版关掉弹窗再开仍是上次的状态）。
  bool watermark_on = false;
  std::optional<ExportWatermark> watermark;
  bool watermark_free_copy = false;  // 同时导出无水印图片的勾选（实测会记住）
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
  // 水印素材：JPG/PNG、不超过 10 MB（test 版上传框的限制），复制进
  // <数据目录>/exports/watermarks，文件名按内容取（同一张图只存一份）；`material`
  // 是写进设置快照的名字。原文件之后移走、删掉都不影响导出。
  ResultCode store_watermark(const std::string& source_path, std::string& material, std::string& diagnostic);
  std::string watermark_path(const std::string& material) const;
  static constexpr std::uint64_t kMaxWatermarkBytes = 10ull * 1024 * 1024;
  // Hands queued tasks left by an earlier session to exportd.
  ResultCode resume(std::string& diagnostic);

  // 扣张数（test 版规则，docs/superpowers/evidence/2026-11-07-export-credit-observation.md）：
  // 精修效果图按画面内容认图，同一张图只扣一次；原图、免费效果图不扣。提交时张数不够的照片
  // 不排队；导出成功后 settle() 记账（read_progress、resume 会顺带调用）。
  ExportEstimate estimate(const std::vector<std::int64_t>& photo_ids);
  std::int64_t remaining_sheets() const;
  ResultCode settle(std::string& diagnostic);

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
  // 照片计费用的图片标识：和已扣过的某张是同一画面就用那张的标识，否则用自己的指纹；
  // 原图读不了时退回参数文件编号。
  std::string picture_identity(std::int64_t photo_id, const std::vector<std::string>& also);

  ProjectSession& projects_;
  const SessionState& session_;
  ExportOptions options_;
  std::unique_ptr<SqliteExportLog> log_;
  PosixFileSystem files_;
  std::unique_ptr<preview_channel::ExportClient> client_;
  std::unique_ptr<SqliteLedgerLog> ledger_log_;
  std::unique_ptr<LedgerBook> ledger_;
  std::map<std::string, ExportEffect> effects_;  // export id -> effect, for the export list
  unsigned sequence_ = 0;
};

}  // namespace pixcake::export_session
