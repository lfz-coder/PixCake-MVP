#include "pixcake/preview_channel/export_client.hpp"

#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

extern char** environ;

namespace pixcake::preview_channel {
namespace {

// Unix socket paths are limited to 104 bytes (see preview_session.cpp).
std::string socket_path() {
  static std::atomic<unsigned> counter{0};
  const char* tmp = std::getenv("TMPDIR");
  std::string directory = tmp != nullptr && *tmp != '\0' ? tmp : "/tmp";
  if (directory.back() == '/') directory.pop_back();
  char name[64];
  std::snprintf(name, sizeof(name), "/pxc-exportd-%d-%u.sock", static_cast<int>(::getpid()), counter++);
  if (directory.size() + std::char_traits<char>::length(name) >= 100) directory = "/tmp";
  return directory + name;
}

// Exit status, or -1 while running (0 once reaped elsewhere).
int reap(pid_t pid, int timeout_ms) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  for (;;) {
    int status = 0;
    const pid_t done = ::waitpid(pid, &status, WNOHANG);
    if (done == pid) return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
    if (done < 0) return 0;
    if (std::chrono::steady_clock::now() >= deadline) return -1;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

}  // namespace

ExportClient::ExportClient(pid_t pid, std::string socket, Channel channel)
    : pid_(pid), socket_(std::move(socket)), channel_(std::move(channel)) {}

ResultCode ExportClient::start(const std::string& executable, const std::string& log_directory,
                               const std::string& data_directory, int timeout_ms,
                               std::unique_ptr<ExportClient>& out, std::string& diagnostic) {
  out.reset();
  if (::access(executable.c_str(), X_OK) != 0) {
    diagnostic = "exportd executable not found: " + executable;
    return ResultCode::SourceMissing;
  }
  const std::string socket = socket_path();
  std::vector<std::string> args = {executable, "--socket", socket, "--log-dir", log_directory,
                                   "--data-dir", data_directory};
  std::vector<char*> argv;
  for (auto& arg : args) argv.push_back(arg.data());
  argv.push_back(nullptr);
  pid_t pid = -1;
  if (posix_spawn(&pid, executable.c_str(), nullptr, nullptr, argv.data(), environ) != 0) {
    diagnostic = "exportd could not be started: " + executable;
    return ResultCode::IoFailed;
  }
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  for (;;) {
    UniqueFd connection;
    if (connect_to(socket, connection, diagnostic) == ResultCode::Ok) {
      out.reset(new ExportClient(pid, socket, Channel(std::move(connection))));
      diagnostic.clear();
      return ResultCode::Ok;
    }
    if (reap(pid, 0) >= 0) {
      diagnostic = "exportd exited before accepting the export connection";
      return ResultCode::IoFailed;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      ::kill(pid, SIGKILL);
      reap(pid, 1000);
      ::unlink(socket.c_str());
      diagnostic = "exportd did not open its export socket in time";
      return ResultCode::Cancelled;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

ExportClient::~ExportClient() {
  // Closing the socket ends exportd's session.  A running export finishes its
  // current task first, so wait a little longer than renderd before killing.
  channel_ = Channel(UniqueFd());
  if (reap(pid_, 5000) < 0) {
    ::kill(pid_, SIGKILL);
    reap(pid_, 1000);
  }
  ::unlink(socket_.c_str());
}

ResultCode ExportClient::send(const std::vector<std::uint8_t>& bytes, std::string& diagnostic) {
  return channel_.send(bytes, -1, diagnostic);
}

ResultCode ExportClient::send_start(const ipc::ExportStartMessage& message, std::string& diagnostic) {
  std::vector<std::uint8_t> bytes;
  if (ipc::encode_export_start(message, bytes) != ResultCode::Ok) {
    diagnostic = "export start message rejected";
    return ResultCode::StateConflict;
  }
  return send(bytes, diagnostic);
}

ResultCode ExportClient::send_cancel(const std::string& export_id, std::string& diagnostic) {
  ipc::TaskCancelMessage cancel;
  cancel.kind = ipc::TaskKind::Export;
  cancel.task_id = export_id;
  std::vector<std::uint8_t> bytes;
  if (ipc::encode_task_cancel(cancel, bytes) != ResultCode::Ok) {
    diagnostic = "export cancel message rejected";
    return ResultCode::StateConflict;
  }
  return send(bytes, diagnostic);
}

ResultCode ExportClient::receive(ipc::ExportProgressMessage& out, int timeout_ms, std::string& diagnostic) {
  for (;;) {
    std::vector<std::uint8_t> bytes;
    UniqueFd ignored;
    const ResultCode received = channel_.receive(bytes, ignored, ipc::kMaxProcessMessageBytes, timeout_ms, diagnostic);
    if (received != ResultCode::Ok) return received;
    if (!ipc::is_export_progress(bytes)) continue;  // nothing else is expected from exportd
    if (ipc::decode_export_progress(bytes, out) == ResultCode::Ok) return ResultCode::Ok;
  }
}

}  // namespace pixcake::preview_channel
