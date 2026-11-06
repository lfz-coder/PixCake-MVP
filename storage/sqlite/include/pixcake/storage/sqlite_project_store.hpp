#pragma once

#include "pixcake/project/project_store.hpp"

#include <memory>
#include <string>

namespace pixcake {

class SqliteProjectStore : public ProjectStore {
 public:
  static ResultCode open(const std::string& path, std::unique_ptr<SqliteProjectStore>& out);
  ~SqliteProjectStore() override;

  std::string journal_mode() const;

  ResultCode insert_project(const ProjectDraft& draft, std::int64_t& id) override;
  ResultCode update_project_deleted_at(std::int64_t id, const std::optional<std::int64_t>& deleted_at) override;
  ResultCode load_project(std::int64_t id, std::optional<ProjectRow>& out) override;
  ResultCode list_projects(std::vector<ProjectRow>& out) override;
  ResultCode erase_project_index(std::int64_t project_id) override;

  ResultCode insert_photo(const PhotoDraft& draft, std::int64_t& id) override;
  ResultCode update_photo_presence(std::int64_t id, const std::string& presence) override;
  ResultCode update_photo_access_credential(std::int64_t id, const std::vector<std::uint8_t>& credential) override;
  ResultCode update_photo_rating(std::int64_t id, int rating) override;
  ResultCode update_photo_flag(std::int64_t id, const std::string& flag) override;
  ResultCode update_photo_color(std::int64_t id, const std::string& color) override;
  ResultCode update_photo_ratings(const std::vector<std::int64_t>& ids, int rating) override;
  ResultCode update_photo_flags(const std::vector<std::int64_t>& ids, const std::string& flag) override;
  ResultCode update_photo_colors(const std::vector<std::int64_t>& ids, const std::string& color) override;
  ResultCode load_photo(std::int64_t id, std::optional<PhotoRow>& out) override;
  ResultCode list_photos(const std::optional<std::int64_t>& project_id, std::vector<PhotoRow>& out) override;
  ResultCode erase_photo(std::int64_t id) override;
  ResultCode update_photo_edit_state(std::int64_t id, const std::string& edit_status,
                                     std::int64_t edited_at) override;
  ResultCode update_photo_export_status(std::int64_t id, const std::string& export_status) override;
  ResultCode query_photos(const PhotoQuery& query, std::vector<PhotoRow>& out) override;
  // 诊断用：返回 query_photos 那条语句的 EXPLAIN QUERY PLAN 明细（性能清单 P-12 的索引证据）。
  ResultCode explain_photo_query(const PhotoQuery& query, std::vector<std::string>& plan);

  ResultCode insert_virtual_copy(const VirtualCopyDraft& draft, std::int64_t& id) override;
  ResultCode load_virtual_copy(std::int64_t id, std::optional<VirtualCopyRow>& out) override;
  ResultCode load_virtual_copy_by_document(const std::string& document_id,
                                           std::optional<VirtualCopyRow>& out) override;
  ResultCode list_virtual_copies(std::int64_t source_photo_id, std::vector<VirtualCopyRow>& out) override;
  ResultCode erase_virtual_copy(std::int64_t id) override;
  ResultCode erase_virtual_copies_of_photo(std::int64_t photo_id) override;

  ResultCode insert_trash(const std::string& subject, std::int64_t subject_id, std::int64_t deleted_at) override;
  ResultCode load_trash(const std::string& subject, std::int64_t subject_id, std::optional<TrashRow>& out) override;
  ResultCode list_trash(std::vector<TrashRow>& out) override;
  ResultCode erase_trash(const std::string& subject, std::int64_t subject_id) override;

  ResultCode in_transaction(const std::function<ResultCode()>& body) override;

 private:
  struct Impl;
  explicit SqliteProjectStore(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace pixcake
