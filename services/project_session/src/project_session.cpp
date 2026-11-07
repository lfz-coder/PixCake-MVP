#include "pixcake/project_session/project_session.hpp"

#include "pixcake/editdoc/edit_document.hpp"
#include "pixcake/storage/sqlite_project_store.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>

namespace pixcake::project_session {
namespace {

bool is_directory(const std::string& path) {
  struct stat info {};
  return ::stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

std::string base_name(std::string path) {
  while (path.size() > 1 && path.back() == '/') path.pop_back();
  const auto slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string lower_extension(const std::string& name) {
  const auto dot = name.find_last_of('.');
  if (dot == std::string::npos || dot == 0) return {};
  std::string ext = name.substr(dot + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext;
}

bool image_extension(const std::string& name) {
  static const char* const kExtensions[] = {"jpg", "jpeg", "png", "tif", "tiff", "bmp"};
  const std::string ext = lower_extension(name);
  return std::any_of(std::begin(kExtensions), std::end(kExtensions), [&](const char* e) { return ext == e; });
}

}  // namespace

std::vector<std::string> expand_import_paths(const std::vector<std::string>& paths) {
  std::vector<std::string> files;
  for (const auto& path : paths) {
    if (!is_directory(path)) {
      files.push_back(path);
      continue;
    }
    std::vector<std::string> inside;
    if (DIR* dir = ::opendir(path.c_str())) {
      while (const dirent* entry = ::readdir(dir)) {
        const std::string name = entry->d_name;
        if (name.empty() || name[0] == '.' || !image_extension(name)) continue;
        const std::string full = (path.back() == '/' ? path : path + "/") + name;
        if (!is_directory(full)) inside.push_back(full);
      }
      ::closedir(dir);
    }
    std::sort(inside.begin(), inside.end());
    files.insert(files.end(), inside.begin(), inside.end());
  }
  return files;
}

std::string project_name_for(const std::vector<std::string>& paths) {
  for (const auto& path : paths) {
    if (is_directory(path)) return base_name(path);
  }
  if (paths.empty()) return {};
  const std::string name = base_name(paths.front());
  const auto dot = name.find_last_of('.');
  return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}

ProjectSession::ProjectSession(const SessionState& session, SourceAccess& access, BookmarkMaker make_bookmark)
    : access_(access), make_bookmark_(make_bookmark), commands_(session) {}

ProjectSession::~ProjectSession() = default;

ResultCode ProjectSession::open(const std::string& data_directory, const SessionState& session, SourceAccess& access,
                                BookmarkMaker make_bookmark, std::unique_ptr<ProjectSession>& out,
                                std::string& diagnostic) {
  out.reset();
  if (data_directory.empty() || make_bookmark == nullptr) {
    diagnostic = "project data directory or bookmark maker missing";
    return ResultCode::StateConflict;
  }
  ::mkdir(data_directory.c_str(), 0700);
  if (!is_directory(data_directory)) {
    diagnostic = "project data directory cannot be created: " + data_directory;
    return ResultCode::IoFailed;
  }
  std::unique_ptr<ProjectSession> made(new ProjectSession(session, access, make_bookmark));
  std::unique_ptr<SqliteProjectStore> store;
  const ResultCode opened = SqliteProjectStore::open(data_directory + "/projects.sqlite", store);
  if (opened != ResultCode::Ok) {
    diagnostic = "project library cannot be opened: " + data_directory + "/projects.sqlite";
    return opened;
  }
  made->store_ = std::move(store);
  made->deleter_ = std::make_unique<FileSourceDeleter>(made->files_);
  made->library_ = std::make_unique<ProjectLibrary>(*made->store_, made->files_, made->clock_, *made->deleter_, nullptr);
  if (const ResultCode started = made->library_->startup(); started != ResultCode::Ok) {
    diagnostic = "project library startup failed";
    return started;
  }
  made->importer_ = std::make_unique<image_import::ImportCoordinator>(*made->library_, made->files_);
  made->document_directory_ = data_directory + "/documents";
  ::mkdir(made->document_directory_.c_str(), 0700);
  made->workspace_directory_ = data_directory + "/workspace";
  ::mkdir(made->workspace_directory_.c_str(), 0700);
  out = std::move(made);
  diagnostic.clear();
  return ResultCode::Ok;
}

std::string ProjectSession::document_id(std::int64_t photo_id) { return "photo-" + std::to_string(photo_id); }

ProjectSession::ImportOutcome ProjectSession::import_new_project(const std::vector<std::string>& paths) {
  ImportOutcome outcome;
  const auto files = expand_import_paths(paths);
  if (files.empty()) {
    outcome.code = ResultCode::UnsupportedFormat;
    outcome.notices.push_back("no image files were chosen");
    return outcome;
  }
  outcome.project_name = project_name_for(paths);
  const ProjectResult created = commands_.create_project(*library_, outcome.project_name, kOriginLocal, false);
  if (created.code != ResultCode::Ok) {
    outcome.code = created.code;
    if (!created.notice.empty()) outcome.notices.push_back(created.notice);
    return outcome;
  }
  return import_paths(created.id, files, std::move(outcome));
}

ProjectSession::ImportOutcome ProjectSession::import_into(std::int64_t project_id,
                                                          const std::vector<std::string>& paths) {
  return import_paths(project_id, expand_import_paths(paths), ImportOutcome{});
}

ProjectSession::ImportOutcome ProjectSession::import_paths(std::int64_t project_id,
                                                           const std::vector<std::string>& files,
                                                           ImportOutcome outcome) {
  outcome.project_id = project_id;
  std::vector<image_import::ImportCandidate> candidates;
  for (const auto& path : files) {
    image_import::ImportCandidate candidate;
    candidate.absolute_path = path;
    candidate.file_name = base_name(path);
    // No bookmark: the library refuses it with "permission denied" for this file.
    make_bookmark_(path, candidate.access_credential);
    candidates.push_back(std::move(candidate));
  }
  const auto batch = commands_.import_into(*importer_, project_id, candidates);
  outcome.code = batch.overall_code;
  outcome.accepted = batch.accepted_count;
  for (const auto& item : batch.items) {
    if (item.code == ResultCode::Ok && item.photo_id != 0) outcome.photo_ids.push_back(item.photo_id);
  }
  const auto notices = image_import::import_notices(batch);
  outcome.notices.insert(outcome.notices.end(), notices.begin(), notices.end());
  return outcome;
}

ResultCode ProjectSession::list_projects(std::vector<ProjectRow>& out) const {
  std::vector<ProjectRow> all;
  const ResultCode code = library_->list_projects(all);
  out.clear();
  for (auto& row : all) {
    if (!row.deleted_at) out.push_back(std::move(row));
  }
  return code;
}

namespace {

// 副本在条目列表里用原图的行，只换编号。
PhotoRow copy_row(const PhotoRow& source, std::int64_t copy_id) {
  PhotoRow row = source;
  row.id = -copy_id;
  return row;
}

}  // namespace

std::int64_t ProjectSession::source_photo(std::int64_t item) const {
  if (item >= 0) return item;
  std::optional<VirtualCopyRow> copy;
  if (library_->load_virtual_copy(-item, copy) != ResultCode::Ok || !copy) return 0;
  return copy->source_photo_id;
}

std::int64_t ProjectSession::copy_sequence(std::int64_t item) const {
  if (item >= 0) return 0;
  std::optional<VirtualCopyRow> copy;
  if (library_->load_virtual_copy(-item, copy) != ResultCode::Ok || !copy) return 0;
  return copy->sequence;
}

std::string ProjectSession::document_name(std::int64_t item) const {
  if (item >= 0) return document_id(item);
  std::optional<VirtualCopyRow> copy;
  if (library_->load_virtual_copy(-item, copy) != ResultCode::Ok || !copy) return "copy-" + std::to_string(-item);
  return copy->document_id;
}

std::string ProjectSession::image_id(std::int64_t item) const { return document_id(source_photo(item)); }

ResultCode ProjectSession::create_virtual_copy(std::int64_t item, std::int64_t& new_item, std::string& diagnostic) {
  new_item = 0;
  const std::int64_t source = source_photo(item);
  std::vector<VirtualCopyRow> copies;
  if (source == 0 || library_->list_virtual_copies(source, copies) != ResultCode::Ok) {
    diagnostic = "photo is not in the project library";
    return ResultCode::StateConflict;
  }
  const std::int64_t sequence = copies.empty() ? 1 : copies.back().sequence + 1;
  const std::string name = document_id(source) + "-copy-" + std::to_string(sequence);
  const std::string from = document_directory_ + "/" + document_name(item) + ".pxedit";
  if (files_.exists(from)) {
    auto copy = EditDocument::create(image_id(item));
    const LoadResult copied = EditDocument::copy_committed(files_, from, document_directory_ + "/" + name + ".pxedit",
                                                           phase_one_version_limits(), *copy);
    if (!copied.code || *copied.code != ResultCode::Ok) {
      diagnostic = "the edit document could not be copied";
      return copied.code ? *copied.code : ResultCode::IoFailed;
    }
  }
  const ProjectResult created = commands_.create_virtual_copy(*library_, source, name);
  if (created.code != ResultCode::Ok) {
    diagnostic = created.notice;
    return created.code;
  }
  new_item = -created.id;
  diagnostic.clear();
  return ResultCode::Ok;
}

ResultCode ProjectSession::list_photos(std::int64_t project_id, std::vector<PhotoRow>& out) const {
  std::vector<PhotoRow> photos;
  const ResultCode code = library_->list_photos(project_id, photos);
  out.clear();
  for (auto& photo : photos) {
    const PhotoRow original = photo;
    out.push_back(std::move(photo));
    // 副本跟在原图右边，按序号；进了回收站的不显示。
    std::vector<VirtualCopyRow> copies;
    if (library_->list_virtual_copies(original.id, copies) != ResultCode::Ok) continue;
    for (const auto& copy : copies) {
      std::optional<TrashRow> trashed;
      if (library_->load_trash(kTrashVirtualCopy, copy.id, trashed) == ResultCode::Ok && trashed) continue;
      out.push_back(copy_row(original, copy.id));
    }
  }
  return code;
}

ResultCode ProjectSession::load_photo(std::int64_t photo_id, std::optional<PhotoRow>& out) const {
  if (photo_id >= 0) return library_->load_photo(photo_id, out);
  const std::int64_t source = source_photo(photo_id);
  const ResultCode code = library_->load_photo(source, out);
  if (code == ResultCode::Ok && out) out = copy_row(*out, -photo_id);
  return code;
}

ResultCode ProjectSession::resolve_photo(std::int64_t item, std::string& absolute_path, std::string& diagnostic) {
  absolute_path.clear();
  const std::int64_t photo_id = source_photo(item);
  std::optional<PhotoRow> photo;
  if (library_->load_photo(photo_id, photo) != ResultCode::Ok || !photo) {
    diagnostic = "photo is not in the project library";
    return ResultCode::StateConflict;
  }
  const ResolvedSource resolved = access_.resolve(photo->access_credential);
  if (resolved.code == ResultCode::Ok && resolved.absolute_path == photo->absolute_path) {
    absolute_path = resolved.absolute_path;
    diagnostic.clear();
    return ResultCode::Ok;
  }
  // Moved, renamed, deleted or no longer granted: record it the library's way
  // (presence "missing", path and edits kept) and refuse to open.
  library_->refresh_source_access(access_);
  diagnostic = (resolved.code == ResultCode::PermissionDenied ? "no permission to read the original: "
                                                               : "the original is missing: ") +
               photo->absolute_path;
  return resolved.code == ResultCode::PermissionDenied ? ResultCode::PermissionDenied : ResultCode::SourceMissing;
}

ResultCode ProjectSession::mark_edited(std::int64_t photo_id) {
  return library_->mark_photo_edited(source_photo(photo_id), clock_.unix_seconds()).code;
}

ResultCode ProjectSession::document_bytes(std::int64_t photo_id, std::vector<std::uint8_t>& out) {
  out.clear();
  const std::string path = document_directory_ + "/" + document_name(photo_id) + ".pxedit";
  if (!files_.exists(path)) return ResultCode::Ok;
  auto document = EditDocument::create(image_id(photo_id));
  // load() falls back to the backup and reports a document recovered empty.
  const LoadResult loaded = EditDocument::load(*document, files_, path, phase_one_version_limits());
  if (loaded.code && *loaded.code != ResultCode::Ok) return *loaded.code;
  out = document->encode();
  return ResultCode::Ok;
}

// One line: "library <photo id>" or "refine <photo id>"; the id is 0 for none.
ResultCode ProjectSession::remember_place(std::int64_t project_id, const ProjectPlace& place) {
  const std::string path = workspace_directory_ + "/project-" + std::to_string(project_id);
  const std::string line = std::string(place.workspace == Workspace::Library ? "library" : "refine") + " " +
                           std::to_string(place.photo_id.value_or(0)) + "\n";
  const std::string temporary = path + ".tmp";
  files_.remove_file(temporary);
  const IoResult written = files_.write_new(temporary, std::vector<std::uint8_t>(line.begin(), line.end()));
  if (!written.ok) return written.code;
  const IoResult renamed = files_.rename_file(temporary, path);
  return renamed.ok ? ResultCode::Ok : renamed.code;
}

ProjectPlace ProjectSession::last_place(std::int64_t project_id) {
  ProjectPlace place;
  std::vector<std::uint8_t> bytes;
  if (!files_.read_file(workspace_directory_ + "/project-" + std::to_string(project_id), bytes).ok) return place;
  const std::string line(bytes.begin(), bytes.end());
  const auto space = line.find(' ');
  const std::string workspace = line.substr(0, space);
  if (workspace == "library") {
    place.workspace = Workspace::Library;
  } else if (workspace != "refine") {
    return place;
  }
  std::int64_t photo = 0;
  if (space != std::string::npos) photo = std::strtoll(line.c_str() + space + 1, nullptr, 10);
  std::vector<PhotoRow> rows;
  if (photo > 0 && list_photos(project_id, rows) == ResultCode::Ok) {
    for (const auto& row : rows) {
      if (row.id == photo) place.photo_id = photo;
    }
  }
  return place;
}

}  // namespace pixcake::project_session
