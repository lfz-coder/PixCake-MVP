#pragma once

#include "pixcake/generated/generation_channel.hpp"

#include <sys/types.h>

#include <memory>
#include <atomic>
#include <string>

namespace pixcake::generation_session {

// The main process owns this child process.  The child is deliberately
// provider-neutral: it receives GenerationSubmitMessage over the existing
// channel and chooses a registered adapter on its side.
struct GenerationProcessOptions {
  std::string executable;
  std::string log_directory;
  std::string data_directory;
  bool fixture = false;
  int connect_timeout_ms = 10'000;
};

class GenerationProcess {
 public:
  static ResultCode start(const GenerationProcessOptions& options,
                          std::unique_ptr<GenerationProcess>& out,
                          generated::GenerationClient& client,
                          std::string& diagnostic, const std::atomic<bool>* cancel_requested = nullptr);

  ~GenerationProcess();
  GenerationProcess(const GenerationProcess&) = delete;
  GenerationProcess& operator=(const GenerationProcess&) = delete;

  pid_t pid() const { return pid_; }
  // Returns the child exit code, or -1 while it is still running.
  int wait(int timeout_ms);

 private:
  GenerationProcess(pid_t pid, std::string socket_path)
      : pid_(pid), socket_path_(std::move(socket_path)) {}

  pid_t pid_ = -1;
  std::string socket_path_;
  bool reaped_ = false;
};

}  // namespace pixcake::generation_session
