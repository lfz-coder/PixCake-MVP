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
  (void)files_;
  (void)clock_;
  (void)log_;
}

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
