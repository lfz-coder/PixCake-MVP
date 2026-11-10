#include "pixcake/generation_session/generation_process.hpp"

#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

extern char** environ;

namespace pixcake::generation_session {
namespace {

std::string socket_path() {
  static std::atomic<unsigned> counter{0};
  const char* tmp = std::getenv("TMPDIR");
  std::string directory = tmp != nullptr && *tmp != '\0' ? tmp : "/tmp";
  if (!directory.empty() && directory.back() == '/') directory.pop_back();
  char name[64];
  std::snprintf(name, sizeof(name), "/pxc-generated-%d-%u.sock", static_cast<int>(::getpid()), counter++);
  if (directory.size() + std::char_traits<char>::length(name) >= 100) directory = "/tmp";
  return directory + name;
}

}  // namespace

ResultCode GenerationProcess::start(const GenerationProcessOptions& options,
                                    std::unique_ptr<GenerationProcess>& out,
                                    generated::GenerationClient& client,
                                    std::string& diagnostic, const std::atomic<bool>* cancel_requested) {
  out.reset();
  client = generated::GenerationClient{};
  if (options.executable.empty() || ::access(options.executable.c_str(), X_OK) != 0) {
    diagnostic = "generated executable not found: " + options.executable;
    return ResultCode::SourceMissing;
  }
  if (options.data_directory.empty()) {
    diagnostic = "generated data directory is empty";
    return ResultCode::StateConflict;
  }
  const std::string socket = socket_path();
  std::vector<std::string> args{options.executable};
  if (options.fixture) args.push_back("--fixture");
  args.push_back("--socket");
  args.push_back(socket);
  args.push_back("--data-dir");
  args.push_back(options.data_directory);
  args.push_back("--log-dir");
  args.push_back(options.log_directory);
  std::vector<char*> argv;
  argv.reserve(args.size() + 1);
  for (auto& arg : args) argv.push_back(arg.data());
  argv.push_back(nullptr);

  pid_t pid = -1;
  if (posix_spawn(&pid, options.executable.c_str(), nullptr, nullptr, argv.data(), environ) != 0) {
    diagnostic = "generated could not be started: " + options.executable;
    return ResultCode::IoFailed;
  }
  std::unique_ptr<GenerationProcess> process(new GenerationProcess(pid, socket));
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(options.connect_timeout_ms);
  for (;;) {
    if (cancel_requested != nullptr && cancel_requested->load(std::memory_order_acquire)) {
      diagnostic = "generation process startup was cancelled";
      return ResultCode::Cancelled;
    }
    if (generated::GenerationClient::connect(socket, client, diagnostic) == ResultCode::Ok) break;
    if (process->wait(0) >= 0) {
      diagnostic = "generated exited before accepting the connection";
      return ResultCode::IoFailed;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      diagnostic = "generated did not open its socket in time";
      return ResultCode::Cancelled;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  out = std::move(process);
  diagnostic.clear();
  return ResultCode::Ok;
}

int GenerationProcess::wait(int timeout_ms) {
  if (reaped_) return 0;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  for (;;) {
    int status = 0;
    const pid_t done = ::waitpid(pid_, &status, WNOHANG);
    if (done == pid_) {
      reaped_ = true;
      return WIFEXITED(status) ? WEXITSTATUS(status)
                               : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    }
    if (done < 0) {
      reaped_ = true;
      return 0;
    }
    if (timeout_ms == 0 || std::chrono::steady_clock::now() >= deadline) return -1;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

GenerationProcess::~GenerationProcess() {
  // Closing the client is done by the owning GenerationClient before this
  // object is destroyed.  The generated process exits when its channel closes.
  if (wait(2000) < 0) {
    ::kill(pid_, SIGKILL);
    int status = 0;
    ::waitpid(pid_, &status, 0);
  }
  ::unlink(socket_path_.c_str());
}

}  // namespace pixcake::generation_session
