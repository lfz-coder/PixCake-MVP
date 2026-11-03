#include "pixcake/exportq/export_queue.hpp"

#include "pixcake/support/checksum.hpp"

#include <map>
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
  job.document_revision = event.document_revision;
  job.destination_path = event.destination_path;
  job.settings_snapshot = event.settings_snapshot;
  job.watermark_free_second = event.watermark_free_second;
  job.print_requested = event.print_requested;
  return job;
}

ExportEvent event_from(const ExportJob& job, std::string kind) {
  ExportEvent event;
  event.kind = std::move(kind);
  event.export_id = job.export_id;
  event.original_id = job.original_id;
  event.document_revision = job.document_revision;
  event.destination_path = job.destination_path;
  event.settings_snapshot = job.settings_snapshot;
  event.watermark_free_second = job.watermark_free_second;
  event.print_requested = job.print_requested;
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
    } else if (event.kind == "commit-begin") {
      job.status = "提交到目标路径";
      job.commit_begun = true;
      job.checksum = event.checksum;
    } else if (event.kind == "commit-end") {
      job.status = "成功";
      ExportProductFact fact;
      fact.export_id = job.job.export_id;
      fact.original_id = job.job.original_id;
      fact.watermark_free_second = job.job.watermark_free_second;
      fact.product_exists = true;
      shot.products.push_back(std::move(fact));
    } else if (event.kind == "fail") {
      job.status = "失败";
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
  if (!safe_export_id(job.export_id) || job.original_id.empty() || job.original_id.find('\0') != std::string::npos ||
      !absolute_destination(job.destination_path)) {
    return ResultCode::StateConflict;
  }
  const auto shot = fold(events_);
  if (shot.jobs.find(job.export_id) != shot.jobs.end()) {
    return ResultCode::Ok;
  }
  return note(event_from(job, "enqueue"));
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
  const std::uint64_t checksum = checksum_of(bytes);
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
    return cleared;
  }
  const IoResult written = files.write_new(temp, bytes);
  if (!written.ok) {
    remove_if_present(files, temp);
    ExportEvent failed = event_from(job, "fail");
    const ResultCode noted = note(std::move(failed));
    return noted == ResultCode::Ok ? written.code : noted;
  }
  const IoResult synced = files.sync_file(temp);
  if (!synced.ok) {
    remove_if_present(files, temp);
    ExportEvent failed = event_from(job, "fail");
    const ResultCode noted = note(std::move(failed));
    return noted == ResultCode::Ok ? synced.code : noted;
  }
  bool temp_present = false;
  std::uint64_t temp_checksum = 0;
  const ResultCode reread = read_checksum(files, temp, temp_present, temp_checksum);
  if (reread != ResultCode::Ok || !temp_present || temp_checksum != checksum) {
    remove_if_present(files, temp);
    ExportEvent failed = event_from(job, "fail");
    const ResultCode noted = note(std::move(failed));
    return noted == ResultCode::Ok ? ResultCode::IoFailed : noted;
  }
  ExportEvent temp_end = event_from(job, "temp-end");
  temp_end.checksum = checksum;
  const ResultCode ended = note(std::move(temp_end));
  if (ended != ResultCode::Ok) {
    return ended;
  }
  ExportEvent commit_begin = event_from(job, "commit-begin");
  commit_begin.checksum = checksum;
  const ResultCode commit_noted = note(std::move(commit_begin));
  if (commit_noted != ResultCode::Ok) {
    return commit_noted;
  }
  const IoResult renamed = files.rename_file(temp, job.destination_path);
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
  if (verified != ResultCode::Ok || !committed_present || committed_checksum != checksum) {
    const ResultCode removed = remove_if_present(files, job.destination_path);
    if (removed != ResultCode::Ok) {
      return removed;
    }
    const IoResult parent_synced = files.sync_directory(parent_directory(job.destination_path));
    if (!parent_synced.ok) {
      return parent_synced.code;
    }
    ExportEvent failed = event_from(job, "fail");
    const ResultCode noted = note(std::move(failed));
    return noted == ResultCode::Ok ? ResultCode::IoFailed : noted;
  }
  ExportEvent commit_end = event_from(job, "commit-end");
  commit_end.checksum = checksum;
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
  const auto shot = fold(events_);
  const auto found = shot.jobs.find(export_id);
  if (found == shot.jobs.end()) {
    return ResultCode::Ok;
  }
  const JobView& job = found->second;
  if (job.status == "成功" || job.status == "失败" || job.status == "取消" || job.status == "排队") {
    return ResultCode::Ok;
  }
  if (job.status == "准备" || job.status == "渲染") {
    ExportEvent event;
    event.kind = "requeue";
    event.export_id = export_id;
    return note(std::move(event));
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
      remove_if_present(files, temp);
      ExportEvent failed;
      failed.kind = "fail";
      failed.export_id = export_id;
      return note(std::move(failed));
    }
    ExportEvent begin = event_from(job.job, "commit-begin");
    begin.checksum = job.checksum;
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
    ExportEvent failed;
    failed.kind = "fail";
    failed.export_id = export_id;
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
    const IoResult renamed = files.rename_file(temp, job.job.destination_path);
    if (!renamed.ok) {
      return renamed.code;
    }
    const ResultCode verified = read_checksum(files, job.job.destination_path, present, destination_checksum);
    if (verified != ResultCode::Ok) {
      return verified;
    }
    if (!present || destination_checksum != job.checksum) {
      const ResultCode removed = remove_if_present(files, job.job.destination_path);
      if (removed != ResultCode::Ok) {
        return removed;
      }
      const IoResult parent_synced = files.sync_directory(parent_directory(job.job.destination_path));
      if (!parent_synced.ok) {
        return parent_synced.code;
      }
      ExportEvent failed;
      failed.kind = "fail";
      failed.export_id = export_id;
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
  return note(std::move(end));
}

}  // namespace pixcake
