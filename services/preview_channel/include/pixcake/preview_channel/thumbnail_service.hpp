#pragma once

#include "pixcake/preview_channel/preview_client.hpp"
#include "pixcake/preview_channel/preview_session.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace pixcake::preview_channel {

// Thumbnails for the library grid and the refine strip, from a renderd process
// of their own so a batch never delays the slider preview.  Requests are
// queued in renderd and all answered; a newer request for the same key (e.g.
// after a commit) makes older results for that key stale.  No Qt.
class ThumbnailService {
 public:
  struct Thumbnail {
    std::int64_t key = 0;
    ResultCode code = ResultCode::Ok;
    std::string diagnostic;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t stride = 0;
    std::vector<std::uint8_t> rgba;  // RGBA8, empty unless code is Ok
  };

  static ResultCode start(const std::string& renderd_executable, const std::string& log_directory, int timeout_ms,
                          std::unique_ptr<ThumbnailService>& out, std::string& diagnostic);
  ~ThumbnailService();
  ThumbnailService(const ThumbnailService&) = delete;
  ThumbnailService& operator=(const ThumbnailService&) = delete;

  // Readable when a thumbnail (or the end of the connection) is waiting.
  int fd() const { return client_.fd(); }
  // `document` empty: the original; otherwise EditDocument::encode() bytes.
  ResultCode request(std::int64_t key, const std::string& image_id, const std::string& source_path,
                     std::vector<std::uint8_t> document, std::uint32_t max_edge, std::string& diagnostic,
                     std::vector<std::uint8_t> layer_document = {});
  // One finished thumbnail if waiting; Cancelled when none (stale ones are skipped).
  ResultCode take(Thumbnail& out, std::string& diagnostic);
  ResultCode wait(Thumbnail& out, int timeout_ms, std::string& diagnostic);
  std::size_t pending() const { return pending_.size(); }

 private:
  ThumbnailService() = default;
  ResultCode accept(PreviewFrame frame, Thumbnail& out, std::string& diagnostic);

  PreviewClient client_;
  std::unique_ptr<RenderdProcess> renderd_;
  std::uint64_t next_id_ = 0;
  std::map<std::uint64_t, std::int64_t> pending_;  // request id -> key
  std::map<std::int64_t, std::uint64_t> latest_;   // key -> newest request id
};

}  // namespace pixcake::preview_channel
