#pragma once

#include "pixcake/generated/generation_service.hpp"
#include "pixcake/preview_channel/channel.hpp"

#include <atomic>
#include <string>
#include <utility>
#include <vector>

namespace pixcake::generated {

// Process-side bridge for the provider-neutral generation messages.  The
// channel only moves encoded kernel/ipc bytes; it never sees a provider URL or
// JSON field.
class GenerationServer {
 public:
  explicit GenerationServer(GenerationService& service) : service_(service) {}

  // Handle one request.  A submit produces one progress response; cancel and
  // result-confirm are one-way messages.  Cancelled is returned when the
  // channel timeout expires.
  ResultCode serve_once(preview_channel::Channel& channel, int timeout_ms, std::string& diagnostic);
  ResultCode serve(preview_channel::Channel& channel, int timeout_ms, std::string& diagnostic);

 private:
  GenerationService& service_;
};

// Main-process side of the same bridge.  This is intentionally independent of
// Qt so it can later be used by a command/controller or by an integration test.
class GenerationClient {
 public:
  static ResultCode connect(const std::string& socket_path, GenerationClient& out,
                            std::string& diagnostic);
  explicit GenerationClient(preview_channel::Channel channel = preview_channel::Channel{preview_channel::UniqueFd{}})
      : channel_(std::move(channel)) {}

  bool connected() const { return channel_.valid(); }
  int fd() const { return channel_.fd(); }

  ResultCode submit(const generate::GenerationTask& task, ipc::GenerationProgressMessage& progress,
                    int timeout_ms, std::string& diagnostic,
                    const std::atomic<bool>* cancel_requested = nullptr);
  ResultCode cancel(const std::string& task_id, std::string& diagnostic);
  ResultCode confirm(const ipc::GenerationResultConfirmMessage& message, std::string& diagnostic);

 private:
  ResultCode send(ResultCode encoded, const std::vector<std::uint8_t>& bytes, std::string& diagnostic);
  preview_channel::Channel channel_;
};

}  // namespace pixcake::generated
