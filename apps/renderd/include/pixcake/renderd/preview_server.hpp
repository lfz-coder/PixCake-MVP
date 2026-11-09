#pragma once

#include "pixcake/editdoc/edit_document.hpp"
#include "pixcake/ipc/preview_protocol.hpp"
#include "pixcake/preview_channel/channel.hpp"
#include "pixcake/renderd/image_io.hpp"
#include "pixcake/renderd/render_service.hpp"
#include "pixcake/support/log_sink.hpp"

#include <deque>
#include <optional>
#include <string>

namespace pixcake::renderd {

// Source images are refused above these sizes before decoding; preview output
// is at most 4000 on the longest edge.
DecodeLimits preview_decode_limits();

// renderd's side of the preview channel (evidence
// 2026-11-06-renderd-transport-and-decoder.md §3.2).  Holds the current source
// (decoded preview image + committed document) and answers preview requests
// with frames in shared memory.  Single-threaded; one connection at a time.
class PreviewServer {
 public:
  PreviewServer(RenderService& service, ImageDecoder* decoder, LogSink* log = nullptr);

  // Serves until the peer closes (Ok) or the channel breaks (IoFailed /
  // StateConflict for an oversized or malformed stream).
  ResultCode serve(preview_channel::Channel& channel, std::string& diagnostic);

  // One round: reads every message already waiting (blocking up to
  // timeout_ms for the first), keeps the latest edit request and latest source
  // request independently, renders both when present, then renders at most one
  // queued thumbnail.  A source request therefore cannot discard an edit frame
  // that was already waiting in the channel. Cancelled when nothing arrived and
  // no thumbnail was waiting. For tests.
  ResultCode step(preview_channel::Channel& channel, int timeout_ms, std::string& diagnostic);

  std::size_t decodes() const { return decodes_; }
  std::size_t queued_thumbnails() const { return thumbnails_.size(); }

 private:
  struct Source {
    ipc::PreviewSourceMessage message;
    std::optional<EditDocument> document;
    ResultCode code = ResultCode::Ok;  // why the source is unusable, if not Ok
    std::string diagnostic;
    std::uint32_t decoded_edge = 0;
    std::optional<RgbaInput> image;
    // The file as it was when the source arrived; decoded pixels are reused
    // only for the same path, size and modification time.
    std::uint64_t file_size = 0;
    std::int64_t file_mtime_ns = 0;
  };

  void accept_source(ipc::PreviewSourceMessage message);
  ipc::PreviewResultMessage render_thumbnail(const ipc::ThumbnailRequestMessage& request, PreviewResult& frame);
  // Puts the frame in shared memory and sends the result (preview or thumbnail kind).
  ResultCode send_result(preview_channel::Channel& channel, ipc::PreviewResultMessage result,
                         const PreviewResult& frame, bool thumbnail, const std::string& subject,
                         std::uint64_t duration_ms, std::string& diagnostic);
  ipc::PreviewResultMessage render(const ipc::PreviewRequestMessage& request, PreviewResult& frame);
  void note(const char* event, ResultCode code, const std::string& message, std::uint64_t duration_ms = 0) const;

  RenderService& service_;
  ImageDecoder* decoder_;
  LogSink* log_;
  std::optional<Source> source_;
  std::deque<ipc::ThumbnailRequestMessage> thumbnails_;
  std::size_t decodes_ = 0;
};

}  // namespace pixcake::renderd
