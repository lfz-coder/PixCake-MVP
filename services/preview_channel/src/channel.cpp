#include "pixcake/preview_channel/channel.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <random>

namespace pixcake::preview_channel {
namespace {

std::string errno_text(const char* what) { return std::string(what) + ": " + std::strerror(errno); }

void set_cloexec(int fd) { ::fcntl(fd, F_SETFD, ::fcntl(fd, F_GETFD) | FD_CLOEXEC); }

void no_sigpipe(int fd) {
#ifdef SO_NOSIGPIPE
  const int on = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#else
  (void)fd;
#endif
}

bool address_of(const std::string& path, sockaddr_un& address) {
  std::memset(&address, 0, sizeof(address));
  address.sun_family = AF_UNIX;
  if (path.empty() || path.size() >= sizeof(address.sun_path)) return false;
  std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
  return true;
}

// 0: ready, 1: timeout, -1: error.
int wait_readable(int fd, int timeout_ms) {
  pollfd p{fd, POLLIN, 0};
  for (;;) {
    const int n = ::poll(&p, 1, timeout_ms);
    if (n > 0) return 0;
    if (n == 0) return 1;
    if (errno != EINTR) return -1;
  }
}

bool write_all(int fd, const std::uint8_t* data, std::size_t size) {
  while (size > 0) {
    const ssize_t n = ::write(fd, data, size);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    data += n;
    size -= static_cast<std::size_t>(n);
  }
  return true;
}

// Reads exactly `size` bytes; false on EOF or error.
bool read_all(int fd, std::uint8_t* data, std::size_t size) {
  while (size > 0) {
    const ssize_t n = ::read(fd, data, size);
    if (n == 0) return false;
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    data += n;
    size -= static_cast<std::size_t>(n);
  }
  return true;
}

}  // namespace

UniqueFd::~UniqueFd() { reset(); }

UniqueFd& UniqueFd::operator=(UniqueFd&& other) noexcept {
  if (this != &other) reset(other.release());
  return *this;
}

int UniqueFd::release() {
  const int fd = fd_;
  fd_ = -1;
  return fd;
}

void UniqueFd::reset(int fd) {
  if (fd_ >= 0) ::close(fd_);
  fd_ = fd;
}

ResultCode listen_at(const std::string& path, UniqueFd& out, std::string& diagnostic) {
  out.reset();
  sockaddr_un address{};
  if (!address_of(path, address)) {
    diagnostic = "socket path is empty or too long: " + path;
    return ResultCode::StateConflict;
  }
  struct stat info {};
  if (::lstat(path.c_str(), &info) == 0) {
    if (!S_ISSOCK(info.st_mode)) {
      diagnostic = "refusing to replace a non-socket file: " + path;
      return ResultCode::StateConflict;
    }
    ::unlink(path.c_str());
  }
  UniqueFd fd(::socket(AF_UNIX, SOCK_STREAM, 0));
  if (!fd.valid()) {
    diagnostic = errno_text("socket");
    return ResultCode::IoFailed;
  }
  set_cloexec(fd.get());
  const mode_t previous = ::umask(0077);
  const int bound = ::bind(fd.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address));
  ::umask(previous);
  if (bound != 0 || ::listen(fd.get(), 4) != 0) {
    diagnostic = errno_text("bind/listen");
    return ResultCode::IoFailed;
  }
  out = std::move(fd);
  diagnostic.clear();
  return ResultCode::Ok;
}

ResultCode accept_same_user(int listen_fd, int timeout_ms, UniqueFd& out, std::string& diagnostic) {
  out.reset();
  const int ready = wait_readable(listen_fd, timeout_ms);
  if (ready == 1) {
    diagnostic = "no connection before the timeout";
    return ResultCode::Cancelled;
  }
  UniqueFd fd(ready == 0 ? ::accept(listen_fd, nullptr, nullptr) : -1);
  if (!fd.valid()) {
    diagnostic = errno_text("accept");
    return ResultCode::IoFailed;
  }
  set_cloexec(fd.get());
  uid_t uid = 0;
  gid_t gid = 0;
  if (::getpeereid(fd.get(), &uid, &gid) != 0 || uid != ::geteuid()) {
    diagnostic = "connection from another user refused";
    return ResultCode::PermissionDenied;
  }
  no_sigpipe(fd.get());
  out = std::move(fd);
  diagnostic.clear();
  return ResultCode::Ok;
}

ResultCode connect_to(const std::string& path, UniqueFd& out, std::string& diagnostic) {
  out.reset();
  sockaddr_un address{};
  if (!address_of(path, address)) {
    diagnostic = "socket path is empty or too long: " + path;
    return ResultCode::StateConflict;
  }
  UniqueFd fd(::socket(AF_UNIX, SOCK_STREAM, 0));
  if (!fd.valid()) {
    diagnostic = errno_text("socket");
    return ResultCode::IoFailed;
  }
  set_cloexec(fd.get());
  if (::connect(fd.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    diagnostic = errno_text("connect");
    return ResultCode::IoFailed;
  }
  no_sigpipe(fd.get());
  out = std::move(fd);
  diagnostic.clear();
  return ResultCode::Ok;
}

Channel::Channel(UniqueFd socket) : socket_(std::move(socket)) {
  if (socket_.valid()) no_sigpipe(socket_.get());
}

ResultCode Channel::send(const std::vector<std::uint8_t>& message, int attached_fd, std::string& diagnostic) {
  if (!socket_.valid()) {
    diagnostic = "channel is closed";
    return ResultCode::IoFailed;
  }
  if (message.size() > 0xFFFFFFFFu) {
    diagnostic = "message too large";
    return ResultCode::StateConflict;
  }
  std::uint8_t header[4];
  const auto length = static_cast<std::uint32_t>(message.size());
  for (int i = 0; i < 4; ++i) header[i] = static_cast<std::uint8_t>(length >> (8 * i));

  // The header goes out with sendmsg so the descriptor rides on its first byte.
  iovec io{header, sizeof(header)};
  msghdr msg{};
  msg.msg_iov = &io;
  msg.msg_iovlen = 1;
  alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int))];
  if (attached_fd >= 0) {
    std::memset(control, 0, sizeof(control));
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);
    cmsghdr* c = CMSG_FIRSTHDR(&msg);
    c->cmsg_level = SOL_SOCKET;
    c->cmsg_type = SCM_RIGHTS;
    c->cmsg_len = CMSG_LEN(sizeof(int));
    std::memcpy(CMSG_DATA(c), &attached_fd, sizeof(int));
  }
  ssize_t sent;
  do {
    sent = ::sendmsg(socket_.get(), &msg, 0);
  } while (sent < 0 && errno == EINTR);
  if (sent < 0 ||
      !write_all(socket_.get(), header + sent, sizeof(header) - static_cast<std::size_t>(sent)) ||
      !write_all(socket_.get(), message.data(), message.size())) {
    diagnostic = errno_text("send");
    return ResultCode::IoFailed;
  }
  diagnostic.clear();
  return ResultCode::Ok;
}

bool Channel::readable(int timeout_ms) const { return socket_.valid() && wait_readable(socket_.get(), timeout_ms) == 0; }

ResultCode Channel::receive(std::vector<std::uint8_t>& message, UniqueFd& attached_fd, std::size_t max_bytes,
                            int timeout_ms, std::string& diagnostic) {
  message.clear();
  attached_fd.reset();
  if (!socket_.valid()) {
    diagnostic = "channel is closed";
    return ResultCode::IoFailed;
  }
  const int ready = wait_readable(socket_.get(), timeout_ms);
  if (ready == 1) {
    diagnostic = "no message before the timeout";
    return ResultCode::Cancelled;
  }
  if (ready < 0) {
    diagnostic = errno_text("poll");
    return ResultCode::IoFailed;
  }
  std::uint8_t header[4];
  std::size_t have = 0;
  while (have < sizeof(header)) {
    iovec io{header + have, sizeof(header) - have};
    msghdr msg{};
    msg.msg_iov = &io;
    msg.msg_iovlen = 1;
    alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int))];
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);
    const ssize_t n = ::recvmsg(socket_.get(), &msg, 0);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) {
      diagnostic = n == 0 ? "peer closed the channel" : errno_text("recvmsg");
      return ResultCode::IoFailed;
    }
    for (cmsghdr* c = CMSG_FIRSTHDR(&msg); c != nullptr; c = CMSG_NXTHDR(&msg, c)) {
      if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS && c->cmsg_len >= CMSG_LEN(sizeof(int))) {
        int fd = -1;
        std::memcpy(&fd, CMSG_DATA(c), sizeof(int));
        set_cloexec(fd);
        attached_fd.reset(fd);
      }
    }
    have += static_cast<std::size_t>(n);
  }
  std::uint32_t length = 0;
  for (int i = 0; i < 4; ++i) length |= static_cast<std::uint32_t>(header[i]) << (8 * i);
  if (length > max_bytes) {
    attached_fd.reset();
    socket_.reset();  // the stream position is lost
    diagnostic = "message length " + std::to_string(length) + " exceeds the limit";
    return ResultCode::StateConflict;
  }
  message.resize(length);
  if (!read_all(socket_.get(), message.data(), message.size())) {
    message.clear();
    attached_fd.reset();
    diagnostic = "peer closed the channel inside a message";
    return ResultCode::IoFailed;
  }
  diagnostic.clear();
  return ResultCode::Ok;
}

ResultCode SharedFrame::create(std::size_t size, SharedFrame& out, std::string& diagnostic) {
  out = SharedFrame();
  if (size == 0) {
    diagnostic = "shared frame size is zero";
    return ResultCode::StateConflict;
  }
  static std::atomic<std::uint64_t> counter{0};
  static thread_local std::mt19937_64 random{std::random_device{}()};
  UniqueFd fd;
  for (int attempt = 0; attempt < 8 && !fd.valid(); ++attempt) {
    // macOS limits POSIX shm names to 31 bytes.
    char name[32];
    std::snprintf(name, sizeof(name), "/pxc%x.%llx.%llx", static_cast<unsigned>(::getpid()) & 0xFFFFF,
                  static_cast<unsigned long long>(counter++ & 0xFFFFF),
                  static_cast<unsigned long long>(random() & 0xFFFFFFFF));
    fd.reset(::shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600));
    if (fd.valid()) ::shm_unlink(name);  // anonymous from now on
  }
  if (!fd.valid()) {
    diagnostic = errno_text("shm_open");
    return ResultCode::IoFailed;
  }
  set_cloexec(fd.get());
  if (::ftruncate(fd.get(), static_cast<off_t>(size)) != 0) {
    diagnostic = errno_text("ftruncate");
    return ResultCode::IoFailed;
  }
  void* address = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0);
  if (address == MAP_FAILED) {
    diagnostic = errno_text("mmap");
    return ResultCode::IoFailed;
  }
  out.fd_ = std::move(fd);
  out.address_ = address;
  out.size_ = size;
  diagnostic.clear();
  return ResultCode::Ok;
}

ResultCode SharedFrame::map(UniqueFd fd, std::size_t size, SharedFrame& out, std::string& diagnostic) {
  out = SharedFrame();
  struct stat info {};
  if (!fd.valid() || size == 0 || ::fstat(fd.get(), &info) != 0 || info.st_size < 0 ||
      static_cast<std::uint64_t>(info.st_size) < size) {
    diagnostic = "shared frame is missing or smaller than its header says";
    return ResultCode::StateConflict;
  }
  void* address = ::mmap(nullptr, size, PROT_READ, MAP_SHARED, fd.get(), 0);
  if (address == MAP_FAILED) {
    diagnostic = errno_text("mmap");
    return ResultCode::IoFailed;
  }
  out.fd_ = std::move(fd);
  out.address_ = address;
  out.size_ = size;
  diagnostic.clear();
  return ResultCode::Ok;
}

SharedFrame::~SharedFrame() { unmap(); }

SharedFrame::SharedFrame(SharedFrame&& other) noexcept
    : fd_(std::move(other.fd_)), address_(other.address_), size_(other.size_) {
  other.address_ = nullptr;
  other.size_ = 0;
}

SharedFrame& SharedFrame::operator=(SharedFrame&& other) noexcept {
  if (this != &other) {
    unmap();
    fd_ = std::move(other.fd_);
    address_ = other.address_;
    size_ = other.size_;
    other.address_ = nullptr;
    other.size_ = 0;
  }
  return *this;
}

void SharedFrame::unmap() {
  if (address_ != nullptr) ::munmap(address_, size_);
  address_ = nullptr;
  size_ = 0;
  fd_.reset();
}

}  // namespace pixcake::preview_channel
