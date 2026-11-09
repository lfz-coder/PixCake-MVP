#include "pixcake/export_session/export_session.hpp"

#include "pixcake/command/business_commands.hpp"
#include "pixcake/editdoc/edit_document.hpp"
#include "pixcake/support/checksum.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
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
    case ResultCode::InsufficientBalance: return "剩余张数不足";
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
  ResultCode ledger_code = SqliteLedgerLog::open(created->options_.data_directory + "/ledger.db", created->ledger_log_);
  if (ledger_code == ResultCode::Ok) {
    ledger_code = LedgerBook::open(FreeQueuePolicy{}, *created->ledger_log_, created->ledger_);
  }
  std::size_t ledger_events = 0;
  if (ledger_code == ResultCode::Ok) ledger_code = created->ledger_log_->event_count(ledger_events);
  if (ledger_code == ResultCode::Ok && ledger_events == 0 && created->options_.opening_sheets > 0) {
    ledger_code = created->ledger_->credit_sheets(created->options_.opening_sheets);
  }
  if (ledger_code != ResultCode::Ok) {
    diagnostic = "ledger unavailable: " + created->options_.data_directory + "/ledger.db";
    return ledger_code;
  }
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
  ExportSettings without_watermark = request.settings;
  without_watermark.watermark.reset();
  const std::string plain_settings = encode_export_settings(without_watermark);
  BusinessCommands commands(session_);
  std::vector<ExportJob> accepted;
  std::vector<std::string> charging;  // pictures this request will charge
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
      document = EditDocument::create(projects_.image_id(photo_id))->encode();
    }
    const auto decoded = EditDocument::decode(document);
    if (!decoded) {
      notices.push_back(photo->file_name + ": " + "参数文件无法读取");
      continue;
    }
    if (classify_export(document) == ExportEffect::Refined) {
      const std::string identity = picture_identity(photo_id, charging);
      const bool already = ledger_->ledger().original_charged(identity) ||
                           std::find(charging.begin(), charging.end(), identity) != charging.end();
      if (!already) {
        if (remaining_sheets() <= static_cast<std::int64_t>(charging.size())) {
          notices.push_back(photo->file_name + ": " + failure_label(ResultCode::InsufficientBalance));
          continue;
        }
        charging.push_back(identity);
      }
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
    // 同时导出无水印图片（test 版实测，docs/superpowers/evidence/2026-11-07-watermark-observation.md）：
    // 导出文件夹下建“文件夹名_有水印”“文件夹名_无水印”两个子文件夹，各放一份同名图。
    // 无水印那份是同一批的第二个任务，标 watermark_free_second，不另扣张数。
    const bool both = request.watermark_free_copy && request.settings.watermark.has_value();
    std::vector<std::pair<std::string, bool>> targets;  // folder, the watermark-free second
    if (both) {
      const std::string base = name_of(folder);
      targets = {{folder + "/" + base + "_有水印", false}, {folder + "/" + base + "_无水印", true}};
      if (!make_directory(targets[0].first) || !make_directory(targets[1].first)) {
        notices.push_back(photo->file_name + ": " + failure_label(ResultCode::PermissionDenied));
        continue;
      }
    } else {
      targets = {{folder, false}};
    }
    for (const auto& [target, free_second] : targets) {
      // Name collisions: "name_(2).jpg", "name_(3).jpg" -- what the test build
      // writes (docs/superpowers/evidence/2026-11-07-export-credit-observation.md).
      const std::string stem = stem_of(photo->file_name);
      const std::string extension = extension_for(request.settings.format, photo->format);
      std::string destination = target + "/" + stem + "." + extension;
      for (int n = 2; taken.count(destination) != 0 || ::access(destination.c_str(), F_OK) == 0; ++n) {
        destination = target + "/" + stem + "_(" + std::to_string(n) + ")." + extension;
      }
      ExportJob job;
      job.export_id = next_export_id();
      if (batch_id.empty()) batch_id = "batch-" + job.export_id.substr(4);
      job.batch_id = batch_id;
      // 副本沿用原图的图片标识（文档里存的就是它），导出进程核对的也是它。
      job.original_id = decoded->image_id();
      job.document_revision = decoded->revision();
      job.document_snapshot = document;
      job.destination_path = destination;
      job.destination_root = root;
      job.settings_snapshot = free_second ? plain_settings : settings;
      job.watermark_free_second = free_second;
      code = commands.enqueue_export(queue, job);
      if (code != ResultCode::Ok) {
        notices.push_back(photo->file_name + ": " + failure_label(code));
        break;
      }
      taken.insert(destination);
      accepted.push_back(std::move(job));
    }
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
  std::string unsettled;
  if (settle(unsettled) != ResultCode::Ok) diagnostic = unsettled;
  return ResultCode::Ok;
}

std::string ExportSession::picture_identity(std::int64_t photo_id, const std::vector<std::string>& also) {
  const std::string fallback = projects_.image_id(photo_id);
  std::string source;
  std::string reason;
  std::string fingerprint;
  if (projects_.resolve_photo(photo_id, source, reason) != ResultCode::Ok ||
      picture_fingerprint(source, fingerprint, reason) != ResultCode::Ok) {
    return fallback;
  }
  for (const auto& charged : ledger_->ledger().charged_originals()) {
    if (same_picture(charged, fingerprint)) return charged;
  }
  for (const auto& pending : also) {
    if (same_picture(pending, fingerprint)) return pending;
  }
  return fingerprint;
}

ExportEstimate ExportSession::estimate(const std::vector<std::int64_t>& photo_ids) {
  ExportEstimate estimate;
  std::vector<std::string> charging;
  for (const std::int64_t photo_id : photo_ids) {
    std::vector<std::uint8_t> document;
    projects_.document_bytes(photo_id, document);
    const ExportEffect effect = document.empty() ? ExportEffect::Original : classify_export(document);
    if (effect == ExportEffect::Original) {
      ++estimate.original;
    } else if (effect == ExportEffect::Free) {
      ++estimate.free;
    } else {
      ++estimate.refined;
      const std::string identity = picture_identity(photo_id, charging);
      if (!ledger_->ledger().original_charged(identity) &&
          std::find(charging.begin(), charging.end(), identity) == charging.end()) {
        charging.push_back(identity);
      }
    }
  }
  estimate.sheets = static_cast<std::int64_t>(charging.size());
  return estimate;
}

std::int64_t ExportSession::remaining_sheets() const { return ledger_->ledger().export_sheets(); }

ResultCode ExportSession::settle(std::string& diagnostic) {
  diagnostic.clear();
  if (!session_.signed_in()) return ResultCode::StateConflict;
  std::unique_ptr<ExportBook> current;
  ResultCode code = book(current);
  if (code != ResultCode::Ok) return code;
  BusinessCommands commands(session_);
  ResultCode outcome = ResultCode::Ok;
  for (const auto& fact : current->queue().products()) {
    if (!fact.product_exists || ledger_->ledger().export_settled(fact.export_id)) continue;
    ExportJob job;
    if (!current->queue().find_job(fact.export_id, job)) continue;
    ExportProduct product;
    product.export_id = fact.export_id;
    product.product_exists = true;
    product.watermark_free_second = fact.watermark_free_second;
    product.chargeable = classify_export(job.document_snapshot) == ExportEffect::Refined;
    std::int64_t photo_id = 0;
    if (job.original_id.rfind("photo-", 0) == 0) photo_id = std::atoll(job.original_id.c_str() + 6);
    product.original_id = photo_id > 0 ? picture_identity(photo_id, {}) : job.original_id;
    code = commands.settle_exports(*ledger_, {product});
    if (code != ResultCode::Ok) {
      // 张数不够（提交后别处扣掉了）：成品已经写出，记账留到下次。
      diagnostic = fact.export_id + ": " + failure_label(code);
      outcome = code;
    }
  }
  return outcome;
}

ResultCode ExportSession::store_watermark(const std::string& source_path, std::string& material,
                                          std::string& diagnostic) {
  const std::string extension = lower(source_path.substr(source_path.rfind('.') == std::string::npos
                                                             ? source_path.size()
                                                             : source_path.rfind('.') + 1));
  if (extension != "png" && extension != "jpg" && extension != "jpeg") {
    diagnostic = "水印素材只支持 JPG、PNG";
    return ResultCode::UnsupportedFormat;
  }
  std::ifstream in(source_path, std::ios::binary);
  if (!in) {
    diagnostic = "水印素材无法读取";
    return ResultCode::SourceMissing;
  }
  in.seekg(0, std::ios::end);
  const auto size = static_cast<std::uint64_t>(std::max<std::streamoff>(0, in.tellg()));
  if (size == 0 || size > kMaxWatermarkBytes) {
    diagnostic = size == 0 ? "水印素材是空文件" : "水印素材超过 10MB";
    return ResultCode::UnsupportedFormat;
  }
  in.seekg(0);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  if (!in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
    diagnostic = "水印素材无法读取";
    return ResultCode::IoFailed;
  }
  char hex[17];
  std::snprintf(hex, sizeof(hex), "%016llx",
                static_cast<unsigned long long>(checksum_fnv1a64(bytes.data(), bytes.size())));
  const std::string name =
      std::string(hex) + "-" + std::to_string(size) + (extension == "png" ? ".png" : ".jpg");
  const std::string directory = options_.data_directory + "/exports/watermarks";
  if (!make_directory(directory)) {
    diagnostic = "水印素材目录无法创建";
    return ResultCode::IoFailed;
  }
  const std::string path = directory + "/" + name;
  if (!files_.exists(path)) {
    const auto written = files_.write_new(path, bytes);
    if (!written.ok && !files_.exists(path)) {
      diagnostic = "水印素材无法保存";
      return ResultCode::IoFailed;
    }
  }
  material = name;
  diagnostic.clear();
  return ResultCode::Ok;
}

std::string ExportSession::watermark_path(const std::string& material) const {
  if (!valid_watermark_material(material)) return {};
  return options_.data_directory + "/exports/watermarks/" + material;
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
    if (key == "watermark_on") out.watermark_on = value == "1";
    if (key == "watermark_free_copy") out.watermark_free_copy = value == "1";
    if (key == "watermark") {
      ExportSettings settings;
      if (parse_export_settings(value, settings) == ResultCode::Ok) out.watermark = settings.watermark;
    }
    if (key == "layout_on") out.layout_on = value == "1";
    if (key == "layout") {
      ExportSettings settings;
      if (parse_export_settings(value, settings) == ResultCode::Ok) out.layout = settings.layout;
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
  text += std::string("watermark_on=") + (defaults.watermark_on ? "1" : "0") + "\n";
  text += std::string("watermark_free_copy=") + (defaults.watermark_free_copy ? "1" : "0") + "\n";
  if (defaults.watermark) {
    ExportSettings settings;
    settings.watermark = defaults.watermark;
    text += "watermark=" + encode_export_settings(settings) + "\n";
  }
  text += std::string("layout_on=") + (defaults.layout_on ? "1" : "0") + "\n";
  if (defaults.layout) {
    ExportSettings settings;
    settings.layout = defaults.layout;
    text += "layout=" + encode_export_settings(settings) + "\n";
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
    if (row.succeeded) {
      if (!ledger_->ledger().export_settled(id)) {
        row.charge = ExportCharge::Pending;
      } else if (ledger_->ledger().export_charged(id)) {
        row.charge = ExportCharge::Charged;
      } else {
        auto effect = effects_.find(id);
        if (effect == effects_.end()) effect = effects_.emplace(id, classify_export(job.document_snapshot)).first;
        row.charge = effect->second == ExportEffect::Refined && job.watermark_free_second ? ExportCharge::FreeCopy
                     : effect->second == ExportEffect::Refined ? ExportCharge::Repeat
                     : effect->second == ExportEffect::Free  ? ExportCharge::Free
                                                             : ExportCharge::Original;
      }
    }
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
    if (any) {
      std::string unsettled;
      settle(unsettled);  // products committed since the last read
    }
    return any;
  }
}

}  // namespace pixcake::export_session
