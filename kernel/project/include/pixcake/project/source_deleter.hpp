#pragma once

#include "pixcake/persist/file_system.hpp"
#include "pixcake/support/result_code.hpp"

#include <string>

namespace pixcake {

class SourceDeleter {
 public:
  virtual ~SourceDeleter() = default;
  virtual ResultCode remove_source(const std::string& absolute_path) = 0;
};

// 只删除调用方给出的那一个绝对路径。
class FileSourceDeleter : public SourceDeleter {
 public:
  explicit FileSourceDeleter(FileSystem& files);
  ResultCode remove_source(const std::string& absolute_path) override;

 private:
  FileSystem& files_;
};

}  // namespace pixcake
