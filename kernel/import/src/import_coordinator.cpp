#include "pixcake/image_import/import_coordinator.hpp"

#include "pixcake/support/user_notice.hpp"

namespace pixcake::image_import {

ImportCoordinator::ImportCoordinator(ProjectLibrary& project, FileSystem& files)
    : project_(project), files_(files) {}

ImportBatchResult ImportCoordinator::import_into(std::int64_t project_id,
                                                 const std::vector<ImportCandidate>& candidates) {
  ImportBatchResult result;
  if (candidates.empty()) {
    result.overall_code = ResultCode::StateConflict;
    return result;
  }
  result.items.reserve(candidates.size());
  for (const ImportCandidate& candidate : candidates) {
    ImportItemResult item;
    item.absolute_path = candidate.absolute_path;
    SourceProbe source;
    std::optional<AdmittedSource> admission;
    const ResultCode admitted = admit_source(files_, candidate.absolute_path, admission, &source);
    if (admitted != ResultCode::Ok || !admission.has_value()) {
      item.code = admitted;
      if (result.overall_code == ResultCode::Ok) {
        result.overall_code = admitted;
      }
      result.items.push_back(std::move(item));
      continue;
    }
    item.probed = true;
    item.source = source;
    const ProjectResult added =
        project_.add_photo(project_id, *admission, candidate.file_name, candidate.access_credential);
    item.code = added.code;
    item.photo_id = added.id;
    if (added.code == ResultCode::Ok) {
      ++result.accepted_count;
    } else if (result.overall_code == ResultCode::Ok) {
      result.overall_code = added.code;
    }
    result.items.push_back(std::move(item));
  }
  return result;
}

std::vector<std::string> import_notices(const ImportBatchResult& batch) {
  std::vector<std::string> notices;
  for (const auto& item : batch.items) {
    if (item.code == ResultCode::Ok) continue;
    const auto slash = item.absolute_path.find_last_of('/');
    const std::string name = slash == std::string::npos ? item.absolute_path : item.absolute_path.substr(slash + 1);
    notices.push_back(name + "：" + notice_for(NoticeScene::Import, item.code).text);
  }
  return notices;
}

}  // namespace pixcake::image_import
