#pragma once

#include "pixcake/persist/file_system.hpp"
#include "pixcake/project/project_store.hpp"
#include "pixcake/project/source_deleter.hpp"
#include "pixcake/support/clock.hpp"
#include "pixcake/support/log_sink.hpp"

#include <optional>
#include <vector>

namespace pixcake {

// 回收站按删除时间加 2592000 秒到期。到期只删索引，不删原图。
// 只有 delete_source_file 调用 SourceDeleter。
// 原图只记绝对路径和调用方交来的访问凭证。add_photo 不探测文件，因此不返回原文件缺失。
class ProjectLibrary {
 public:
  ProjectLibrary(ProjectStore& store, FileSystem& files, const Clock& clock, SourceDeleter& deleter,
                 LogSink* log);

  ProjectResult create_project(std::string name, std::string origin, bool cloud_marked);
  ProjectResult add_photo(std::int64_t project_id, std::string absolute_path, std::string file_name,
                          std::vector<std::uint8_t> access_credential);

  ResultCode startup();
  ResultCode refresh_source_presence();
  ResultCode source_status(std::int64_t photo_id) const;
  ResultCode purge_expired_trash();

  ProjectResult trash_project(std::int64_t project_id);
  ProjectResult trash_photo(std::int64_t photo_id);
  ProjectResult restore_project(std::int64_t project_id);
  ProjectResult restore_photos(std::vector<std::int64_t> photo_ids);
  ProjectResult erase_trashed_project(std::int64_t project_id);
  ProjectResult delete_source_file(std::int64_t photo_id);

  ResultCode load_project(std::int64_t id, std::optional<ProjectRow>& out) const;
  ResultCode load_trash(const std::string& subject, std::int64_t subject_id, std::optional<TrashRow>& out) const;
  ResultCode list_trash(std::vector<TrashRow>& out) const;

  ResultCode list_projects(std::vector<ProjectRow>& out) const;
  ResultCode list_photos(const std::optional<std::int64_t>& project_id, std::vector<PhotoRow>& out) const;
  ResultCode load_photo(std::int64_t id, std::optional<PhotoRow>& out) const;

 private:
  void note(ResultCode code, const std::string& path) const;

  bool trash_expired(std::int64_t deleted_at, std::int64_t now) const;

  ProjectStore& store_;
  FileSystem& files_;
  const Clock& clock_;
  SourceDeleter& deleter_;
  LogSink* log_ = nullptr;
};

}  // namespace pixcake
