#include "pixcake/project/source_deleter.hpp"

namespace pixcake {

FileSourceDeleter::FileSourceDeleter(FileSystem& files) : files_(files) {}

ResultCode FileSourceDeleter::remove_source(const std::string& absolute_path) {
  if (absolute_path.empty() || absolute_path.front() != '/' ||
      absolute_path.find('\0') != std::string::npos) {
    return ResultCode::StateConflict;
  }
  const IoResult removed = files_.remove_file(absolute_path);
  if (removed.ok) {
    return ResultCode::Ok;
  }
  if (removed.not_found) {
    return ResultCode::SourceMissing;
  }
  return removed.code;
}

}  // namespace pixcake
