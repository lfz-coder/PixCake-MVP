#pragma once

#include "pixcake/project/admitted_source.hpp"
#include "pixcake/persist/file_system.hpp"
#include "pixcake/project/project_store.hpp"
#include "pixcake/project/source_access.hpp"
#include "pixcake/project/source_deleter.hpp"
#include "pixcake/support/clock.hpp"
#include "pixcake/support/log_sink.hpp"

#include <optional>
#include <vector>

namespace pixcake {

// 回收站按删除时间加 2592000 秒到期。到期只删索引，不删原图。
// 只有 delete_source_file 调用 SourceDeleter。
// 原图只记绝对路径和调用方交来的访问凭证。add_photo 只接受经过内容准入的 AdmittedSource。
// 虚拟副本指向原图，并保存调用方交来的参数文档编号。编号是不透明字符串，这里不打开参数文档。
// 项目库只回答这些条目是否共用一张原图。张数在账本里计算，这里不减张数，也没有余额。
// 星级、旗标和色标记在图片上。虚拟副本不另存这三列。
class ProjectLibrary {
 public:
  ProjectLibrary(ProjectStore& store, FileSystem& files, const Clock& clock, SourceDeleter& deleter,
                 LogSink* log);

  ProjectResult create_project(std::string name, std::string origin, bool cloud_marked);
  // 只接受导入准入签发的凭证。访问凭证（书签或安全范围）为空返回权限不足（S-10）。写入前重新识别文件：文件已不存在返回原文件缺失，
  // 身份（设备、inode、大小）与准入时不同返回状态冲突，均不写入。
  ProjectResult add_photo(std::int64_t project_id, const AdmittedSource& source, std::string file_name,
                          std::vector<std::uint8_t> access_credential);
  ProjectResult create_virtual_copy(std::int64_t photo_id, std::string document_id);
  ProjectResult set_photo_ratings(std::vector<std::int64_t> photo_ids, int rating);
  ProjectResult set_photo_flags(std::vector<std::int64_t> photo_ids, std::string flag);
  ProjectResult set_photo_colors(std::vector<std::int64_t> photo_ids, std::string color);
  // 修图状态记为已编辑并记下编辑时间（Unix 秒）；导出状态记为已导出。图片不存在返回状态冲突。
  ProjectResult mark_photo_edited(std::int64_t photo_id, std::int64_t edited_at);
  ProjectResult mark_photo_exported(std::int64_t photo_id);

  ResultCode startup();
  ResultCode refresh_source_presence();
  // 按书签重新解析每张原图（S-10，需求第 118 行）：书签换新时存新书签；书签指向别的路径（被挪走或改名）
  // 或文件不在时标“原文件缺失”，不改路径、不动参数文档；回到原路径时恢复“可编辑”。
  // 系统不再授予读取时状态不变，返回权限不足。
  ResultCode refresh_source_access(SourceAccess& access);
  ResultCode source_status(std::int64_t photo_id) const;
  ResultCode purge_expired_trash();

  ProjectResult trash_project(std::int64_t project_id);
  ProjectResult trash_photo(std::int64_t photo_id);
  ProjectResult trash_virtual_copy(std::int64_t copy_id);
  ProjectResult restore_project(std::int64_t project_id);
  ProjectResult restore_photos(std::vector<std::int64_t> photo_ids);
  ProjectResult restore_virtual_copies(std::vector<std::int64_t> copy_ids);
  ProjectResult erase_trashed_project(std::int64_t project_id);
  ProjectResult delete_source_file(std::int64_t photo_id);

  ResultCode load_project(std::int64_t id, std::optional<ProjectRow>& out) const;
  ResultCode load_trash(const std::string& subject, std::int64_t subject_id, std::optional<TrashRow>& out) const;
  ResultCode list_trash(std::vector<TrashRow>& out) const;

  ResultCode list_projects(std::vector<ProjectRow>& out) const;
  ResultCode list_photos(const std::optional<std::int64_t>& project_id, std::vector<PhotoRow>& out) const;
  ResultCode load_photo(std::int64_t id, std::optional<PhotoRow>& out) const;
  // 筛选和排序（需求 §6.2、§6.3）。星级区间越界或倒置、状态值不在定义内返回状态冲突。
  ResultCode query_photos(const PhotoQuery& query, std::vector<PhotoRow>& out) const;
  ResultCode load_virtual_copy(std::int64_t id, std::optional<VirtualCopyRow>& out) const;
  ResultCode list_virtual_copies(std::int64_t photo_id, std::vector<VirtualCopyRow>& out) const;
  ResultCode shares_original(const std::string& left_kind, std::int64_t left_id, const std::string& right_kind,
                             std::int64_t right_id, bool& shares) const;

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
