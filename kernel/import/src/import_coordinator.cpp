#include "pixcake/image_import/import_coordinator.hpp"

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
    const ResultCode admitted = probe_source(files_, candidate.absolute_path, source);
    if (admitted != ResultCode::Ok) {
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
        project_.add_photo(project_id, candidate.absolute_path, candidate.file_name, candidate.access_credential);
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

}  // namespace pixcake::image_import
