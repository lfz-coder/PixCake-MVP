#include "pixcake/exportq/export_queue.hpp"
#include "pixcake/exportq/export_settings.hpp"

#include "pixcake/support/checksum.hpp"

#include <map>
#include <string>
#include <cstdlib>
#include <utility>

namespace pixcake {
namespace {

struct JobView {
  ExportJob job;
  std::string status;
  bool temp_begun = false;
  bool temp_done = false;
  bool commit_begun = false;
  std::uint64_t checksum = 0;
  ResultCode failure_code = ResultCode::Ok;
};

struct Fold {
  bool fast_export = false;
  std::map<std::string, JobView> jobs;
  std::vector<ExportProductFact> products;
};

ExportJob job_from(const ExportEvent& event) {
  ExportJob job;
  job.export_id = event.export_id;
  job.original_id = event.original_id;
  job.batch_id = event.batch_id;
  job.document_revision = event.document_revision;
  job.document_snapshot = event.document_snapshot;
  job.destination_path = event.destination_path;
  job.destination_root = event.destination_root;
  if (job.destination_root.empty()) {
    const auto slash = job.destination_path.rfind('/');
    job.destination_root = slash == std::string::npos || slash == 0 ? "/" : job.destination_path.substr(0, slash);
  }
  job.settings_snapshot = event.settings_snapshot;
  job.watermark_free_second = event.watermark_free_second;
  job.print_requested = event.print_requested;
  job.destination_identity = event.destination_identity;
  return job;
}

ExportEvent event_from(const ExportJob& job, std::string kind) {
  ExportEvent event;
  event.kind = kind;
  event.export_id = job.export_id;
  event.original_id = job.original_id;
  event.batch_id = job.batch_id.empty() ? job.export_id : job.batch_id;
  event.document_revision = job.document_revision;
  event.document_snapshot = job.document_snapshot;
  event.destination_path = job.destination_path;
  event.destination_root = job.destination_root;
  event.settings_snapshot = job.settings_snapshot;
  event.watermark_free_second = job.watermark_free_second;
  event.print_requested = job.print_requested;
  event.destination_identity = job.destination_identity;
  event.failure_code = ResultCode::Ok;
  event.phase = std::move(kind);
  return event;
}

Fold fold(const std::vector<ExportEvent>& events) {
  Fold shot;
  for (const auto& event : events) {
    if (event.kind == "fast-on") {
      shot.fast_export = true;
      continue;
    }
    if (event.kind == "fast-off") {
      shot.fast_export = false;
      continue;
    }
    auto& job = shot.jobs[event.export_id];
    if (event.kind == "enqueue") {
      job.job = job_from(event);
      job.status = "排队";
    } else if (event.kind == "prepare") {
      job.status = "准备";
    } else if (event.kind == "render") {
      job.status = "渲染";
    } else if (event.kind == "temp-begin") {
      job.status = "写临时文件";
      job.temp_begun = true;
    } else if (event.kind == "temp-end") {
      job.status = "写临时文件";
      job.temp_done = true;
      job.checksum = event.checksum;
      job.job.destination_identity = event.destination_identity;
    } else if (event.kind == "commit-begin") {
      job.status = "提交到目标路径";
      job.commit_begun = true;
      job.checksum = event.checksum;
      job.job.destination_identity = event.destination_identity;
    } else if (event.kind == "commit-end") {
      job.status = "成功";
      job.job.destination_identity = event.destination_identity;
      ExportProductFact fact;
      fact.export_id = job.job.export_id;
      fact.original_id = job.job.original_id;
      fact.batch_id = job.job.batch_id;
      fact.watermark_free_second = job.job.watermark_free_second;
      fact.product_exists = true;
      shot.products.push_back(std::move(fact));
    } else if (event.kind == "fail") {
      job.status = "失败";
      job.failure_code = event.failure_code;
    } else if (event.kind == "cleanup-pending") {
      job.status = "待清理临时文件";
      job.failure_code = event.failure_code;
    } else if (event.kind == "cancel") {
      job.status = "取消";
    } else if (event.kind == "requeue") {
      job.status = "排队";
      job.temp_begun = false;
      job.temp_done = false;
      job.commit_begun = false;
      job.checksum = 0;
    }
  }
  return shot;
}

bool safe_export_id(const std::string& export_id) {
  return !export_id.empty() && export_id != "." && export_id != ".." &&
         export_id.find('/') == std::string::npos && export_id.find('\\') == std::string::npos &&
         export_id.find('\0') == std::string::npos;
}

bool absolute_destination(const std::string& path) {
  if (path.size() <= 1 || path.front() != '/' || path.back() == '/' || path.find('\0') != std::string::npos) {
    return false;
  }
  std::size_t start = 1;
  while (start < path.size()) {
    const std::size_t end = path.find('/', start);
    const std::string component = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (component == ".." || component == ".") {
      return false;
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  return true;
}

bool path_within_root(const std::string& path, const std::string& root) {
  if (!absolute_destination(path) || !absolute_destination(root)) return false;
  if (path == root) return false;
  return path.rfind(root + "/", 0) == 0;
}

std::string default_root(const std::string& path) {
  const auto slash = path.rfind('/');
  return slash == std::string::npos || slash == 0 ? "/" : path.substr(0, slash);
}

std::uint64_t checksum_of(const std::vector<std::uint8_t>& bytes) {
  return checksum_fnv1a64(bytes.data(), bytes.size());
}

ResultCode read_checksum(FileSystem& files, const std::string& path, bool& present, std::uint64_t& checksum) {
  present = false;
  if (!files.exists(path)) {
    return ResultCode::Ok;
  }
  std::vector<std::uint8_t> bytes;
  const IoResult read = files.read_file(path, bytes);
  if (!read.ok) {
    return read.not_found ? ResultCode::Ok : read.code;
  }
  present = true;
  checksum = checksum_of(bytes);
  return ResultCode::Ok;
}

bool same_identity(const FileIdentity& left, const FileIdentity& right) {
  return left.valid && right.valid && left.device == right.device && left.inode == right.inode &&
         left.size == right.size;
}

ResultCode remove_if_present(FileSystem& files, const std::string& path) {
  if (!files.exists(path)) {
    return ResultCode::Ok;
  }
  const IoResult removed = files.remove_file(path);
  if (!removed.ok && !removed.not_found) {
    return removed.code;
  }
  return ResultCode::Ok;
}

std::string parent_directory(const std::string& path) {
  const auto slash = path.rfind('/');
  if (slash == std::string::npos || slash == 0) {
    return "/";
  }
  return path.substr(0, slash);
}

}  // namespace

ExportQueue::ExportQueue(std::string cache_directory, std::vector<ExportEvent> events)
    : cache_directory_(std::move(cache_directory)), events_(std::move(events)) {}

ExportQueue ExportQueue::open(std::string cache_directory, std::vector<ExportEvent> events) {
  return ExportQueue(std::move(cache_directory), std::move(events));
}

ExportQueue::ExportQueue(ExportQueue&&) noexcept = default;
ExportQueue& ExportQueue::operator=(ExportQueue&&) noexcept = default;

void ExportQueue::set_sink(ExportEventSink* sink) { sink_ = sink; }

const std::vector<ExportEvent>& ExportQueue::events() const { return events_; }

bool ExportQueue::enabled() const { return fold(events_).fast_export; }

std::string ExportQueue::status(const std::string& export_id) const {
  const auto shot = fold(events_);
  const auto found = shot.jobs.find(export_id);
  if (found == shot.jobs.end()) {
    return "";
  }
  return found->second.status;
}

ResultCode ExportQueue::failure_reason(const std::string& export_id) const {
  const auto shot = fold(events_);
  const auto found = shot.jobs.find(export_id);
  if (found == shot.jobs.end()) return ResultCode::StateConflict;
  return found->second.failure_code;
}

bool ExportQueue::find_job(const std::string& export_id, ExportJob& out) const {
  const auto shot = fold(events_);
  const auto found = shot.jobs.find(export_id);
  if (found == shot.jobs.end() || found->second.job.export_id.empty()) {
    return false;
  }
  out = found->second.job;
  return true;
}

std::vector<ExportProductFact> ExportQueue::products() const { return fold(events_).products; }

ExportCache ExportQueue::cache() const {
  ExportCache cache;
  const auto shot = fold(events_);
  cache.fast_export = shot.fast_export;
  for (const auto& item : shot.jobs) {
    if (!item.second.job.export_id.empty()) {
      cache.statuses.emplace_back(item.first, item.second.status);
    }
  }
  return cache;
}

std::string ExportQueue::temp_path(const std::string& export_id) const {
  return cache_directory_ + "/" + export_id;
}

ResultCode ExportQueue::note(ExportEvent event) {
  events_.push_back(std::move(event));
  if (sink_ == nullptr) {
    return ResultCode::Ok;
  }
  const ResultCode persisted = sink_->persist(events_.back(), cache());
  if (persisted != ResultCode::Ok) {
    events_.pop_back();
    return persisted;
  }
  return ResultCode::Ok;
}

ResultCode ExportQueue::set_fast_export(bool on) {
  if (enabled() == on) {
    return ResultCode::Ok;
  }
  ExportEvent event;
  event.kind = on ? "fast-on" : "fast-off";
  return note(std::move(event));
}

ResultCode ExportQueue::enqueue(const ExportJob& job) {
  ExportJob normalized = job;
  if (normalized.batch_id.empty()) {
    normalized.batch_id = normalized.export_id;
  }
  if (normalized.destination_root.empty()) {
    normalized.destination_root = default_root(normalized.destination_path);
  }
  if (!safe_export_id(normalized.export_id) || normalized.original_id.empty() || normalized.document_snapshot.empty() ||
      normalized.original_id.find('\0') != std::string::npos ||
      !path_within_root(normalized.destination_path, normalized.destination_root)) {
    return ResultCode::StateConflict;
  }
  ExportSettings settings;
  if (parse_export_settings(normalized.settings_snapshot, settings) != ResultCode::Ok) {
    return ResultCode::StateConflict;
  }
  normalized.settings_snapshot = encode_export_settings(settings);
  const auto shot = fold(events_);
  if (shot.jobs.find(normalized.export_id) != shot.jobs.end()) {
    return ResultCode::Ok;
  }
  return note(event_from(normalized, "enqueue"));
}

ResultCode ExportQueue::prepare(const std::string& export_id) {
  const auto shot = fold(events_);
  const auto found = shot.jobs.find(export_id);
  if (found == shot.jobs.end()) {
    return ResultCode::StateConflict;
  }
  if (found->second.status == "准备") {
    return ResultCode::Ok;
  }
  if (found->second.status != "排队") {
    return ResultCode::StateConflict;
  }
  ExportEvent event;
  event.kind = "prepare";
  event.export_id = export_id;
  return note(std::move(event));
}

ResultCode ExportQueue::start_rendering(const std::string& export_id) {
  const auto shot = fold(events_);
  const auto found = shot.jobs.find(export_id);
  if (found == shot.jobs.end()) {
    return ResultCode::StateConflict;
  }
  if (found->second.status == "渲染") {
    return ResultCode::Ok;
  }
  if (found->second.status != "准备") {
    return ResultCode::StateConflict;
  }
  ExportEvent event;
  event.kind = "render";
  event.export_id = export_id;
  return note(std::move(event));
}

ResultCode ExportQueue::fail(const std::string& export_id) {
  const auto shot = fold(events_);
  const auto found = shot.jobs.find(export_id);
  if (found == shot.jobs.end()) {
    return ResultCode::StateConflict;
  }
  if (found->second.status == "失败") {
    return ResultCode::Ok;
  }
  if (found->second.status != "排队" && found->second.status != "准备" && found->second.status != "渲染") {
    return ResultCode::StateConflict;
  }
  ExportEvent event;
  event.kind = "fail";
  event.export_id = export_id;
  return note(std::move(event));
}

ResultCode ExportQueue::retry(const std::string& export_id) {
  const auto shot = fold(events_);
  const auto found = shot.jobs.find(export_id);
  if (found == shot.jobs.end()) {
    return ResultCode::StateConflict;
  }
  if (found->second.status == "排队") {
    return ResultCode::Ok;
  }
  if (found->second.status != "失败") {
    return ResultCode::StateConflict;
  }
  ExportEvent event;
  event.kind = "requeue";
  event.export_id = export_id;
  return note(std::move(event));
}

ResultCode ExportQueue::request_print(const std::string& export_id, bool printer_available, bool& unavailable) {
  ExportJob job;
  if (!find_job(export_id, job) || !job.print_requested) {
    return ResultCode::StateConflict;
  }
  unavailable = !printer_available;
  return ResultCode::Ok;
}

ResultCode ExportQueue::cancel(const std::string& export_id) {
  const auto shot = fold(events_);
  const auto found = shot.jobs.find(export_id);
  if (found == shot.jobs.end()) {
    return ResultCode::StateConflict;
  }
  if (found->second.status == "取消") {
    return ResultCode::Ok;
  }
  if (found->second.commit_begun ||
      (found->second.status != "排队" && found->second.status != "准备" && found->second.status != "渲染")) {
    return ResultCode::StateConflict;
  }
  ExportEvent event;
  event.kind = "cancel";
  event.export_id = export_id;
  return note(std::move(event));
}

ResultCode ExportQueue::commit_product(const std::string& export_id, const std::vector<std::uint8_t>& bytes,
                                       FileSystem& files) {
  const auto shot = fold(events_);
  const auto found = shot.jobs.find(export_id);
  if (found == shot.jobs.end() || bytes.empty()) {
    return ResultCode::StateConflict;
  }
  if (found->second.status == "成功") {
    return ResultCode::Ok;
  }
  if (found->second.status != "渲染") {
    return ResultCode::StateConflict;
  }
  const ExportJob& job = found->second.job;
  const IoResult within_root = files.path_within_root(job.destination_path, job.destination_root);
  if (!within_root.ok) {
    return within_root.code;
  }
  const std::uint64_t checksum = checksum_of(bytes);
  auto record_cleanup_pending = [&](ResultCode cleanup_code) {
    ExportEvent pending = event_from(job, "cleanup-pending");
    pending.failure_code = cleanup_code;
    return note(std::move(pending));
  };
  bool destination_present = false;
  std::uint64_t destination_checksum = 0;
  const ResultCode looked = read_checksum(files, job.destination_path, destination_present, destination_checksum);
  if (looked != ResultCode::Ok) {
    return looked;
  }
  if (destination_present) {
    return ResultCode::StateConflict;
  }

  const IoResult directory = files.create_directory(cache_directory_);
  if (!directory.ok) {
    return directory.code;
  }
  ExportEvent temp_begin = event_from(job, "temp-begin");
  const ResultCode began_temp = note(std::move(temp_begin));
  if (began_temp != ResultCode::Ok) {
    return began_temp;
  }
  const std::string temp = temp_path(export_id);
  const ResultCode cleared = remove_if_present(files, temp);
  if (cleared != ResultCode::Ok) {
    const ResultCode noted = record_cleanup_pending(cleared);
    return noted == ResultCode::Ok ? cleared : noted;
  }
  const IoResult written = files.write_new(temp, bytes);
  if (!written.ok) {
    const ResultCode cleanup = remove_if_present(files, temp);
    if (cleanup != ResultCode::Ok) {
      const ResultCode noted = record_cleanup_pending(cleanup);
      return noted == ResultCode::Ok ? written.code : noted;
    }
    ExportEvent failed = event_from(job, "fail");
    failed.failure_code = written.code;
    const ResultCode noted = note(std::move(failed));
    return noted == ResultCode::Ok ? written.code : noted;
  }
  const IoResult synced = files.sync_file(temp);
  if (!synced.ok) {
    const ResultCode cleanup = remove_if_present(files, temp);
    if (cleanup != ResultCode::Ok) {
      const ResultCode noted = record_cleanup_pending(cleanup);
      return noted == ResultCode::Ok ? synced.code : noted;
    }
    ExportEvent failed = event_from(job, "fail");
    failed.failure_code = synced.code;
    const ResultCode noted = note(std::move(failed));
    return noted == ResultCode::Ok ? synced.code : noted;
  }
  bool temp_present = false;
  std::uint64_t temp_checksum = 0;
  const ResultCode reread = read_checksum(files, temp, temp_present, temp_checksum);
  if (reread != ResultCode::Ok || !temp_present || temp_checksum != checksum) {
    const ResultCode cleanup = remove_if_present(files, temp);
    if (cleanup != ResultCode::Ok) {
      const ResultCode noted = record_cleanup_pending(cleanup);
      return noted == ResultCode::Ok ? ResultCode::IoFailed : noted;
    }
    ExportEvent failed = event_from(job, "fail");
    failed.failure_code = ResultCode::IoFailed;
    const ResultCode noted = note(std::move(failed));
    return noted == ResultCode::Ok ? ResultCode::IoFailed : noted;
  }
  ExportEvent temp_end = event_from(job, "temp-end");
  temp_end.checksum = checksum;
  FileIdentity temp_identity;
  const IoResult temp_identified = files.identify(temp, temp_identity);
  if (temp_identified.ok) temp_end.destination_identity = temp_identity;
  const FileIdentity owned_identity = temp_end.destination_identity;
  const ResultCode ended = note(std::move(temp_end));
  if (ended != ResultCode::Ok) {
    return ended;
  }
  ExportEvent commit_begin = event_from(job, "commit-begin");
  commit_begin.checksum = checksum;
  commit_begin.destination_identity = owned_identity;
  const ResultCode commit_noted = note(std::move(commit_begin));
  if (commit_noted != ResultCode::Ok) {
    return commit_noted;
  }
  const IoResult renamed = files.publish_new(temp, job.destination_path);
  if (!renamed.ok) {
    return renamed.code;
  }
  const IoResult directory_synced = files.sync_directory(parent_directory(job.destination_path));
  if (!directory_synced.ok) {
    return directory_synced.code;
  }
  bool committed_present = false;
  std::uint64_t committed_checksum = 0;
  const ResultCode verified = read_checksum(files, job.destination_path, committed_present, committed_checksum);
  FileIdentity committed_identity;
  const IoResult identity = files.identify(job.destination_path, committed_identity);
  if (verified != ResultCode::Ok || !committed_present || committed_checksum != checksum ||
      (owned_identity.valid &&
       (!identity.ok || committed_identity.device != owned_identity.device ||
        committed_identity.inode != owned_identity.inode))) {
    if (owned_identity.valid && identity.ok && committed_identity.device == owned_identity.device &&
        committed_identity.inode == owned_identity.inode) {
      const IoResult removed = files.remove_if_identity(job.destination_path, owned_identity);
      if (!removed.ok && !removed.not_found) return removed.code;
      const IoResult parent_synced = files.sync_directory(parent_directory(job.destination_path));
      if (!parent_synced.ok) return parent_synced.code;
    }
    // A destination replaced by a user, or a destination that could not be
    // identified, remains untouched and the task stays retryable.
    ExportEvent failed = event_from(job, "fail");
    failed.failure_code = verified == ResultCode::Ok ? ResultCode::IoFailed : verified;
    const ResultCode failure = failed.failure_code;
    const ResultCode noted = note(std::move(failed));
    return noted == ResultCode::Ok ? failure : noted;
  }
  ExportEvent commit_end = event_from(job, "commit-end");
  commit_end.checksum = checksum;
  commit_end.destination_identity = owned_identity;
  return note(std::move(commit_end));
}

ResultCode ExportQueue::recover(FileSystem& files) {
  const IoResult directory = files.create_directory(cache_directory_);
  if (!directory.ok) {
    return directory.code;
  }
  std::vector<std::string> ids;
  for (const auto& item : fold(events_).jobs) {
    ids.push_back(item.first);
  }
  ResultCode first = ResultCode::Ok;
  for (const auto& export_id : ids) {
    const ResultCode code = recover_one(export_id, files);
    if (code != ResultCode::Ok && first == ResultCode::Ok) {
      first = code;
    }
  }
  return first;
}

ResultCode ExportQueue::recover_one(const std::string& export_id, FileSystem& files) {
  if (!safe_export_id(export_id)) {
    return ResultCode::StateConflict;
  }
  const auto shot = fold(events_);
  const auto found = shot.jobs.find(export_id);
  if (found == shot.jobs.end()) {
    return ResultCode::Ok;
  }
  const JobView& job = found->second;
  const IoResult within_root = files.path_within_root(job.job.destination_path, job.job.destination_root);
  if (!within_root.ok) return within_root.code;
  if (job.status == "成功" || job.status == "失败" || job.status == "取消" || job.status == "排队") {
    return ResultCode::Ok;
  }
  if (job.status == "准备" || job.status == "渲染") {
    ExportEvent event;
    event.kind = "requeue";
    event.export_id = export_id;
    return note(std::move(event));
  }
  if (job.status == "待清理临时文件") {
    const ResultCode removed = remove_if_present(files, temp_path(export_id));
    if (removed != ResultCode::Ok) {
      return removed;
    }
    ExportEvent failed = event_from(job.job, "fail");
    failed.failure_code = job.failure_code;
    return note(std::move(failed));
  }
  const std::string temp = temp_path(export_id);
  if (!job.commit_begun) {
    if (!job.temp_done) {
      const ResultCode removed = remove_if_present(files, temp);
      if (removed != ResultCode::Ok) {
        return removed;
      }
      ExportEvent failed;
      failed.kind = "fail";
      failed.export_id = export_id;
      return note(std::move(failed));
    }
    bool destination_present = false;
    std::uint64_t existing_checksum = 0;
    const ResultCode existing = read_checksum(files, job.job.destination_path, destination_present, existing_checksum);
    if (existing != ResultCode::Ok) {
      return existing;
    }
    if (destination_present) {
      // A destination that appeared after the task started is not owned by
      // this task.  Preserve it and record a retryable collision.
      ExportEvent failed;
      failed.kind = "fail";
      failed.export_id = export_id;
      failed.failure_code = ResultCode::StateConflict;
      return note(std::move(failed));
    }
    ExportEvent begin = event_from(job.job, "commit-begin");
    begin.checksum = job.checksum;
    begin.destination_identity = job.job.destination_identity;
    const ResultCode began = note(std::move(begin));
    if (began != ResultCode::Ok) {
      return began;
    }
    return recover_one(export_id, files);
  }
  bool present = false;
  std::uint64_t destination_checksum = 0;
  const ResultCode looked = read_checksum(files, job.job.destination_path, present, destination_checksum);
  if (looked != ResultCode::Ok) {
    return looked;
  }
  if (present) {
    // A path occupied before recovery is not provably owned by this task.
    // Preserve it and fail the task instead of deleting a user's file.
    if (job.job.destination_identity.valid) {
      FileIdentity current;
      const IoResult identity = files.identify(job.job.destination_path, current);
      if (identity.ok && current.device == job.job.destination_identity.device &&
          current.inode == job.job.destination_identity.inode) {
        ExportEvent end = event_from(job.job, "commit-end");
        end.checksum = job.checksum;
        return note(std::move(end));
      }
    }
    ExportEvent failed = event_from(job.job, "fail");
    failed.failure_code = ResultCode::StateConflict;
    return note(std::move(failed));
  }
  if (!present) {
    if (!files.exists(temp)) {
      const IoResult parent_synced = files.sync_directory(parent_directory(job.job.destination_path));
      if (!parent_synced.ok) {
        return parent_synced.code;
      }
      ExportEvent failed;
      failed.kind = "fail";
      failed.export_id = export_id;
      return note(std::move(failed));
    }
    bool temp_present = false;
    std::uint64_t temp_checksum = 0;
    const ResultCode temp_read = read_checksum(files, temp, temp_present, temp_checksum);
    if (temp_read != ResultCode::Ok) return temp_read;
    if (!temp_present || temp_checksum != job.checksum) {
      // The temp path is occupied by bytes that this task did not create.
      // Keep it for inspection/retry; never unlink an unproven owner.
      return ResultCode::StateConflict;
    }
    FileIdentity temp_identity;
    const IoResult identified = files.identify(temp, temp_identity);
    if (!identified.ok || !same_identity(temp_identity, job.job.destination_identity)) {
      // A checksum alone does not prove ownership: another task or a user can
      // create identical bytes at the cache path after a crash.
      return ResultCode::StateConflict;
    }
    const IoResult renamed = files.publish_new(temp, job.job.destination_path);
    if (!renamed.ok) {
      return renamed.code;
    }
    const ResultCode verified = read_checksum(files, job.job.destination_path, present, destination_checksum);
    if (verified != ResultCode::Ok) {
      return verified;
    }
    FileIdentity destination_identity;
    const IoResult identity = files.identify(job.job.destination_path, destination_identity);
    if (!present || destination_checksum != job.checksum ||
        (job.job.destination_identity.valid &&
         (!identity.ok || destination_identity.device != job.job.destination_identity.device ||
          destination_identity.inode != job.job.destination_identity.inode))) {
      ExportEvent failed;
      failed.kind = "fail";
      failed.export_id = export_id;
      failed.failure_code = verified == ResultCode::Ok ? ResultCode::IoFailed : verified;
      return note(std::move(failed));
    }
  } else {
    remove_if_present(files, temp);
  }
  const IoResult directory_synced = files.sync_directory(parent_directory(job.job.destination_path));
  if (!directory_synced.ok) {
    return directory_synced.code;
  }
  ExportEvent end = event_from(job.job, "commit-end");
  end.checksum = job.checksum;
  end.destination_identity = job.job.destination_identity;
  return note(std::move(end));
}

}  // namespace pixcake
