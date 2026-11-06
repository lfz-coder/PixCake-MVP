#include "pixcake/project/project_library.hpp"

namespace pixcake {
namespace {

bool valid_origin(const std::string& origin) {
  return origin == kOriginLocal || origin == kOriginCloudUploaded || origin == kOriginCloudDownloaded;
}

bool absolute_posix_path(const std::string& path) {
  return !path.empty() && path.front() == '/' && path.find('\0') == std::string::npos;
}

bool allowed_flag(const std::string& flag) {
  return flag.empty() || flag == kFlagPick || flag == kFlagExclude;
}

bool allowed_color(const std::string& color) {
  return color.empty() || color == kColorRed || color == kColorYellow || color == kColorGreen ||
         color == kColorBlue || color == kColorPurple;
}

ProjectResult write_marks_batch(ProjectStore& store, const std::vector<std::int64_t>& photo_ids,
                                const std::function<ResultCode(const std::vector<std::int64_t>&)>& update) {
  ProjectResult result;
  if (photo_ids.empty()) { result.code = ResultCode::StateConflict; return result; }
  result.code = store.in_transaction([&] {
    std::vector<PhotoRow> rows;
    const ResultCode listed = store.list_photos(std::nullopt, rows);
    if (listed != ResultCode::Ok) return listed;
    for (const auto id : photo_ids) {
      bool found = false;
      for (const auto& row : rows) if (row.id == id) { found = true; break; }
      if (!found) return ResultCode::StateConflict;
    }
    return update(photo_ids);
  });
  return result;
}

ResultCode original_photo_id(ProjectStore& store, const std::string& kind, std::int64_t id,
                             std::int64_t& original) {
  if (kind == kTrashPhoto) {
    std::optional<PhotoRow> photo;
    const ResultCode loaded = store.load_photo(id, photo);
    if (loaded != ResultCode::Ok) {
      return loaded;
    }
    if (!photo.has_value()) {
      return ResultCode::StateConflict;
    }
    original = photo->id;
    return ResultCode::Ok;
  }
  if (kind == kTrashVirtualCopy) {
    std::optional<VirtualCopyRow> copy;
    const ResultCode loaded = store.load_virtual_copy(id, copy);
    if (loaded != ResultCode::Ok) {
      return loaded;
    }
    if (!copy.has_value()) {
      return ResultCode::StateConflict;
    }
    original = copy->source_photo_id;
    return ResultCode::Ok;
  }
  return ResultCode::StateConflict;
}

ResultCode trash_new_copies(ProjectStore& store, std::int64_t photo_id, std::int64_t now) {
  std::vector<VirtualCopyRow> copies;
  const ResultCode listed = store.list_virtual_copies(photo_id, copies);
  if (listed != ResultCode::Ok) {
    return listed;
  }
  for (const VirtualCopyRow& copy : copies) {
    std::optional<TrashRow> existing;
    const ResultCode found = store.load_trash(kTrashVirtualCopy, copy.id, existing);
    if (found != ResultCode::Ok) {
      return found;
    }
    if (existing.has_value()) {
      continue;
    }
    const ResultCode inserted = store.insert_trash(kTrashVirtualCopy, copy.id, now);
    if (inserted != ResultCode::Ok) {
      return inserted;
    }
  }
  return ResultCode::Ok;
}

ProjectResult failed(ResultCode code) {
  ProjectResult result;
  result.code = code;
  return result;
}

}  // namespace

ProjectLibrary::ProjectLibrary(ProjectStore& store, FileSystem& files, const Clock& clock, SourceDeleter& deleter,
                               LogSink* log)
    : store_(store), files_(files), clock_(clock), deleter_(deleter), log_(log) {}

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

bool ProjectLibrary::trash_expired(std::int64_t deleted_at, std::int64_t now) const {
  if (deleted_at > now) {
    return false;
  }
  return now - deleted_at >= kTrashRetentionSeconds;
}

ResultCode ProjectLibrary::purge_expired_trash() {
  std::vector<TrashRow> rows;
  const ResultCode listed = store_.list_trash(rows);
  if (listed != ResultCode::Ok) {
    return listed;
  }
  const std::int64_t now = clock_.unix_seconds();
  std::vector<std::int64_t> projects;
  std::vector<std::int64_t> photos;
  std::vector<std::int64_t> copies;
  for (const TrashRow& row : rows) {
    if (!trash_expired(row.deleted_at, now)) {
      continue;
    }
    if (row.subject == kTrashProject) {
      projects.push_back(row.subject_id);
    } else if (row.subject == kTrashPhoto) {
      photos.push_back(row.subject_id);
    } else if (row.subject == kTrashVirtualCopy) {
      copies.push_back(row.subject_id);
    } else {
      return ResultCode::StateConflict;
    }
  }
  return store_.in_transaction([this, &projects, &photos, &copies] {
    for (const std::int64_t project_id : projects) {
      const ResultCode erased = store_.erase_project_index(project_id);
      if (erased != ResultCode::Ok) {
        return erased;
      }
    }
    for (const std::int64_t photo_id : photos) {
      std::optional<PhotoRow> photo;
      const ResultCode loaded = store_.load_photo(photo_id, photo);
      if (loaded != ResultCode::Ok) {
        return loaded;
      }
      const ResultCode erased_copies = store_.erase_virtual_copies_of_photo(photo_id);
      if (erased_copies != ResultCode::Ok) {
        return erased_copies;
      }
      const ResultCode erased_trash = store_.erase_trash(kTrashPhoto, photo_id);
      if (erased_trash != ResultCode::Ok) {
        return erased_trash;
      }
      if (!photo.has_value()) {
        continue;
      }
      const ResultCode erased = store_.erase_photo(photo_id);
      if (erased != ResultCode::Ok) {
        return erased;
      }
    }
    for (const std::int64_t copy_id : copies) {
      const ResultCode erased_trash = store_.erase_trash(kTrashVirtualCopy, copy_id);
      if (erased_trash != ResultCode::Ok) {
        return erased_trash;
      }
      const ResultCode erased = store_.erase_virtual_copy(copy_id);
      if (erased != ResultCode::Ok) {
        return erased;
      }
    }
    return ResultCode::Ok;
  });
}

ProjectResult ProjectLibrary::trash_project(std::int64_t project_id) {
  std::optional<ProjectRow> project;
  const ResultCode loaded = store_.load_project(project_id, project);
  if (loaded != ResultCode::Ok) {
    return failed(loaded);
  }
  if (!project.has_value() || project->deleted_at.has_value()) {
    return failed(ResultCode::StateConflict);
  }
  std::optional<TrashRow> existing;
  const ResultCode trash = store_.load_trash(kTrashProject, project_id, existing);
  if (trash != ResultCode::Ok) {
    return failed(trash);
  }
  if (existing.has_value()) {
    return failed(ResultCode::StateConflict);
  }
  std::vector<PhotoRow> photos;
  const ResultCode listed = store_.list_photos(project_id, photos);
  if (listed != ResultCode::Ok) {
    return failed(listed);
  }
  const std::int64_t now = clock_.unix_seconds();
  const ResultCode wrote = store_.in_transaction([this, project_id, now, &photos] {
    const ResultCode stamped = store_.update_project_deleted_at(project_id, now);
    if (stamped != ResultCode::Ok) {
      return stamped;
    }
    const ResultCode inserted = store_.insert_trash(kTrashProject, project_id, now);
    if (inserted != ResultCode::Ok) {
      return inserted;
    }
    for (const PhotoRow& photo : photos) {
      std::optional<TrashRow> photo_trash;
      const ResultCode found = store_.load_trash(kTrashPhoto, photo.id, photo_trash);
      if (found != ResultCode::Ok) {
        return found;
      }
      if (!photo_trash.has_value()) {
        const ResultCode photo_inserted = store_.insert_trash(kTrashPhoto, photo.id, now);
        if (photo_inserted != ResultCode::Ok) {
          return photo_inserted;
        }
      }
      const ResultCode copies = trash_new_copies(store_, photo.id, now);
      if (copies != ResultCode::Ok) {
        return copies;
      }
    }
    return ResultCode::Ok;
  });
  return failed(wrote);
}

ProjectResult ProjectLibrary::trash_photo(std::int64_t photo_id) {
  std::optional<PhotoRow> photo;
  const ResultCode loaded = store_.load_photo(photo_id, photo);
  if (loaded != ResultCode::Ok) {
    return failed(loaded);
  }
  if (!photo.has_value()) {
    return failed(ResultCode::StateConflict);
  }
  std::optional<TrashRow> existing;
  const ResultCode trash = store_.load_trash(kTrashPhoto, photo_id, existing);
  if (trash != ResultCode::Ok) {
    return failed(trash);
  }
  if (existing.has_value()) {
    return failed(ResultCode::StateConflict);
  }
  const std::int64_t now = clock_.unix_seconds();
  const ResultCode wrote = store_.in_transaction([this, photo_id, now] {
    const ResultCode inserted = store_.insert_trash(kTrashPhoto, photo_id, now);
    if (inserted != ResultCode::Ok) {
      return inserted;
    }
    return trash_new_copies(store_, photo_id, now);
  });
  return failed(wrote);
}

ProjectResult ProjectLibrary::trash_virtual_copy(std::int64_t copy_id) {
  std::optional<VirtualCopyRow> copy;
  const ResultCode loaded = store_.load_virtual_copy(copy_id, copy);
  if (loaded != ResultCode::Ok) {
    return failed(loaded);
  }
  if (!copy.has_value()) {
    return failed(ResultCode::StateConflict);
  }
  std::optional<TrashRow> existing;
  const ResultCode trash = store_.load_trash(kTrashVirtualCopy, copy_id, existing);
  if (trash != ResultCode::Ok) {
    return failed(trash);
  }
  if (existing.has_value()) {
    return failed(ResultCode::StateConflict);
  }
  const std::int64_t now = clock_.unix_seconds();
  return failed(store_.insert_trash(kTrashVirtualCopy, copy_id, now));
}

ProjectResult ProjectLibrary::restore_project(std::int64_t project_id) {
  std::optional<TrashRow> existing;
  const ResultCode trash = store_.load_trash(kTrashProject, project_id, existing);
  if (trash != ResultCode::Ok) {
    return failed(trash);
  }
  if (!existing.has_value()) {
    return failed(ResultCode::StateConflict);
  }
  std::vector<PhotoRow> photos;
  const ResultCode listed = store_.list_photos(project_id, photos);
  if (listed != ResultCode::Ok) {
    return failed(listed);
  }
  const ResultCode wrote = store_.in_transaction([this, project_id, &photos] {
    const ResultCode cleared = store_.update_project_deleted_at(project_id, std::nullopt);
    if (cleared != ResultCode::Ok) {
      return cleared;
    }
    const ResultCode erased = store_.erase_trash(kTrashProject, project_id);
    if (erased != ResultCode::Ok) {
      return erased;
    }
    for (const PhotoRow& photo : photos) {
      const ResultCode photo_erased = store_.erase_trash(kTrashPhoto, photo.id);
      if (photo_erased != ResultCode::Ok) {
        return photo_erased;
      }
    }
    return ResultCode::Ok;
  });
  return failed(wrote);
}

ProjectResult ProjectLibrary::restore_photos(std::vector<std::int64_t> photo_ids) {
  if (photo_ids.empty()) {
    return failed(ResultCode::StateConflict);
  }
  for (const std::int64_t photo_id : photo_ids) {
    std::optional<PhotoRow> photo;
    const ResultCode loaded = store_.load_photo(photo_id, photo);
    if (loaded != ResultCode::Ok) {
      return failed(loaded);
    }
    if (!photo.has_value()) {
      return failed(ResultCode::StateConflict);
    }
    std::optional<TrashRow> photo_trash;
    const ResultCode trash = store_.load_trash(kTrashPhoto, photo_id, photo_trash);
    if (trash != ResultCode::Ok) {
      return failed(trash);
    }
    if (!photo_trash.has_value()) {
      return failed(ResultCode::StateConflict);
    }
    std::optional<TrashRow> project_trash;
    const ResultCode project = store_.load_trash(kTrashProject, photo->project_id, project_trash);
    if (project != ResultCode::Ok) {
      return failed(project);
    }
    if (project_trash.has_value()) {
      return failed(ResultCode::StateConflict);
    }
  }
  const ResultCode wrote = store_.in_transaction([this, &photo_ids] {
    for (const std::int64_t photo_id : photo_ids) {
      const ResultCode erased = store_.erase_trash(kTrashPhoto, photo_id);
      if (erased != ResultCode::Ok) {
        return erased;
      }
    }
    return ResultCode::Ok;
  });
  return failed(wrote);
}

ProjectResult ProjectLibrary::restore_virtual_copies(std::vector<std::int64_t> copy_ids) {
  if (copy_ids.empty()) {
    return failed(ResultCode::StateConflict);
  }
  std::vector<std::int64_t> photos_in_trash;
  for (const std::int64_t copy_id : copy_ids) {
    std::optional<VirtualCopyRow> copy;
    const ResultCode loaded = store_.load_virtual_copy(copy_id, copy);
    if (loaded != ResultCode::Ok) {
      return failed(loaded);
    }
    if (!copy.has_value()) {
      return failed(ResultCode::StateConflict);
    }
    std::optional<TrashRow> copy_trash;
    const ResultCode trash = store_.load_trash(kTrashVirtualCopy, copy_id, copy_trash);
    if (trash != ResultCode::Ok) {
      return failed(trash);
    }
    if (!copy_trash.has_value()) {
      return failed(ResultCode::StateConflict);
    }
    std::optional<TrashRow> project_trash;
    const ResultCode project = store_.load_trash(kTrashProject, copy->project_id, project_trash);
    if (project != ResultCode::Ok) {
      return failed(project);
    }
    if (project_trash.has_value()) {
      return failed(ResultCode::StateConflict);
    }
    std::optional<PhotoRow> photo;
    const ResultCode photo_loaded = store_.load_photo(copy->source_photo_id, photo);
    if (photo_loaded != ResultCode::Ok) {
      return failed(photo_loaded);
    }
    if (!photo.has_value()) {
      return failed(ResultCode::StateConflict);
    }
    std::optional<TrashRow> photo_trash;
    const ResultCode photo_trash_code = store_.load_trash(kTrashPhoto, copy->source_photo_id, photo_trash);
    if (photo_trash_code != ResultCode::Ok) {
      return failed(photo_trash_code);
    }
    if (photo_trash.has_value()) {
      bool seen = false;
      for (const std::int64_t existing : photos_in_trash) {
        if (existing == copy->source_photo_id) {
          seen = true;
          break;
        }
      }
      if (!seen) {
        photos_in_trash.push_back(copy->source_photo_id);
      }
    }
  }
  const ResultCode wrote = store_.in_transaction([this, &copy_ids, &photos_in_trash] {
    for (const std::int64_t copy_id : copy_ids) {
      const ResultCode erased = store_.erase_trash(kTrashVirtualCopy, copy_id);
      if (erased != ResultCode::Ok) {
        return erased;
      }
    }
    for (const std::int64_t photo_id : photos_in_trash) {
      const ResultCode erased = store_.erase_trash(kTrashPhoto, photo_id);
      if (erased != ResultCode::Ok) {
        return erased;
      }
    }
    return ResultCode::Ok;
  });
  return failed(wrote);
}

ProjectResult ProjectLibrary::erase_trashed_project(std::int64_t project_id) {
  std::optional<TrashRow> existing;
  const ResultCode trash = store_.load_trash(kTrashProject, project_id, existing);
  if (trash != ResultCode::Ok) {
    return failed(trash);
  }
  if (!existing.has_value()) {
    return failed(ResultCode::StateConflict);
  }
  return failed(store_.erase_project_index(project_id));
}

ProjectResult ProjectLibrary::delete_source_file(std::int64_t photo_id) {
  std::optional<PhotoRow> photo;
  const ResultCode loaded = store_.load_photo(photo_id, photo);
  if (loaded != ResultCode::Ok) {
    return failed(loaded);
  }
  if (!photo.has_value()) {
    return failed(ResultCode::StateConflict);
  }
  std::optional<TrashRow> existing;
  const ResultCode trash = store_.load_trash(kTrashPhoto, photo_id, existing);
  if (trash != ResultCode::Ok) {
    return failed(trash);
  }
  const std::int64_t now = clock_.unix_seconds();
  const bool already_trashed = existing.has_value();

  {
    const ResultCode intent = store_.in_transaction([this, photo_id, now, already_trashed] {
      if (!already_trashed) {
        const ResultCode inserted = store_.insert_trash(kTrashPhoto, photo_id, now);
        if (inserted != ResultCode::Ok) return inserted;
      }
      const ResultCode pending = store_.update_photo_presence(photo_id, kPresenceDeletePending);
      if (pending != ResultCode::Ok) return pending;
      return already_trashed ? ResultCode::Ok : trash_new_copies(store_, photo_id, now);
    });
    if (intent != ResultCode::Ok) return failed(intent);
  }

  const ResultCode removed = deleter_.remove_source(photo->absolute_path, photo->source_identity);

  if (removed != ResultCode::Ok && removed != ResultCode::PermissionDenied &&
      removed != ResultCode::SourceMissing) {
    return failed(removed);
  }

  const ResultCode wrote = store_.in_transaction([this, photo_id, already_trashed, removed] {
    if (removed == ResultCode::Ok || removed == ResultCode::SourceMissing) {
      const ResultCode presence = store_.update_photo_presence(photo_id, kPresenceMissing);
      if (presence != ResultCode::Ok) {
        return presence;
      }
    }
    if (!already_trashed) {
      return ResultCode::Ok;
    }
    return ResultCode::Ok;
  });
  if (wrote != ResultCode::Ok) {
    return failed(wrote);
  }
  ProjectResult result;
  result.code = removed;
  if (removed == ResultCode::PermissionDenied) {
    result.notice = kDiskFileRemains;
  }
  return result;
}

ResultCode ProjectLibrary::load_project(std::int64_t id, std::optional<ProjectRow>& out) const {
  return store_.load_project(id, out);
}

ResultCode ProjectLibrary::load_trash(const std::string& subject, std::int64_t subject_id,
                                      std::optional<TrashRow>& out) const {
  return store_.load_trash(subject, subject_id, out);
}

ResultCode ProjectLibrary::list_trash(std::vector<TrashRow>& out) const { return store_.list_trash(out); }

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

ProjectResult ProjectLibrary::add_photo(std::int64_t project_id, const AdmittedSource& source,
                                        std::string file_name, std::vector<std::uint8_t> access_credential) {
  if (!absolute_posix_path(source.absolute_path()) || file_name.empty() || !source.identity().valid) {
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
  draft.absolute_path = source.absolute_path();
  draft.file_name = std::move(file_name);
  draft.access_credential = std::move(access_credential);
  draft.presence = kPresenceEditable;
  const IoResult identity = files_.identify(draft.absolute_path, draft.source_identity);
  if (identity.not_found) {
    return failed(ResultCode::SourceMissing);
  }
  if (!identity.ok) {
    return failed(identity.code);
  }
  const FileIdentity& admitted = source.identity();
  if (!draft.source_identity.valid || draft.source_identity.device != admitted.device ||
      draft.source_identity.inode != admitted.inode || draft.source_identity.size != admitted.size) {
    return failed(ResultCode::StateConflict);
  }
  ProjectResult result;
  result.code = store_.insert_photo(draft, result.id);
  return result;
}

ProjectResult ProjectLibrary::create_virtual_copy(std::int64_t photo_id, std::string document_id) {
  if (document_id.empty()) {
    return failed(ResultCode::StateConflict);
  }
  std::optional<PhotoRow> photo;
  const ResultCode loaded = store_.load_photo(photo_id, photo);
  if (loaded != ResultCode::Ok) {
    return failed(loaded);
  }
  if (!photo.has_value()) {
    return failed(ResultCode::StateConflict);
  }
  std::optional<TrashRow> photo_trash;
  const ResultCode photo_trash_code = store_.load_trash(kTrashPhoto, photo_id, photo_trash);
  if (photo_trash_code != ResultCode::Ok) {
    return failed(photo_trash_code);
  }
  if (photo_trash.has_value()) {
    return failed(ResultCode::StateConflict);
  }
  std::optional<TrashRow> project_trash;
  const ResultCode project_trash_code = store_.load_trash(kTrashProject, photo->project_id, project_trash);
  if (project_trash_code != ResultCode::Ok) {
    return failed(project_trash_code);
  }
  if (project_trash.has_value()) {
    return failed(ResultCode::StateConflict);
  }
  std::optional<VirtualCopyRow> existing_document;
  const ResultCode found_document = store_.load_virtual_copy_by_document(document_id, existing_document);
  if (found_document != ResultCode::Ok) {
    return failed(found_document);
  }
  if (existing_document.has_value()) {
    return failed(ResultCode::StateConflict);
  }

  std::vector<VirtualCopyRow> copies;
  const ResultCode listed = store_.list_virtual_copies(photo_id, copies);
  if (listed != ResultCode::Ok) {
    return failed(listed);
  }
  VirtualCopyDraft draft;
  draft.project_id = photo->project_id;
  draft.source_photo_id = photo_id;
  draft.sequence = copies.empty() ? 1 : copies.back().sequence + 1;
  draft.document_id = std::move(document_id);
  ProjectResult result;
  result.code = store_.insert_virtual_copy(draft, result.id);
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

ResultCode ProjectLibrary::load_virtual_copy(std::int64_t id, std::optional<VirtualCopyRow>& out) const {
  return store_.load_virtual_copy(id, out);
}

ResultCode ProjectLibrary::list_virtual_copies(std::int64_t photo_id, std::vector<VirtualCopyRow>& out) const {
  return store_.list_virtual_copies(photo_id, out);
}

ProjectResult ProjectLibrary::set_photo_ratings(std::vector<std::int64_t> photo_ids, int rating) {
  if (rating < kRatingMin || rating > kRatingMax) {
    ProjectResult result;
    result.code = ResultCode::StateConflict;
    return result;
  }
  return write_marks_batch(store_, photo_ids, [this, rating](const std::vector<std::int64_t>& ids) {
    return store_.update_photo_ratings(ids, rating);
  });
}

ProjectResult ProjectLibrary::set_photo_flags(std::vector<std::int64_t> photo_ids, std::string flag) {
  if (!allowed_flag(flag)) {
    ProjectResult result;
    result.code = ResultCode::StateConflict;
    return result;
  }
  return write_marks_batch(store_, photo_ids, [this, flag = std::move(flag)](const std::vector<std::int64_t>& ids) {
    return store_.update_photo_flags(ids, flag);
  });
}

ProjectResult ProjectLibrary::set_photo_colors(std::vector<std::int64_t> photo_ids, std::string color) {
  if (!allowed_color(color)) {
    ProjectResult result;
    result.code = ResultCode::StateConflict;
    return result;
  }
  return write_marks_batch(store_, photo_ids, [this, color = std::move(color)](const std::vector<std::int64_t>& ids) {
    return store_.update_photo_colors(ids, color);
  });
}

ResultCode ProjectLibrary::shares_original(const std::string& left_kind, std::int64_t left_id,
                                           const std::string& right_kind, std::int64_t right_id,
                                           bool& shares) const {
  shares = false;
  std::int64_t left_original = 0;
  const ResultCode left = original_photo_id(store_, left_kind, left_id, left_original);
  if (left != ResultCode::Ok) {
    return left;
  }
  std::int64_t right_original = 0;
  const ResultCode right = original_photo_id(store_, right_kind, right_id, right_original);
  if (right != ResultCode::Ok) {
    return right;
  }
  shares = left_original == right_original;
  return ResultCode::Ok;
}

}  // namespace pixcake
