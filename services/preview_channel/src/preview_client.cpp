#include "pixcake/preview_channel/preview_client.hpp"

#include <cstring>

namespace pixcake::preview_channel {

ResultCode PreviewClient::connect(const std::string& socket_path, PreviewClient& out, std::string& diagnostic) {
  UniqueFd fd;
  const ResultCode code = connect_to(socket_path, fd, diagnostic);
  if (code != ResultCode::Ok) return code;
  out = PreviewClient(Channel{std::move(fd)});
  return ResultCode::Ok;
}

ResultCode PreviewClient::send(ResultCode encoded, const std::vector<std::uint8_t>& bytes, std::string& diagnostic) {
  if (encoded != ResultCode::Ok) {
    diagnostic = "preview message is invalid and was not sent";
    return encoded;
  }
  return channel_.send(bytes, -1, diagnostic);
}

ResultCode PreviewClient::send_source(const ipc::PreviewSourceMessage& message, std::string& diagnostic) {
  std::vector<std::uint8_t> bytes;
  const ResultCode encoded = ipc::encode_preview_source(message, bytes);
  return send(encoded, bytes, diagnostic);
}

ResultCode PreviewClient::send_request(const ipc::PreviewRequestMessage& message, std::string& diagnostic) {
  std::vector<std::uint8_t> bytes;
  const ResultCode encoded = ipc::encode_preview_request(message, bytes);
  return send(encoded, bytes, diagnostic);
}

ResultCode PreviewClient::send_cancel(const ipc::PreviewCancelMessage& message, std::string& diagnostic) {
  std::vector<std::uint8_t> bytes;
  const ResultCode encoded = ipc::encode_preview_cancel(message, bytes);
  return send(encoded, bytes, diagnostic);
}

ResultCode PreviewClient::send_thumbnail(const ipc::ThumbnailRequestMessage& message, std::string& diagnostic) {
  std::vector<std::uint8_t> bytes;
  const ResultCode encoded = ipc::encode_thumbnail_request(message, bytes);
  return send(encoded, bytes, diagnostic);
}

ResultCode PreviewClient::receive(PreviewFrame& out, int timeout_ms, std::string& diagnostic) {
  out = {};
  std::vector<std::uint8_t> bytes;
  UniqueFd fd;
  ResultCode code = channel_.receive(bytes, fd, ipc::kMaxPreviewResultBytes, timeout_ms, diagnostic);
  if (code != ResultCode::Ok) return code;
  ipc::PreviewMessageKind kind{};
  if (ipc::peek_preview_message_kind(bytes, kind) != ResultCode::Ok ||
      (kind != ipc::PreviewMessageKind::Result && kind != ipc::PreviewMessageKind::ThumbnailResult)) {
    diagnostic = "renderd sent an unexpected message";
    return ResultCode::StateConflict;
  }
  out.thumbnail = kind == ipc::PreviewMessageKind::ThumbnailResult;
  ipc::PreviewResultMessage result;
  code = out.thumbnail ? ipc::decode_thumbnail_result(bytes, result) : ipc::decode_preview_result(bytes, result);
  if (code != ResultCode::Ok) {
    diagnostic = "renderd sent a malformed preview result";
    return code;
  }
  if (result.code == ResultCode::Ok) {
    const std::size_t size = static_cast<std::size_t>(result.header.stride) * result.header.height;
    SharedFrame frame;
    code = SharedFrame::map(std::move(fd), size, frame, diagnostic);
    if (code != ResultCode::Ok) return code;
    out.rgba.assign(frame.data(), frame.data() + size);
  }
  out.result = std::move(result);
  diagnostic.clear();
  return ResultCode::Ok;
}

}  // namespace pixcake::preview_channel
