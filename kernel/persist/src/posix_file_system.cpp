#include "pixcake/persist/file_system.hpp"

#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <filesystem>

namespace pixcake {
namespace {

IoResult from_errno(int error) {
  if (error == ENOENT) {
    return io_not_found();
  }
  if (error == ENOSPC || error == EDQUOT) {
    return io_error(ResultCode::DiskFull);
  }
  if (error == EACCES || error == EPERM || error == EROFS) {
    return io_error(ResultCode::PermissionDenied);
  }
  return io_error(ResultCode::IoFailed);
}

class Fd {
 public:
  explicit Fd(int fd) : fd_(fd) {}
  ~Fd() { reset(); }
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;

  int get() const { return fd_; }

  void reset() {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }

 private:
  int fd_ = -1;
};

class PosixWriteLock final : public FileWriteLock {
 public:
  explicit PosixWriteLock(int fd) : fd_(fd) {}
  ~PosixWriteLock() override {
    if (fd_ >= 0) {
      ::close(fd_);
    }
  }

 private:
  int fd_ = -1;
};

}  // namespace

IoResult FileSystem::path_within_root(const std::string& path, const std::string& root) const {
  if (path.empty() || root.empty() || path.front() != '/' || root.front() != '/') {
    return io_error(ResultCode::StateConflict);
  }
  std::filesystem::path p(path);
  std::filesystem::path r(root);
  if (p.filename().empty()) return io_error(ResultCode::StateConflict);
  for (const auto& part : p) {
    if (part == "." || part == "..") return io_error(ResultCode::StateConflict);
  }
  const auto relative = p.lexically_relative(r);
  if (relative.empty() || relative == "." || relative.string().rfind("..", 0) == 0) {
    return io_error(ResultCode::StateConflict);
  }
  return io_ok();
}

IoResult FileSystem::identify(const std::string&, FileIdentity&) const {
  return io_error(ResultCode::StateConflict);
}

IoResult FileSystem::remove_if_identity(const std::string&, const FileIdentity&) {
  return io_error(ResultCode::StateConflict);
}

bool PosixFileSystem::exists(const std::string& path) const {
  struct stat info;
  return ::stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

IoResult PosixFileSystem::probe_file(const std::string& path) const {
  struct stat info;
  if (::stat(path.c_str(), &info) != 0) {
    return from_errno(errno);
  }
  if (!S_ISREG(info.st_mode)) {
    return io_not_found();
  }
  return io_ok();
}

IoResult PosixFileSystem::read_file(const std::string& path, std::vector<std::uint8_t>& out) {
  Fd fd(::open(path.c_str(), O_RDONLY));
  if (fd.get() < 0) {
    return from_errno(errno);
  }
  struct stat info;
  if (::fstat(fd.get(), &info) != 0) {
    return from_errno(errno);
  }
  if (info.st_size < 0) {
    return io_error(ResultCode::IoFailed);
  }
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(info.st_size));
  std::size_t got = 0;
  while (got < bytes.size()) {
    const ssize_t n = ::read(fd.get(), bytes.data() + got, bytes.size() - got);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return from_errno(errno);
    }
    if (n == 0) {
      break;
    }
    got += static_cast<std::size_t>(n);
  }
  bytes.resize(got);
  out = std::move(bytes);
  return io_ok();
}

IoResult PosixFileSystem::write_new(const std::string& path, const std::vector<std::uint8_t>& data) {
  Fd fd(::open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644));
  if (fd.get() < 0) {
    return from_errno(errno);
  }
  std::size_t wrote = 0;
  while (wrote < data.size()) {
    const ssize_t n = ::write(fd.get(), data.data() + wrote, data.size() - wrote);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      const int error = errno;
      fd.reset();
      ::unlink(path.c_str());
      return from_errno(error);
    }
    wrote += static_cast<std::size_t>(n);
  }
  return io_ok();
}

IoResult PosixFileSystem::remove_file(const std::string& path) {
  if (::unlink(path.c_str()) != 0) {
    return from_errno(errno);
  }
  return io_ok();
}

IoResult PosixFileSystem::rename_file(const std::string& from, const std::string& to) {
  if (::rename(from.c_str(), to.c_str()) != 0) {
    return from_errno(errno);
  }
  return io_ok();
}

IoResult PosixFileSystem::publish_new(const std::string& from, const std::string& to) {
  if (::link(from.c_str(), to.c_str()) != 0) {
    return from_errno(errno);
  }
  if (::unlink(from.c_str()) != 0) {
    const int error = errno;
    ::unlink(to.c_str());
    return from_errno(error);
  }
  return io_ok();
}

IoResult PosixFileSystem::sync_file(const std::string& path) {
  Fd fd(::open(path.c_str(), O_RDWR));
  if (fd.get() < 0) {
    return from_errno(errno);
  }
  if (::fsync(fd.get()) != 0) {
    return from_errno(errno);
  }
  return io_ok();
}

IoResult PosixFileSystem::sync_directory(const std::string& path) {
  Fd fd(::open(path.c_str(), O_RDONLY));
  if (fd.get() < 0) {
    return from_errno(errno);
  }
  if (::fsync(fd.get()) != 0) {
    return from_errno(errno);
  }
  return io_ok();
}

IoResult PosixFileSystem::create_directory(const std::string& path) {
  std::error_code error;
  std::filesystem::create_directories(path, error);
  if (!error) {
    return io_ok();
  }
  if (error == std::errc::permission_denied) {
    return io_error(ResultCode::PermissionDenied);
  }
  if (error == std::errc::no_space_on_device) {
    return io_error(ResultCode::DiskFull);
  }
  return io_error(ResultCode::IoFailed);
}

IoResult PosixFileSystem::path_within_root(const std::string& path, const std::string& root) const {
  std::error_code error;
  const auto canonical_root = std::filesystem::weakly_canonical(root, error);
  if (error) return io_error(error == std::errc::permission_denied ? ResultCode::PermissionDenied : ResultCode::IoFailed);
  const auto parent = std::filesystem::path(path).parent_path();
  const auto canonical_parent = std::filesystem::weakly_canonical(parent, error);
  if (error) return io_error(error == std::errc::permission_denied ? ResultCode::PermissionDenied : ResultCode::IoFailed);
  const auto relative = canonical_parent.lexically_relative(canonical_root);
  if (relative.empty() || relative.string().rfind("..", 0) == 0 || relative.is_absolute()) {
    return io_error(ResultCode::StateConflict);
  }
  return io_ok();
}

IoResult PosixFileSystem::identify(const std::string& path, FileIdentity& out) const {
  struct stat info;
  if (::lstat(path.c_str(), &info) != 0) return from_errno(errno);
  if (!S_ISREG(info.st_mode)) return io_error(ResultCode::StateConflict);
  out.valid = true;
  out.device = static_cast<std::uint64_t>(info.st_dev);
  out.inode = static_cast<std::uint64_t>(info.st_ino);
  out.size = static_cast<std::uint64_t>(info.st_size);
  out.fingerprint = 0;
  return io_ok();
}

IoResult PosixFileSystem::remove_if_identity(const std::string& path, const FileIdentity& expected) {
  FileIdentity current;
  const IoResult found = identify(path, current);
  if (!found.ok) return found;
  if (!expected.valid || current.device != expected.device || current.inode != expected.inode ||
      current.size != expected.size) {
    return io_error(ResultCode::StateConflict);
  }
  return remove_file(path);
}

FileWriteLockResult PosixFileSystem::try_write_lock(const std::string& path) const {
  FileWriteLockResult result;
  if (path.empty()) {
    result.code = ResultCode::StateConflict;
    return result;
  }
  const std::string lock_path = path + ".lock";
  const int fd = ::open(lock_path.c_str(), O_CREAT | O_RDWR, 0644);
  if (fd < 0) {
    result.code = from_errno(errno).code;
    return result;
  }
  if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
    const int error = errno;
    ::close(fd);
    result.code = (error == EWOULDBLOCK || error == EAGAIN) ? ResultCode::StateConflict
                                                            : from_errno(error).code;
    return result;
  }
  result.lock = std::make_unique<PosixWriteLock>(fd);
  result.code = ResultCode::Ok;
  return result;
}

}  // namespace pixcake
