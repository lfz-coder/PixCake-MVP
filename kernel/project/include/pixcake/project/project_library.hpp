#pragma once

#include "pixcake/persist/file_system.hpp"
#include "pixcake/project/project_store.hpp"
#include "pixcake/support/clock.hpp"
#include "pixcake/support/log_sink.hpp"

#include <optional>
#include <vector>

namespace pixcake {

// 原图只记绝对路径和调用方交来的访问凭证。add_photo 不探测文件，因此不返回原文件缺失。
class ProjectLibrary {
 public:
  ProjectLibrary(ProjectStore& store, FileSystem& files, const Clock& clock, LogSink* log);

  ProjectResult create_project(std::string name, std::string origin, bool cloud_marked);
  ProjectResult add_photo(std::int64_t project_id, std::string absolute_path, std::string file_name,
                          std::vector<std::uint8_t> access_credential);

  ResultCode startup();
  ResultCode refresh_source_presence();
  ResultCode source_status(std::int64_t photo_id) const;
  ResultCode purge_expired_trash();

  ResultCode list_projects(std::vector<ProjectRow>& out) const;
  ResultCode list_photos(const std::optional<std::int64_t>& project_id, std::vector<PhotoRow>& out) const;
  ResultCode load_photo(std::int64_t id, std::optional<PhotoRow>& out) const;

 private:
  void note(ResultCode code, const std::string& path) const;

  ProjectStore& store_;
  FileSystem& files_;
  const Clock& clock_;
  LogSink* log_ = nullptr;
};

}  // namespace pixcake
