#pragma once

#include "pixcake/command/business_commands.hpp"
#include "pixcake/image_import/import_coordinator.hpp"
#include "pixcake/persist/file_system.hpp"
#include "pixcake/project/project_library.hpp"
#include "pixcake/project/source_access.hpp"
#include "pixcake/project/source_deleter.hpp"
#include "pixcake/support/clock.hpp"
#include "pixcake/support/session_state.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pixcake::project_session {

// What the shell needs from the project library for the main flow
// (requirement 5.2/5.3): import photos into a new project, list projects and
// photos, resolve a photo's original through its bookmark (S-10), and mark it
// edited.  Project changes go through BusinessCommands (architecture 6.2), so
// every change is refused while signed out (S-25).  No Qt.
// Where a project was left (window state, not a project record): opening it
// again from home returns to that workspace and photo, as 像素蛋糕 does
// (evidence 2026-11-06-project-entry-observation.md).
enum class Workspace { Library, Refine };
struct ProjectPlace {
  Workspace workspace = Workspace::Refine;  // requirement 5.3: a project opens in refine by default
  std::optional<std::int64_t> photo_id;
};

class ProjectSession {
 public:
  struct ImportOutcome {
    ResultCode code = ResultCode::Ok;
    std::int64_t project_id = 0;
    std::string project_name;
    std::size_t accepted = 0;
    std::vector<std::int64_t> photo_ids;  // accepted photos, in import order
    std::vector<std::string> notices;     // "file name: reason" per refused file (O-14)
  };

  // Opens (creating if needed) <data_directory>/projects.sqlite.  `access`
  // creates and resolves bookmarks; `make_bookmark` turns a chosen path into
  // a credential (platform/macos in the app, a stub in tests).
  using BookmarkMaker = ResultCode (*)(const std::string& absolute_path, std::vector<std::uint8_t>& out);
  static ResultCode open(const std::string& data_directory, const SessionState& session, SourceAccess& access,
                         BookmarkMaker make_bookmark, std::unique_ptr<ProjectSession>& out, std::string& diagnostic);
  ~ProjectSession();

  // The project name is the first chosen folder's name, or the first image's
  // name without extension (requirement 5.2).  Folders contribute their image
  // files (not recursive, hidden files skipped), sorted by name.
  ImportOutcome import_new_project(const std::vector<std::string>& paths);
  // Adds photos to an existing project (dropping onto the refine page).
  ImportOutcome import_into(std::int64_t project_id, const std::vector<std::string>& paths);

  ResultCode list_projects(std::vector<ProjectRow>& out) const;
  ResultCode list_photos(std::int64_t project_id, std::vector<PhotoRow>& out) const;
  ResultCode load_photo(std::int64_t photo_id, std::optional<PhotoRow>& out) const;
  // The original's current path through its bookmark.  A moved or deleted
  // original is SourceMissing and the photo is marked missing (its edits stay).
  ResultCode resolve_photo(std::int64_t photo_id, std::string& absolute_path, std::string& diagnostic);
  ResultCode mark_edited(std::int64_t photo_id);
  // The photo's committed edit document as EditDocument::encode() bytes (for
  // thumbnails); empty when the photo was never edited.
  ResultCode document_bytes(std::int64_t photo_id, std::vector<std::uint8_t>& out);

  // Remembered per project in <data_directory>/workspace/project-<id>.  A
  // missing or unreadable record is the default place; a photo that left the
  // project is dropped when read.
  ResultCode remember_place(std::int64_t project_id, const ProjectPlace& place);
  ProjectPlace last_place(std::int64_t project_id);

  // Edit documents live here, one per photo, named by document_id().
  const std::string& document_directory() const { return document_directory_; }
  static std::string document_id(std::int64_t photo_id);

 private:
  ProjectSession(const SessionState& session, SourceAccess& access, BookmarkMaker make_bookmark);
  ImportOutcome import_paths(std::int64_t project_id, const std::vector<std::string>& paths, ImportOutcome outcome);

  SourceAccess& access_;
  BookmarkMaker make_bookmark_;
  PosixFileSystem files_;
  SystemClock clock_;
  std::unique_ptr<FileSourceDeleter> deleter_;
  std::unique_ptr<ProjectStore> store_;
  std::unique_ptr<ProjectLibrary> library_;
  std::unique_ptr<image_import::ImportCoordinator> importer_;
  BusinessCommands commands_;
  std::string document_directory_;
  std::string workspace_directory_;
};

// Expands chosen paths into image files: files as given, folders to their
// image files by extension (jpg, jpeg, png, tif, tiff, bmp), sorted by name.
std::vector<std::string> expand_import_paths(const std::vector<std::string>& paths);
// Requirement 5.2 naming: first folder's name, else first file's stem.
std::string project_name_for(const std::vector<std::string>& paths);

}  // namespace pixcake::project_session
