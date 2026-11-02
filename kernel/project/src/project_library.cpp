#include "pixcake/project/project_library.hpp"

namespace pixcake {
namespace {

bool valid_origin(const std::string& origin) {
  return origin == kOriginLocal || origin == kOriginCloudUploaded || origin == kOriginCloudDownloaded;
}

bool absolute_posix_path(const std::string& path) {
  return !path.empty() && path.front() == '/' && path.find('\0') == std::string::npos;
}

ProjectResult failed(ResultCode code) {
  ProjectResult result;
  result.code = code;
  return result;
}

}  // namespace

ProjectLibrary::ProjectLibrary(ProjectStore& store, FileSystem& files, const Clock& clock, LogSink* log)
    : store_(store), files_(files), clock_(clock), log_(log) {
  (void)clock_;
}

void ProjectLibrary::note(ResultCode code, const std::string& path) const {
  if (log_ == nullptr) {
    return;
  }
  LogRecord record;
  record.code = code;
  record.message = "project";
  record.path = path;
  log_->write(record);
}

ResultCode ProjectLibrary::startup() {
  const ResultCode purged = purge_expired_trash();
  if (purged != ResultCode::Ok) {
    return purged;
  }
  return refresh_source_presence();
}

ResultCode ProjectLibrary::refresh_source_presence() {
  std::vector<PhotoRow> photos;
  const ResultCode listed = store_.list_photos(std::nullopt, photos);
  if (listed != ResultCode::Ok) {
    return listed;
  }
  ResultCode probe_error = ResultCode::Ok;
  for (const PhotoRow& photo : photos) {
    const IoResult probe = files_.probe_file(photo.absolute_path);
    if (probe.ok) {
      if (photo.presence != kPresenceEditable) {
        const ResultCode updated = store_.update_photo_presence(photo.id, kPresenceEditable);
        if (updated != ResultCode::Ok) {
          return updated;
        }
      }
      continue;
    }
    if (probe.not_found) {
      if (photo.presence != kPresenceMissing) {
        const ResultCode updated = store_.update_photo_presence(photo.id, kPresenceMissing);
        if (updated != ResultCode::Ok) {
          return updated;
        }
      }
      note(ResultCode::SourceMissing, photo.absolute_path);
      continue;
    }
    note(probe.code, photo.absolute_path);
    if (probe_error == ResultCode::Ok) {
      probe_error = probe.code;
    }
  }
  return probe_error;
}

ResultCode ProjectLibrary::source_status(std::int64_t photo_id) const {
  std::optional<PhotoRow> photo;
  const ResultCode loaded = store_.load_photo(photo_id, photo);
  if (loaded != ResultCode::Ok) {
    return loaded;
  }
  if (!photo.has_value()) {
    return ResultCode::StateConflict;
  }
  if (photo->presence == kPresenceMissing) {
    return ResultCode::SourceMissing;
  }
  if (photo->presence == kPresenceEditable) {
    return ResultCode::Ok;
  }
  return ResultCode::StateConflict;
}

ResultCode ProjectLibrary::purge_expired_trash() { return ResultCode::Ok; }

ProjectResult ProjectLibrary::create_project(std::string name, std::string origin, bool cloud_marked) {
  if (!valid_origin(origin)) {
    return failed(ResultCode::StateConflict);
  }
  ProjectDraft draft;
  draft.name = std::move(name);
  draft.origin = std::move(origin);
  draft.cloud_marked = cloud_marked;
  ProjectResult result;
  result.code = store_.insert_project(draft, result.id);
  return result;
}

ProjectResult ProjectLibrary::add_photo(std::int64_t project_id, std::string absolute_path,
                                        std::string file_name, std::vector<std::uint8_t> access_credential) {
  if (!absolute_posix_path(absolute_path) || file_name.empty()) {
    return failed(ResultCode::StateConflict);
  }
  std::optional<ProjectRow> project;
  const ResultCode loaded = store_.load_project(project_id, project);
  if (loaded != ResultCode::Ok) {
    return failed(loaded);
  }
  if (!project.has_value()) {
    return failed(ResultCode::StateConflict);
  }
  std::optional<TrashRow> trashed_project;
  const ResultCode trash = store_.load_trash(kTrashProject, project_id, trashed_project);
  if (trash != ResultCode::Ok) {
    return failed(trash);
  }
  if (trashed_project.has_value()) {
    return failed(ResultCode::StateConflict);
  }

  PhotoDraft draft;
  draft.project_id = project_id;
  draft.absolute_path = std::move(absolute_path);
  draft.file_name = std::move(file_name);
  draft.access_credential = std::move(access_credential);
  draft.presence = kPresenceEditable;
  ProjectResult result;
  result.code = store_.insert_photo(draft, result.id);
  return result;
}

ResultCode ProjectLibrary::list_projects(std::vector<ProjectRow>& out) const {
  return store_.list_projects(out);
}

ResultCode ProjectLibrary::list_photos(const std::optional<std::int64_t>& project_id,
                                       std::vector<PhotoRow>& out) const {
  return store_.list_photos(project_id, out);
}

ResultCode ProjectLibrary::load_photo(std::int64_t id, std::optional<PhotoRow>& out) const {
  return store_.load_photo(id, out);
}

}  // namespace pixcake
