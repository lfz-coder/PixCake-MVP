#pragma once

#include "pixcake/ipc/preview_protocol.hpp"
#include "pixcake/preview_channel/channel.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pixcake::preview_channel {

// A preview frame as the shell receives it: the result message plus the
// pixels copied out of renderd's shared memory (header.stride * height bytes,
// RGBA8).  rgba is empty unless result.code is Ok.
struct PreviewFrame {
  ipc::PreviewResultMessage result;
  std::vector<std::uint8_t> rgba;
  // A thumbnail result: result.client_sequence is the thumbnail request id.
  bool thumbnail = false;
};

// The shell's side of the preview channel.  No Qt; the shell turns frames
// into images itself.  Not thread-safe.
class PreviewClient {
 public:
  static ResultCode connect(const std::string& socket_path, PreviewClient& out, std::string& diagnostic);
  explicit PreviewClient(Channel channel = Channel{UniqueFd{}}) : channel_(std::move(channel)) {}
  bool connected() const { return channel_.valid(); }
  // Readable when a result (or the end of the connection) is waiting.
  int fd() const { return channel_.fd(); }

  ResultCode send_source(const ipc::PreviewSourceMessage& message, std::string& diagnostic);
  ResultCode send_request(const ipc::PreviewRequestMessage& message, std::string& diagnostic);
  ResultCode send_cancel(const ipc::PreviewCancelMessage& message, std::string& diagnostic);
  ResultCode send_thumbnail(const ipc::ThumbnailRequestMessage& message, std::string& diagnostic);

  // Waits up to timeout_ms for the next result (Cancelled on timeout).  A
  // frame whose shared memory is missing or smaller than its header is
  // StateConflict.
  ResultCode receive(PreviewFrame& out, int timeout_ms, std::string& diagnostic);

 private:
  ResultCode send(ResultCode encoded, const std::vector<std::uint8_t>& bytes, std::string& diagnostic);
  Channel channel_;
};

}  // namespace pixcake::preview_channel
