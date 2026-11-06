#include "pixcake/instance/single_instance.hpp"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

namespace pixcake::instance {

namespace {

constexpr std::size_t kMaxMessage = 4096;

ResultCode io_code(int error) {
  switch (error) {
    case EACCES:
    case EPERM:
      return ResultCode::PermissionDenied;
    case ENOSPC:
      return ResultCode::DiskFull;
    default:
      return ResultCode::IoFailed;
  }
}

ResultCode make_directories(const std::string& directory) {
  if (directory.empty()) return ResultCode::IoFailed;
  for (std::size_t i = 1; i <= directory.size(); ++i) {
    if (i != directory.size() && directory[i] != '/') continue;
    const std::string partial = directory.substr(0, i);
    struct stat info {};
    if (::stat(partial.c_str(), &info) == 0) {
      if (!S_ISDIR(info.st_mode)) return ResultCode::IoFailed;
      continue;
    }
    if (::mkdir(partial.c_str(), 0700) != 0 && errno != EEXIST) return io_code(errno);
  }
  return ResultCode::Ok;
}

bool socket_address(const std::string& path, sockaddr_un& address) {
  std::memset(&address, 0, sizeof(address));
  address.sun_family = AF_UNIX;
  if (path.size() >= sizeof(address.sun_path)) return false;
  std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
  return true;
}

void set_cloexec(int fd) { ::fcntl(fd, F_SETFD, ::fcntl(fd, F_GETFD) | FD_CLOEXEC); }

}  // namespace

struct InstanceAccess {
  static std::unique_ptr<PrimaryInstance> make(int lock_fd, int listen_fd, std::string path) {
    return std::unique_ptr<PrimaryInstance>(new PrimaryInstance(lock_fd, listen_fd, std::move(path)));
  }
};

PrimaryInstance::PrimaryInstance(int lock_fd, int listen_fd, std::string socket_path)
    : lock_fd_(lock_fd), listen_fd_(listen_fd), socket_path_(std::move(socket_path)) {}

PrimaryInstance::~PrimaryInstance() {
  // 先删套接字再放锁：放锁之后下一个主实例可能已经绑定了同名套接字。
  ::close(listen_fd_);
  ::unlink(socket_path_.c_str());
  ::close(lock_fd_);
}

std::optional<std::string> PrimaryInstance::take_message() {
  const int connection = ::accept(listen_fd_, nullptr, nullptr);
  if (connection < 0) return std::nullopt;
  set_cloexec(connection);
  uid_t uid = 0;
  gid_t gid = 0;
  if (::getpeereid(connection, &uid, &gid) != 0 || uid != ::geteuid()) {
    ::close(connection);
    return std::nullopt;
  }
  ::fcntl(connection, F_SETFL, ::fcntl(connection, F_GETFL) & ~O_NONBLOCK);
  timeval timeout{1, 0};
  ::setsockopt(connection, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  std::string line;
  char buffer[512];
  while (line.size() <= kMaxMessage) {
    const ssize_t got = ::read(connection, buffer, sizeof(buffer));
    if (got <= 0) break;
    line.append(buffer, static_cast<std::size_t>(got));
    const auto newline = line.find('\n');
    if (newline != std::string::npos) {
      ::close(connection);
      if (newline > kMaxMessage) return std::nullopt;
      return line.substr(0, newline);
    }
  }
  ::close(connection);
  return std::nullopt;
}

InstanceClaim claim_instance(const std::string& directory) {
  InstanceClaim claim;
  const std::string socket_path = directory + "/instance.sock";
  sockaddr_un address{};
  if (directory.empty() || !socket_address(socket_path, address)) {
    claim.code = ResultCode::IoFailed;
    return claim;
  }
  claim.code = make_directories(directory);
  if (claim.code != ResultCode::Ok) return claim;

  const std::string lock_path = directory + "/instance.lock";
  const int lock_fd = ::open(lock_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  if (lock_fd < 0) {
    claim.code = io_code(errno);
    return claim;
  }
  if (::flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
    const int error = errno;
    ::close(lock_fd);
    if (error == EWOULDBLOCK) {
      claim.another_running = true;
    } else {
      claim.code = io_code(error);
    }
    return claim;
  }

  // 拿到锁就说明之前的主实例已经不在；它留下的套接字文件可以删。
  ::unlink(socket_path.c_str());
  const int listen_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    claim.code = io_code(errno);
    ::close(lock_fd);
    return claim;
  }
  set_cloexec(listen_fd);
  ::fcntl(listen_fd, F_SETFL, ::fcntl(listen_fd, F_GETFL) | O_NONBLOCK);
  if (::bind(listen_fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
      ::chmod(socket_path.c_str(), 0600) != 0 || ::listen(listen_fd, 8) != 0) {
    claim.code = io_code(errno);
    ::close(listen_fd);
    ::unlink(socket_path.c_str());
    ::close(lock_fd);
    return claim;
  }
  claim.primary = InstanceAccess::make(lock_fd, listen_fd, socket_path);
  return claim;
}

ResultCode notify_primary(const std::string& directory, const std::string& message) {
  if (message.empty() || message.size() > kMaxMessage || message.find('\n') != std::string::npos) {
    return ResultCode::StateConflict;
  }
  sockaddr_un address{};
  if (!socket_address(directory + "/instance.sock", address)) return ResultCode::IoFailed;
  const std::string line = message + "\n";
  ResultCode last = ResultCode::IoFailed;
  for (int attempt = 0; attempt < 20; ++attempt) {
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return io_code(errno);
    set_cloexec(fd);
    if (::connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0) {
      const ssize_t sent = ::write(fd, line.data(), line.size());
      ::close(fd);
      return sent == static_cast<ssize_t>(line.size()) ? ResultCode::Ok : ResultCode::IoFailed;
    }
    last = io_code(errno);
    ::close(fd);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  return last;
}

std::string default_instance_directory() {
  const char* home = std::getenv("HOME");
  if (home == nullptr || *home == '\0') return {};
  return std::string(home) + "/Library/Application Support/PixCake";
}

}  // namespace pixcake::instance
