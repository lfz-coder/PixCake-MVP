#include "pixcake/assistant/assistant_channel.hpp"

namespace pixcake::assistant {

ResultCode AssistantChannel::connect(const std::string& socket_path, AssistantChannel& out,
                                     std::string& diagnostic) {
  preview_channel::UniqueFd fd;
  const ResultCode code = preview_channel::connect_to(socket_path, fd, diagnostic);
  if (code != ResultCode::Ok) return code;
  out = AssistantChannel(preview_channel::Channel(std::move(fd)));
  return ResultCode::Ok;
}

ResultCode AssistantChannel::send(const std::vector<std::uint8_t>& payload, std::string& diagnostic) {
  if (payload.size() > kMaxAssistantFrameBytes) {
    diagnostic = "assistant frame is too large";
    return ResultCode::StateConflict;
  }
  return channel_.send(payload, -1, diagnostic);
}

ResultCode AssistantChannel::receive(std::vector<std::uint8_t>& payload, int timeout_ms,
                                     std::string& diagnostic) {
  preview_channel::UniqueFd attached_fd;
  return channel_.receive(payload, attached_fd, kMaxAssistantFrameBytes, timeout_ms, diagnostic);
}

}  // namespace pixcake::assistant
