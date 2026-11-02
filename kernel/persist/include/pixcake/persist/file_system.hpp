#pragma once

#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pixcake {

// not_found 为真时，code 不是给用户看的结果码。调用方必须先看 not_found。
struct IoResult {
  bool ok = false;
  bool not_found = false;
  ResultCode code = ResultCode::IoFailed;
};

inline IoResult io_ok() {
  IoResult result;
  result.ok = true;
  result.code = ResultCode::Ok;
  return result;
}

inline IoResult io_not_found() {
  IoResult result;
  result.not_found = true;
  return result;
}

inline IoResult io_error(ResultCode code) {
  IoResult result;
  result.code = code;
  return result;
}

// write_new 只创建新文件。正式文件不能传进来，否则就会先截断再写。
class FileSystem {
 public:
  virtual ~FileSystem() = default;
  virtual bool exists(const std::string& path) const = 0;
  virtual IoResult read_file(const std::string& path, std::vector<std::uint8_t>& out) = 0;
  virtual IoResult write_new(const std::string& path, const std::vector<std::uint8_t>& data) = 0;
  virtual IoResult remove_file(const std::string& path) = 0;
  virtual IoResult rename_file(const std::string& from, const std::string& to) = 0;
  virtual IoResult sync_file(const std::string& path) = 0;
  virtual IoResult sync_directory(const std::string& path) = 0;
  virtual IoResult create_directory(const std::string& path) = 0;
};

class PosixFileSystem : public FileSystem {
 public:
  bool exists(const std::string& path) const override;
  IoResult read_file(const std::string& path, std::vector<std::uint8_t>& out) override;
  IoResult write_new(const std::string& path, const std::vector<std::uint8_t>& data) override;
  IoResult remove_file(const std::string& path) override;
  IoResult rename_file(const std::string& from, const std::string& to) override;
  IoResult sync_file(const std::string& path) override;
  IoResult sync_directory(const std::string& path) override;
  IoResult create_directory(const std::string& path) override;
};

}  // namespace pixcake
