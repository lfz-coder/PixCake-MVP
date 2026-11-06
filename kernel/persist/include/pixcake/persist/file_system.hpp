#pragma once

#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pixcake {

// not_found 为真时，code 不是给用户看的结果码。调用方必须先看 not_found。
struct IoResult {
  bool ok = false;
  bool not_found = false;
  ResultCode code = ResultCode::IoFailed;
};

// A file identity is stronger than a path or a content checksum.  Export
// recovery uses it before removing a destination, so a user replacement at
// the same path is never mistaken for the task's file.
struct FileIdentity {
  bool valid = false;
  std::uint64_t device = 0;
  std::uint64_t inode = 0;
  std::uint64_t size = 0;
  std::uint64_t fingerprint = 0;
};

class FileWriteLock {
 public:
  virtual ~FileWriteLock() = default;
};

class NoopFileWriteLock final : public FileWriteLock {};

struct FileWriteLockResult {
  ResultCode code = ResultCode::Ok;
  std::unique_ptr<FileWriteLock> lock;
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
  // exists 分不清没有文件和没有权限。探测原图只用这个结果。
  virtual IoResult probe_file(const std::string& path) const = 0;
  virtual IoResult read_file(const std::string& path, std::vector<std::uint8_t>& out) = 0;
  virtual IoResult write_new(const std::string& path, const std::vector<std::uint8_t>& data) = 0;
  virtual IoResult remove_file(const std::string& path) = 0;
  virtual IoResult rename_file(const std::string& from, const std::string& to) = 0;
  virtual IoResult publish_new(const std::string& from, const std::string& to) = 0;
  virtual IoResult sync_file(const std::string& path) = 0;
  virtual IoResult sync_directory(const std::string& path) = 0;
  virtual IoResult create_directory(const std::string& path) = 0;

  // The default implementation is deliberately conservative and is useful
  // for in-memory file systems.  PosixFileSystem overrides it to resolve
  // symlinks and compare device/inode identities.
  virtual IoResult path_within_root(const std::string& path, const std::string& root) const;
  virtual IoResult identify(const std::string& path, FileIdentity& out) const;
  virtual IoResult remove_if_identity(const std::string& path, const FileIdentity& expected);
  virtual FileWriteLockResult try_write_lock(const std::string& path) const;
};

class PosixFileSystem : public FileSystem {
 public:
  bool exists(const std::string& path) const override;
  IoResult probe_file(const std::string& path) const override;
  IoResult read_file(const std::string& path, std::vector<std::uint8_t>& out) override;
  IoResult write_new(const std::string& path, const std::vector<std::uint8_t>& data) override;
  IoResult remove_file(const std::string& path) override;
  IoResult rename_file(const std::string& from, const std::string& to) override;
  IoResult publish_new(const std::string& from, const std::string& to) override;
  IoResult sync_file(const std::string& path) override;
  IoResult sync_directory(const std::string& path) override;
  IoResult create_directory(const std::string& path) override;
  IoResult path_within_root(const std::string& path, const std::string& root) const override;
  IoResult identify(const std::string& path, FileIdentity& out) const override;
  IoResult remove_if_identity(const std::string& path, const FileIdentity& expected) override;
  FileWriteLockResult try_write_lock(const std::string& path) const override;
};

}  // namespace pixcake
