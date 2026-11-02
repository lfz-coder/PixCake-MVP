#include "pixcake/persist/file_system.hpp"

#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

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

}  // namespace

bool PosixFileSystem::exists(const std::string& path) const {
  struct stat info;
  return ::stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
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

}  // namespace pixcake
