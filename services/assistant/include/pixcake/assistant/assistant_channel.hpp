#pragma once

#include "pixcake/preview_channel/channel.hpp"
#include "pixcake/support/result_code.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace pixcake::assistant {

// The sidecar gateway carries one bounded UTF-8 JSON command or reply per
// local frame.  This class only owns framing and the local socket; JSON stays
// in the process adapter above it and never enters kernel/.
inline constexpr std::size_t kMaxAssistantFrameBytes = 256 * 1024;

class AssistantChannel final {
 public:
  static ResultCode connect(const std::string& socket_path, AssistantChannel& out, std::string& diagnostic);

  explicit AssistantChannel(preview_channel::Channel channel =
                                preview_channel::Channel(preview_channel::UniqueFd{}))
      : channel_(std::move(channel)) {}

  bool connected() const { return channel_.valid(); }
  int fd() const { return channel_.fd(); }

  // Sends one complete sidecar payload.  The payload is opaque to this
  // target; the process adapter decides whether it is JSON or another codec.
  ResultCode send(const std::vector<std::uint8_t>& payload, std::string& diagnostic);
  ResultCode receive(std::vector<std::uint8_t>& payload, int timeout_ms, std::string& diagnostic);

 private:
  preview_channel::Channel channel_;
};

}  // namespace pixcake::assistant
