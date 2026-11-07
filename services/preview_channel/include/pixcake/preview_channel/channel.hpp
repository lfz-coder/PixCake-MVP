#pragma once

#include "pixcake/support/result_code.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pixcake::preview_channel {

// Local transport between the shell and renderd (evidence
// docs/superpowers/evidence/2026-11-06-renderd-transport-and-decoder.md §3.1):
// a Unix stream socket carrying "u32 little-endian length + kernel/ipc message
// bytes", optionally with one file descriptor (SCM_RIGHTS) for a shared-memory
// frame.  No Qt, no kernel types: bytes in, bytes out.

// Owns a file descriptor; closes it on destruction.
class UniqueFd {
 public:
  UniqueFd() = default;
  explicit UniqueFd(int fd) : fd_(fd) {}
  ~UniqueFd();
  UniqueFd(UniqueFd&& other) noexcept : fd_(other.release()) {}
  UniqueFd& operator=(UniqueFd&& other) noexcept;
  UniqueFd(const UniqueFd&) = delete;
  UniqueFd& operator=(const UniqueFd&) = delete;
  int get() const { return fd_; }
  bool valid() const { return fd_ >= 0; }
  int release();
  void reset(int fd = -1);

 private:
  int fd_ = -1;
};

// Listening socket at `path` (replaces a stale socket file; refuses another
// kind of file).  Mode 0600.
ResultCode listen_at(const std::string& path, UniqueFd& out, std::string& diagnostic);
// Waits up to timeout_ms (-1: forever) and accepts one connection from the
// same user (getpeereid).  Cancelled on timeout; PermissionDenied for another user.
ResultCode accept_same_user(int listen_fd, int timeout_ms, UniqueFd& out, std::string& diagnostic);
ResultCode connect_to(const std::string& path, UniqueFd& out, std::string& diagnostic);

// One connected endpoint.  Not thread-safe.
class Channel {
 public:
  // Sets SO_NOSIGPIPE at once: macOS refuses socket options after the peer closed.
  explicit Channel(UniqueFd socket);
  bool valid() const { return socket_.valid(); }
  int fd() const { return socket_.get(); }

  // Sends one message; `attached_fd` (>= 0) travels with it and stays owned by
  // the caller.  IoFailed when the peer is gone.
  ResultCode send(const std::vector<std::uint8_t>& message, int attached_fd, std::string& diagnostic);
  // Waits up to timeout_ms (-1: forever, 0: poll).  Cancelled on timeout,
  // IoFailed when the peer closed, StateConflict for a length above max_bytes
  // (the channel is then unusable: the stream position is lost).
  ResultCode receive(std::vector<std::uint8_t>& message, UniqueFd& attached_fd, std::size_t max_bytes,
                     int timeout_ms, std::string& diagnostic);
  // True when a message (or EOF) is ready to read without blocking.
  bool readable(int timeout_ms) const;

 private:
  UniqueFd socket_;
};

// Anonymous shared memory for one frame: created, unlinked at once, sized,
// mapped read-write.  The fd is what crosses the socket.
class SharedFrame {
 public:
  static ResultCode create(std::size_t size, SharedFrame& out, std::string& diagnostic);
  // Maps a received fd read-only; fails when the object is smaller than `size`.
  static ResultCode map(UniqueFd fd, std::size_t size, SharedFrame& out, std::string& diagnostic);
  SharedFrame() = default;
  ~SharedFrame();
  SharedFrame(SharedFrame&& other) noexcept;
  SharedFrame& operator=(SharedFrame&& other) noexcept;
  SharedFrame(const SharedFrame&) = delete;
  SharedFrame& operator=(const SharedFrame&) = delete;

  std::uint8_t* data() { return static_cast<std::uint8_t*>(address_); }
  const std::uint8_t* data() const { return static_cast<const std::uint8_t*>(address_); }
  std::size_t size() const { return size_; }
  int fd() const { return fd_.get(); }

 private:
  void unmap();
  UniqueFd fd_;
  void* address_ = nullptr;
  std::size_t size_ = 0;
};

}  // namespace pixcake::preview_channel
