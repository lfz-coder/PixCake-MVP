#pragma once

#include "pixcake/command/business_commands.hpp"
#include "pixcake/sync/sync_batch.hpp"
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

  // 虚拟副本（需求 §7）：编号用负数，-副本号；原图是正数的照片编号。照片编号能出现的地方都能用副本编号：
  // 列表、精修打开、缩略图、导出。副本有自己的编辑文档，原图文件和原图的图片标识不变（同一张图）。
  static bool is_copy(std::int64_t item) { return item < 0; }
  // 副本的原图编号；原图自己返回自己。
  std::int64_t source_photo(std::int64_t item) const;
  // 副本在原图里的序号（从 1 递增）；原图为 0。
  std::int64_t copy_sequence(std::int64_t item) const;
  // 这个条目的编辑文档文件名（不含 .pxedit）和文档里的图片标识（副本沿用原图的）。
  std::string document_name(std::int64_t item) const;
  std::string image_id(std::int64_t item) const;
  // 复制原图或副本当前已提交的调整，新副本排在原图的副本最后；new_item 是新副本的编号。
  ResultCode create_virtual_copy(std::int64_t item, std::int64_t& new_item, std::string& diagnostic);

  // 复制、粘贴效果和批量同步（需求 §18）：读写的是已提交的编辑文档（不是正在精修的内存预览）。
  // 每个目标一条结果；成功的目标记成已编辑。未登录返回状态冲突。
  struct EffectOutcome {
    std::int64_t item = 0;
    ResultCode code = ResultCode::Ok;
  };
  ResultCode copy_effect(std::int64_t item, EffectClipboard& out);
  ResultCode paste_effect(const EffectClipboard& clipboard, const std::vector<std::int64_t>& targets,
                          std::vector<EffectOutcome>& outcomes);
  // 把 source 里勾选的一级、二级功能（sync_tree 的编号）同步到各目标；来源不能出现在目标里。
  ResultCode sync_effect(std::int64_t source, const std::vector<std::string>& checked_ids,
                         const std::vector<std::int64_t>& targets, std::vector<EffectOutcome>& outcomes);

  // 标记（需求 §6.1）：星级 0–5、旗标（选取/排除/空）、色标（红黄绿蓝紫/空）。条目编号，副本记在原图上；
  // 同一张原图只写一次。
  ResultCode set_rating(const std::vector<std::int64_t>& items, int rating);
  ResultCode set_flag(const std::vector<std::int64_t>& items, const std::string& flag);
  ResultCode set_color(const std::vector<std::int64_t>& items, const std::string& color);

  // 删除与回收站（需求 §7）：删除只进回收站，保留 30 天；删原图时它的副本一起进去；恢复任一副本时原图一并恢复。
  struct TrashEntry {
    std::string subject;          // kTrashProject / kTrashPhoto / kTrashVirtualCopy
    std::int64_t id = 0;          // 项目编号、照片编号、副本编号（不是条目编号）
    std::int64_t project_id = 0;  // 图片和副本所在的项目
    std::string name;             // 项目名；图片和副本是文件名
    std::string project_name;
    std::int64_t copy = 0;        // 副本的序号
    std::int64_t days_left = 0;
  };
  // 删除照片或副本（条目编号）。已经在回收站里的跳过。
  ResultCode trash_items(const std::vector<std::int64_t>& items, std::string& diagnostic);
  ResultCode trash_project(std::int64_t project_id, std::string& diagnostic);
  ResultCode list_trash(std::vector<TrashEntry>& out) const;
  ResultCode restore(const TrashEntry& entry, std::string& diagnostic);
  // 彻底删除回收站里的项目；只清项目库的索引，不碰磁盘上的原图。
  ResultCode erase_trashed_project(std::int64_t project_id, std::string& diagnostic);

  ResultCode list_projects(std::vector<ProjectRow>& out) const;
  // query 非空时按筛选和排序（需求 §6.2、§6.3）列出；副本跟在原图后面（原图被筛掉，它的副本也不出现）。
  ResultCode list_photos(std::int64_t project_id, std::vector<PhotoRow>& out, const PhotoQuery* query = nullptr) const;
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
