#include "pixcake/preview_channel/thumbnail_service.hpp"

#include <algorithm>
#include <chrono>

namespace pixcake::preview_channel {

ResultCode ThumbnailService::start(const std::string& renderd_executable, const std::string& log_directory,
                                   int timeout_ms, std::unique_ptr<ThumbnailService>& out, std::string& diagnostic) {
  out.reset();
  std::unique_ptr<ThumbnailService> service(new ThumbnailService());
  const ResultCode code =
      RenderdProcess::start(renderd_executable, log_directory, timeout_ms, service->renderd_, service->client_, diagnostic);
  if (code != ResultCode::Ok) return code;
  out = std::move(service);
  return ResultCode::Ok;
}

ThumbnailService::~ThumbnailService() {
  client_ = PreviewClient{};  // renderd sees the connection end and exits
  renderd_.reset();
}

ResultCode ThumbnailService::request(std::int64_t key, const std::string& image_id, const std::string& source_path,
                                     std::vector<std::uint8_t> document, std::uint32_t max_edge,
                                     std::string& diagnostic) {
  ipc::ThumbnailRequestMessage message;
  message.request_id = ++next_id_;
  message.image_id = image_id;
  message.source_path = source_path;
  message.max_edge = max_edge;
  message.document = std::move(document);
  const ResultCode code = client_.send_thumbnail(message, diagnostic);
  if (code != ResultCode::Ok) return code;
  pending_[message.request_id] = key;
  latest_[key] = message.request_id;
  return ResultCode::Ok;
}

ResultCode ThumbnailService::accept(PreviewFrame frame, Thumbnail& out, std::string& diagnostic) {
  const auto found = pending_.find(frame.result.client_sequence);
  if (!frame.thumbnail || found == pending_.end()) {
    diagnostic = "unexpected result on the thumbnail channel";
    return ResultCode::Cancelled;
  }
  const std::int64_t key = found->second;
  pending_.erase(found);
  if (latest_[key] != frame.result.client_sequence) {
    diagnostic = "stale thumbnail";
    return ResultCode::Cancelled;
  }
  out = {};
  out.key = key;
  out.code = frame.result.code;
  out.diagnostic = frame.result.diagnostic;
  if (out.code == ResultCode::Ok) {
    out.width = frame.result.header.width;
    out.height = frame.result.header.height;
    out.stride = frame.result.header.stride;
    out.rgba = std::move(frame.rgba);
  }
  diagnostic.clear();
  return ResultCode::Ok;
}

ResultCode ThumbnailService::take(Thumbnail& out, std::string& diagnostic) {
  for (;;) {
    PreviewFrame frame;
    const ResultCode code = client_.receive(frame, 0, diagnostic);
    if (code != ResultCode::Ok) return code;
    if (accept(std::move(frame), out, diagnostic) == ResultCode::Ok) return ResultCode::Ok;
  }
}

ResultCode ThumbnailService::wait(Thumbnail& out, int timeout_ms, std::string& diagnostic) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  for (;;) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
    PreviewFrame frame;
    const ResultCode code = client_.receive(frame, static_cast<int>(std::max<long long>(0, left.count())), diagnostic);
    if (code != ResultCode::Ok) return code;
    if (accept(std::move(frame), out, diagnostic) == ResultCode::Ok) return ResultCode::Ok;
  }
}

}  // namespace pixcake::preview_channel
