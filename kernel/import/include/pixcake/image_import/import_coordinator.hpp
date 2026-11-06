#pragma once

#include "pixcake/image_import/source_probe.hpp"
#include "pixcake/project/project_library.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pixcake::image_import {

struct ImportCandidate {
  std::string absolute_path;
  std::string file_name;
  std::vector<std::uint8_t> access_credential;
};

struct ImportItemResult {
  std::string absolute_path;
  ResultCode code = ResultCode::StateConflict;
  std::int64_t photo_id = 0;
  bool probed = false;
  SourceProbe source;
};

struct ImportBatchResult {
  ResultCode overall_code = ResultCode::Ok;
  std::size_t accepted_count = 0;
  std::vector<ImportItemResult> items;
};

class ImportCoordinator {
 public:
  ImportCoordinator(ProjectLibrary& project, FileSystem& files);

  ImportBatchResult import_into(std::int64_t project_id, const std::vector<ImportCandidate>& candidates);

 private:
  ProjectLibrary& project_;
  FileSystem& files_;
};

}  // namespace pixcake::image_import
