#pragma once

#include "pixcake/project/records.hpp"

#include <functional>
#include <optional>
#include <vector>

namespace pixcake {

class ProjectStore {
 public:
  virtual ~ProjectStore() = default;

  virtual ResultCode insert_project(const ProjectDraft& draft, std::int64_t& id) = 0;
  virtual ResultCode update_project_deleted_at(std::int64_t id,
                                                const std::optional<std::int64_t>& deleted_at) = 0;
  virtual ResultCode load_project(std::int64_t id, std::optional<ProjectRow>& out) = 0;
  virtual ResultCode list_projects(std::vector<ProjectRow>& out) = 0;
  virtual ResultCode erase_project_index(std::int64_t project_id) = 0;

  virtual ResultCode insert_photo(const PhotoDraft& draft, std::int64_t& id) = 0;
  virtual ResultCode update_photo_presence(std::int64_t id, const std::string& presence) = 0;
  virtual ResultCode load_photo(std::int64_t id, std::optional<PhotoRow>& out) = 0;
  virtual ResultCode list_photos(const std::optional<std::int64_t>& project_id,
                                 std::vector<PhotoRow>& out) = 0;
  virtual ResultCode erase_photo(std::int64_t id) = 0;

  virtual ResultCode insert_virtual_copy(const VirtualCopyDraft& draft, std::int64_t& id) = 0;
  virtual ResultCode load_virtual_copy(std::int64_t id, std::optional<VirtualCopyRow>& out) = 0;
  virtual ResultCode load_virtual_copy_by_document(const std::string& document_id,
                                                   std::optional<VirtualCopyRow>& out) = 0;
  virtual ResultCode list_virtual_copies(std::int64_t source_photo_id, std::vector<VirtualCopyRow>& out) = 0;
  virtual ResultCode erase_virtual_copy(std::int64_t id) = 0;
  virtual ResultCode erase_virtual_copies_of_photo(std::int64_t photo_id) = 0;

  virtual ResultCode insert_trash(const std::string& subject, std::int64_t subject_id,
                                  std::int64_t deleted_at) = 0;
  virtual ResultCode load_trash(const std::string& subject, std::int64_t subject_id,
                                std::optional<TrashRow>& out) = 0;
  virtual ResultCode list_trash(std::vector<TrashRow>& out) = 0;
  virtual ResultCode erase_trash(const std::string& subject, std::int64_t subject_id) = 0;

  virtual ResultCode in_transaction(const std::function<ResultCode()>& body) = 0;
};

}  // namespace pixcake
