#include "pixcake/export_session/export_session.hpp"

#include "pixcake/command/business_commands.hpp"
#include "pixcake/editdoc/edit_document.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <utility>

namespace pixcake::export_session {
namespace {

std::string lower(std::string text) {
  for (char& c : text) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
  return text;
}

std::string directory_of(const std::string& path) {
  const auto slash = path.rfind('/');
  return slash == std::string::npos || slash == 0 ? "/" : path.substr(0, slash);
}

std::string stem_of(const std::string& file_name) {
  const auto dot = file_name.rfind('.');
  return dot == std::string::npos || dot == 0 ? file_name : file_name.substr(0, dot);
}

std::string name_of(const std::string& path) {
  const auto slash = path.rfind('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool make_directory(const std::string& path) {
  struct stat info {};
  if (::stat(path.c_str(), &info) == 0) return S_ISDIR(info.st_mode);
  return ::mkdir(path.c_str(), 0755) == 0;
}

std::int64_t photo_of(const std::string& original_id) {
  const std::string prefix = "photo-";
  if (original_id.rfind(prefix, 0) != 0) return 0;
  try {
    return std::stoll(original_id.substr(prefix.size()));
  } catch (...) {
    return 0;
  }
}

}  // namespace

std::string extension_for(ExportFormat format, const std::string& original_format) {
  switch (format) {
    case ExportFormat::Jpg: return "jpg";
    case ExportFormat::Tiff:
    case ExportFormat::Tiff16: return "tif";
    case ExportFormat::Original: break;
  }
  const std::string kind = lower(original_format);
  if (kind == "png") return "png";
  if (kind == "tiff" || kind == "tif") return "tif";
  return "jpg";
}

std::string failure_label(ResultCode code) {
  switch (code) {
    case ResultCode::SourceMissing: return "原图不存在或无法访问";
    case ResultCode::ImageCorrupt: return "原图已损坏，无法读取";
    case ResultCode::UnsupportedFormat: return "暂不支持这个格式或设置";
    case ResultCode::PermissionDenied: return "没有写入目标文件夹的权限";
    case ResultCode::DiskFull: return "磁盘空间不足";
    case ResultCode::IoFailed: return "读写文件失败";
    case ResultCode::Cancelled: return "已取消";
    default: break;
  }
  return "导出失败";
}

ExportSession::ExportSession(ProjectSession& projects, const SessionState& session, ExportOptions options,
                             std::unique_ptr<SqliteExportLog> log)
    : projects_(projects), session_(session), options_(std::move(options)), log_(std::move(log)) {}

ResultCode ExportSession::open(ProjectSession& projects, const SessionState& session, ExportOptions options,
                               std::unique_ptr<ExportSession>& out, std::string& diagnostic) {
  out.reset();
  const std::string exports = options.data_directory + "/exports";
  if (!make_directory(options.data_directory) || !make_directory(exports)) {
    diagnostic = "export directory unavailable: " + exports;
    return ResultCode::IoFailed;
  }
  std::unique_ptr<SqliteExportLog> log;
  const ResultCode opened = SqliteExportLog::open(exports + "/export.db", log);
  if (opened != ResultCode::Ok) {
    diagnostic = "export log unavailable: " + exports + "/export.db";
    return opened;
  }
  std::unique_ptr<ExportSession> created(new ExportSession(projects, session, std::move(options), std::move(log)));
  // Architecture 11: a task interrupted by a crash fails (it can be retried);
  // a complete temporary file is published.  exportd is not running yet, so
  // this is the one place the shell recovers; every later read skips it.
  std::unique_ptr<ExportBook> book;
  ExportBook::open(created->options_.data_directory + "/exports/cache", *created->log_, created->files_, book);
  out = std::move(created);
  diagnostic.clear();
  return ResultCode::Ok;
}

ResultCode ExportSession::book(std::unique_ptr<ExportBook>& out) {
  // exportd may be mid-task: never recover here (see ExportBook::Recovery).
  return ExportBook::open(options_.data_directory + "/exports/cache", *log_, files_, out,
                          ExportBook::Recovery::Skip);
}

std::string ExportSession::next_export_id() {
  // Sorted by time in the log's id order: the export list reads them back so.
  const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
  char text[48];
  std::snprintf(text, sizeof(text), "exp-%013lld-%04u", static_cast<long long>(now), ++sequence_ % 10000);
  return text;
}

ResultCode ExportSession::ensure_exportd(std::string& diagnostic) {
  if (client_) return ResultCode::Ok;
  return preview_channel::ExportClient::start(options_.exportd_executable, options_.log_directory,
                                              options_.data_directory, options_.connect_timeout_ms, client_,
                                              diagnostic);
}

ResultCode ExportSession::hand_over(const ExportJob& job, std::string& diagnostic) {
  ipc::ExportStartMessage start;
  start.export_id = job.export_id;
  // The original is resolved now, through its bookmark: it may have moved.
  if (projects_.resolve_photo(photo_of(job.original_id), start.source_path, diagnostic) != ResultCode::Ok) {
    std::unique_ptr<ExportBook> current;
    if (book(current) == ResultCode::Ok) {
      current->queue().fail(job.export_id, ResultCode::SourceMissing, failure_label(ResultCode::SourceMissing));
    }
    return ResultCode::SourceMissing;
  }
  ResultCode code = ensure_exportd(diagnostic);
  if (code == ResultCode::Ok) code = client_->send_start(start, diagnostic);
  if (code != ResultCode::Ok) client_.reset();
  return code;
}

ResultCode ExportSession::submit(const ExportRequest& request, std::vector<std::string>& notices,
                                 std::string& diagnostic) {
  notices.clear();
  if (!session_.signed_in()) {
    diagnostic = "not signed in";
    return ResultCode::StateConflict;
  }
  if (request.photo_ids.empty() || (!request.original_folder && request.folder.empty())) {
    diagnostic = "nothing to export or no destination";
    return ResultCode::StateConflict;
  }
  std::unique_ptr<ExportBook> current;
  ResultCode code = book(current);
  if (code != ResultCode::Ok) {
    diagnostic = "export log unavailable";
    return code;
  }
  ExportQueue& queue = current->queue();
  // Destinations already taken by a task that may still write them.
  std::set<std::string> taken;
  for (const auto& [id, status] : queue.cache().statuses) {
    ExportJob job;
    if (status != "取消" && queue.find_job(id, job)) taken.insert(job.destination_path);
  }
  const std::string settings = encode_export_settings(request.settings);
  BusinessCommands commands(session_);
  std::vector<ExportJob> accepted;
  std::string batch_id;
  for (const std::int64_t photo_id : request.photo_ids) {
    std::optional<PhotoRow> photo;
    if (projects_.load_photo(photo_id, photo) != ResultCode::Ok || !photo) continue;
    std::string source;
    std::string reason;
    if (projects_.resolve_photo(photo_id, source, reason) != ResultCode::Ok) {
      notices.push_back(photo->file_name + ": " + failure_label(ResultCode::SourceMissing));
      continue;
    }
    std::vector<std::uint8_t> document;
    if (projects_.document_bytes(photo_id, document) != ResultCode::Ok || document.empty()) {
      // Never edited (or the document is unreadable): the original as it is.
      document = EditDocument::create(ProjectSession::document_id(photo_id))->encode();
    }
    const auto decoded = EditDocument::decode(document);
    if (!decoded) {
      notices.push_back(photo->file_name + ": " + "参数文件无法读取");
      continue;
    }
    std::string root = request.original_folder ? directory_of(source) : request.folder;
    while (root.size() > 1 && root.back() == '/') root.pop_back();
    std::string folder = root;
    if (!request.subfolder.empty()) {
      folder += "/" + request.subfolder;
      if (!make_directory(folder)) {
        notices.push_back(photo->file_name + ": " + failure_label(ResultCode::PermissionDenied));
        continue;
      }
    }
    // Name collisions: "name(1).jpg", "name(2).jpg" -- a project choice; what
    // the official build does on a collision was not observed.
    const std::string stem = stem_of(photo->file_name);
    const std::string extension = extension_for(request.settings.format, photo->format);
    std::string destination = folder + "/" + stem + "." + extension;
    for (int n = 1; taken.count(destination) != 0 || ::access(destination.c_str(), F_OK) == 0; ++n) {
      destination = folder + "/" + stem + "(" + std::to_string(n) + ")." + extension;
    }
    ExportJob job;
    job.export_id = next_export_id();
    if (batch_id.empty()) batch_id = "batch-" + job.export_id.substr(4);
    job.batch_id = batch_id;
    job.original_id = ProjectSession::document_id(photo_id);
    job.document_revision = decoded->revision();
    job.document_snapshot = std::move(document);
    job.destination_path = destination;
    job.destination_root = root;
    job.settings_snapshot = settings;
    code = commands.enqueue_export(queue, job);
    if (code != ResultCode::Ok) {
      notices.push_back(photo->file_name + ": " + failure_label(code));
      continue;
    }
    taken.insert(destination);
    accepted.push_back(std::move(job));
  }
  for (const auto& job : accepted) {
    code = hand_over(job, diagnostic);
    if (code != ResultCode::Ok && code != ResultCode::SourceMissing) {
      // exportd unavailable: the tasks stay queued; resume() hands them over.
      return code;
    }
  }
  diagnostic.clear();
  return accepted.empty() ? ResultCode::StateConflict : ResultCode::Ok;
}

ResultCode ExportSession::retry(const std::string& export_id, std::string& diagnostic) {
  if (!session_.signed_in()) return ResultCode::StateConflict;
  std::unique_ptr<ExportBook> current;
  ResultCode code = book(current);
  ExportJob job;
  if (code != ResultCode::Ok || !current->queue().find_job(export_id, job)) {
    diagnostic = "export task not found";
    return ResultCode::StateConflict;
  }
  code = current->queue().retry(export_id);
  if (code != ResultCode::Ok) {
    diagnostic = "export task cannot be retried";
    return code;
  }
  return hand_over(job, diagnostic);
}

ResultCode ExportSession::cancel(const std::string& export_id, std::string& diagnostic) {
  if (client_) return client_->send_cancel(export_id, diagnostic);
  std::unique_ptr<ExportBook> current;
  const ResultCode code = book(current);
  return code == ResultCode::Ok ? current->queue().cancel(export_id) : code;
}

ResultCode ExportSession::resume(std::string& diagnostic) {
  std::unique_ptr<ExportBook> current;
  ResultCode code = book(current);
  if (code != ResultCode::Ok) return code;
  for (const auto& [id, status] : current->queue().cache().statuses) {
    ExportJob job;
    if (status != "排队" || !current->queue().find_job(id, job)) continue;
    code = hand_over(job, diagnostic);
    if (code != ResultCode::Ok && code != ResultCode::SourceMissing) return code;
  }
  return ResultCode::Ok;
}

ExportDefaults ExportSession::defaults() const {
  ExportDefaults out;
  std::ifstream in(options_.data_directory + "/exports/defaults");
  std::string line;
  while (std::getline(in, line)) {
    const auto equals = line.find('=');
    if (equals == std::string::npos) continue;
    const std::string key = line.substr(0, equals);
    const std::string value = line.substr(equals + 1);
    if (key == "folder" && !value.empty() && value.front() == '/') out.folder = value;
    if (key == "format") {
      ExportSettings settings;
      if (parse_export_settings("format=" + value, settings) == ResultCode::Ok) out.format = settings.format;
    }
  }
  return out;
}

ResultCode ExportSession::remember_defaults(const ExportDefaults& defaults) {
  std::string text;
  if (!defaults.folder.empty() && defaults.folder.find('\n') == std::string::npos) {
    text += "folder=" + defaults.folder + "\n";
  }
  if (defaults.format) {
    ExportSettings settings;
    settings.format = *defaults.format;
    text += encode_export_settings(settings) + "\n";  // "format=<name>"
  }
  const std::string path = options_.data_directory + "/exports/defaults";
  const std::string temp = path + ".tmp";
  {
    std::ofstream out(temp, std::ios::trunc);
    out << text;
    if (!out) return ResultCode::IoFailed;
  }
  return ::rename(temp.c_str(), path.c_str()) == 0 ? ResultCode::Ok : ResultCode::IoFailed;
}

std::vector<ExportRow> ExportSession::rows() {
  std::vector<ExportRow> rows;
  std::unique_ptr<ExportBook> current;
  if (book(current) != ResultCode::Ok) return rows;
  const ExportQueue& queue = current->queue();
  for (const auto& [id, status] : queue.cache().statuses) {
    ExportJob job;
    if (!queue.find_job(id, job)) continue;
    ExportRow row;
    row.export_id = id;
    row.photo_id = photo_of(job.original_id);
    row.destination_path = job.destination_path;
    row.file_name = name_of(job.destination_path);
    row.status = status;
    if (status == "失败") {
      row.reason = queue.failure_text(id);
      if (row.reason.empty()) row.reason = failure_label(queue.failure_reason(id));
      row.can_retry = true;
    }
    row.can_cancel = status == "排队";
    row.succeeded = status == "成功";
    rows.push_back(std::move(row));
  }
  std::reverse(rows.begin(), rows.end());
  return rows;
}

bool ExportSession::read_progress() {
  if (!client_) return false;
  bool any = false;
  for (;;) {
    ipc::ExportProgressMessage progress;
    std::string diagnostic;
    const ResultCode code = client_->receive(progress, 0, diagnostic);
    if (code == ResultCode::Ok) {
      any = true;
      continue;
    }
    if (code != ResultCode::Cancelled) {
      // exportd exited: what it finished is in the log; the next submit restarts it.
      client_.reset();
      any = true;
    }
    return any;
  }
}

}  // namespace pixcake::export_session
