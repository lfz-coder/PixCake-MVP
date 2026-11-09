#include "pixcake/renderd/preview_server.hpp"

#include "pixcake/render/render_description.hpp"

#include <sys/stat.h>

#include <chrono>
#include <cstring>
#include <array>

namespace pixcake::renderd {
namespace {

constexpr std::size_t kMaxIncomingBytes = ipc::kMaxPreviewSourceBytes + 1024;

std::string single_line(std::string text) {
  for (char& c : text) {
    if (c == '\n' || c == '\t' || c == '\0') c = ' ';
  }
  if (text.size() > 2000) text.resize(2000);
  return text;
}

}  // namespace

DecodeLimits preview_decode_limits() { return {30000, 30000, 300'000'000, 4000ull * 4000ull * 4ull}; }

PreviewServer::PreviewServer(RenderService& service, ImageDecoder* decoder, LogSink* log)
    : service_(service), decoder_(decoder), log_(log) {}

void PreviewServer::note(const char* event, ResultCode code, const std::string& message,
                         std::uint64_t duration_ms) const {
  if (log_ == nullptr) return;
  LogRecord record;
  record.module = "renderd";
  record.event = event;
  record.code = code;
  record.message = message;
  record.duration_ms = duration_ms;
  log_->write(record);
}

void PreviewServer::accept_source(ipc::PreviewSourceMessage message) {
  Source source;
  source.document = EditDocument::decode(message.document);
  if (!source.document) {
    source.code = ResultCode::DocumentCorrupt;
    source.diagnostic = "preview source document cannot be decoded";
  } else if (source.document->image_id() != message.image_id ||
             source.document->revision() != message.document_revision) {
    // The editing instance id is not part of the encoded document (EditDocument
    // keeps it in memory: the document path once loaded), so the source message
    // is its authority; image id and revision are checked against the bytes.
    source.code = ResultCode::StateConflict;
    source.diagnostic = "preview source identity differs from its document";
  }
  struct stat info {};
  if (::stat(message.source_path.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) {
    if (source.code == ResultCode::Ok) {
      source.code = ResultCode::SourceMissing;
      source.diagnostic = "source image does not exist: " + message.source_path;
    }
  } else {
    source.file_size = static_cast<std::uint64_t>(info.st_size);
    source.file_mtime_ns = static_cast<std::int64_t>(info.st_mtimespec.tv_sec) * 1'000'000'000 +
                           info.st_mtimespec.tv_nsec;
  }
  // Same unchanged file as before: keep the decoded pixels.
  if (source.code == ResultCode::Ok && source_ && source_->image &&
      source_->message.source_path == message.source_path && source_->message.image_id == message.image_id &&
      source_->file_size == source.file_size && source_->file_mtime_ns == source.file_mtime_ns) {
    source.image = std::move(source_->image);
    source.decoded_edge = source_->decoded_edge;
  }
  source.message = std::move(message);
  note("renderd.source", source.code, source.code == ResultCode::Ok ? source.message.image_id : source.diagnostic);
  source_ = std::move(source);
}

ipc::PreviewResultMessage PreviewServer::render(const ipc::PreviewRequestMessage& message, PreviewResult& frame) {
  ipc::PreviewResultMessage result;
  result.client_sequence = message.client_sequence;
  auto fail = [&](ResultCode code, std::string why) {
    result.code = code;
    result.diagnostic = single_line(std::move(why));
    return result;
  };
  const auto& request = message.request;
  if (!source_) return fail(ResultCode::StateConflict, "no preview source was sent before the request");
  Source& source = *source_;
  if (source.code != ResultCode::Ok) return fail(source.code, source.diagnostic);
  if (request.image_id != source.message.image_id ||
      request.editing_instance_id != source.message.editing_instance_id ||
      request.base_revision != source.message.document_revision) {
    return fail(ResultCode::StateConflict, "preview request does not match the current source or revision");
  }

  if (!source.image || source.decoded_edge != request.max_edge) {
    if (decoder_ == nullptr) return fail(ResultCode::UnsupportedFormat, "no image decoder is built");
    DecodeRequest decode;
    decode.source_path = source.message.source_path;
    decode.limits = preview_decode_limits();
    decode.max_edge = request.max_edge;
    RgbaInput image;
    std::string why;
    const auto start = std::chrono::steady_clock::now();
    const ResultCode code = decoder_->decode(decode, image, why);
    ++decodes_;
    const auto ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
    note("renderd.decode", code, code == ResultCode::Ok ? decode.source_path : why, ms);
    source.image.reset();
    if (code != ResultCode::Ok) return fail(code, why);
    source.image = std::move(image);
    source.decoded_edge = request.max_edge;
  }

  render::RenderDescription description;
  if (request.mode == preview::PreviewMode::Original) {
    // Keep the source identity and revision for the normal render contract,
    // but leave the operation list empty so render_now copies source pixels.
    description.image_id = source.document->image_id();
    description.editing_instance_id = source.message.editing_instance_id;
    description.document_revision = source.document->revision();
  } else {
    EditDocument document = *source.document;
    if (document.set_preview(request.capability_id, request.parameter_id, request.display_value) != ResultCode::Ok) {
      return fail(ResultCode::StateConflict, "the parameter cannot be previewed: " + request.capability_id + "/" +
                                                 request.parameter_id);
    }
    auto compiled = render::compile(document);
    if (compiled.code != ResultCode::Ok) return fail(compiled.code, "render description cannot be compiled");
    description = std::move(compiled.description);
    description.editing_instance_id = source.message.editing_instance_id;
  }

  PreviewWork work;
  work.client_sequence = message.client_sequence;
  work.request = request;
  work.description = std::move(description);
  work.input = *source.image;
  std::string why;
  const ResultCode submitted = service_.submit(std::move(work), why);
  if (submitted != ResultCode::Ok) return fail(submitted, why);
  frame = service_.render_latest();
  if (frame.code != ResultCode::Ok) return fail(frame.code, frame.diagnostic);
  result.code = ResultCode::Ok;
  result.header = frame.header;
  for (const auto& d : frame.diagnostics) {
    if (result.diagnostics.size() == 64) break;
    result.diagnostics.push_back({single_line(d.capability_id), single_line(d.reason.empty() ? "-" : d.reason)});
  }
  return result;
}

ResultCode PreviewServer::step(preview_channel::Channel& channel, int timeout_ms, std::string& diagnostic) {
  // Edit and baseline requests share the channel, but are independent display
  // products. Keeping one latest request for each mode means pressing to view
  // the original cannot cancel an edit frame that is already queued; release
  // can still consume that edit frame immediately.
  std::optional<ipc::PreviewRequestMessage> latest_edit;
  std::optional<ipc::PreviewRequestMessage> latest_original;
  bool any = false;
  int wait = timeout_ms;
  for (;;) {
    std::vector<std::uint8_t> bytes;
    preview_channel::UniqueFd ignored;
    const ResultCode received = channel.receive(bytes, ignored, kMaxIncomingBytes, wait, diagnostic);
    if (received == ResultCode::Cancelled) break;
    if (received != ResultCode::Ok) {
      if (any) break;  // render what arrived; the next step reports the broken channel
      return received;
    }
    any = true;
    wait = 0;  // drain what is already there, then render
    ipc::PreviewMessageKind kind{};
    if (ipc::peek_preview_message_kind(bytes, kind) != ResultCode::Ok) {
      note("renderd.message", ResultCode::UnsupportedFormat, "unknown message ignored");
      continue;
    }
    if (kind == ipc::PreviewMessageKind::Source) {
      ipc::PreviewSourceMessage source;
      const ResultCode code = ipc::decode_preview_source(bytes, source);
      if (code == ResultCode::Ok) {
        accept_source(std::move(source));
      } else {
        source_.reset();
        note("renderd.source", code, "malformed preview source");
      }
    } else if (kind == ipc::PreviewMessageKind::Request) {
      ipc::PreviewRequestMessage request;
      const ResultCode code = ipc::decode_preview_request(bytes, request);
      if (code == ResultCode::Ok) {
        if (request.request.mode == preview::PreviewMode::Original) {
          latest_original = std::move(request);
        } else {
          latest_edit = std::move(request);  // latest edit wins; replaced requests get no result
        }
      } else {
        note("renderd.request", code, "malformed preview request");
      }
    } else if (kind == ipc::PreviewMessageKind::ThumbnailRequest) {
      ipc::ThumbnailRequestMessage thumbnail;
      const ResultCode code = ipc::decode_thumbnail_request(bytes, thumbnail);
      if (code == ResultCode::Ok) {
        thumbnails_.push_back(std::move(thumbnail));  // queued: every thumbnail is answered
      } else {
        note("renderd.thumbnail", code, "malformed thumbnail request");
      }
    } else if (kind == ipc::PreviewMessageKind::Cancel) {
      ipc::PreviewCancelMessage cancel;
      if (ipc::decode_preview_cancel(bytes, cancel) == ResultCode::Ok) {
        const auto matches = [&cancel](const auto& candidate) {
          return candidate && candidate->client_sequence == cancel.client_sequence &&
                 candidate->request.image_id == cancel.image_id &&
                 candidate->request.editing_instance_id == cancel.editing_instance_id &&
                 candidate->request.base_revision == cancel.base_revision;
        };
        if (matches(latest_edit)) latest_edit.reset();
        if (matches(latest_original)) latest_original.reset();
      }
    }
  }
  if (!any && thumbnails_.empty()) return ResultCode::Cancelled;

  // Preserve client sequence order when both products were queued. This keeps
  // PreviewSession's normal stale-frame discard rule valid even if a baseline
  // request arrived just before a newer edit request.
  std::array<const ipc::PreviewRequestMessage*, 2> requests{};
  std::size_t request_count = 0;
  if (latest_edit) requests[request_count++] = &*latest_edit;
  if (latest_original) requests[request_count++] = &*latest_original;
  if (request_count == 2 && requests[1]->client_sequence < requests[0]->client_sequence) {
    std::swap(requests[0], requests[1]);
  }
  for (std::size_t index = 0; index < request_count; ++index) {
    const auto& request = *requests[index];
    const auto start = std::chrono::steady_clock::now();
    PreviewResult frame;
    ipc::PreviewResultMessage result = render(request, frame);
    const auto ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
    if (const ResultCode sent = send_result(channel, std::move(result), frame, false, request.request.image_id, ms,
                                            diagnostic);
        sent != ResultCode::Ok) {
      return sent;
    }
  }
  if (!thumbnails_.empty()) {
    const ipc::ThumbnailRequestMessage request = std::move(thumbnails_.front());
    thumbnails_.pop_front();
    const auto start = std::chrono::steady_clock::now();
    PreviewResult frame;
    ipc::PreviewResultMessage result = render_thumbnail(request, frame);
    const auto ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
    if (const ResultCode sent = send_result(channel, std::move(result), frame, true, request.image_id, ms, diagnostic);
        sent != ResultCode::Ok) {
      return sent;
    }
  }
  diagnostic.clear();
  return ResultCode::Ok;
}

ipc::PreviewResultMessage PreviewServer::render_thumbnail(const ipc::ThumbnailRequestMessage& request,
                                                          PreviewResult& frame) {
  ipc::PreviewResultMessage result;
  result.client_sequence = request.request_id;
  auto fail = [&](ResultCode code, std::string why) {
    result.code = code;
    result.diagnostic = single_line(std::move(why));
    return result;
  };
  if (decoder_ == nullptr) return fail(ResultCode::UnsupportedFormat, "no image decoder is built");
  render::RenderDescription description;
  description.image_id = request.image_id;
  if (!request.document.empty()) {
    const auto document = EditDocument::decode(request.document);
    if (!document) return fail(ResultCode::DocumentCorrupt, "thumbnail document cannot be decoded");
    auto compiled = render::compile(*document);
    if (compiled.code != ResultCode::Ok) return fail(compiled.code, "render description cannot be compiled");
    description = std::move(compiled.description);
  }
  DecodeRequest decode;
  decode.source_path = request.source_path;
  decode.limits = preview_decode_limits();
  decode.max_edge = request.max_edge;
  RgbaInput image;
  std::string why;
  const ResultCode decoded = decoder_->decode(decode, image, why);
  ++decodes_;
  if (decoded != ResultCode::Ok) return fail(decoded, why);
  frame = service_.render_now(request.request_id, request.image_id, description, image, request.max_edge);
  if (frame.code != ResultCode::Ok) return fail(frame.code, frame.diagnostic);
  result.code = ResultCode::Ok;
  result.header = frame.header;
  return result;
}

ResultCode PreviewServer::send_result(preview_channel::Channel& channel, ipc::PreviewResultMessage result,
                                      const PreviewResult& frame, bool thumbnail, const std::string& subject,
                                      std::uint64_t duration_ms, std::string& diagnostic) {
  preview_channel::SharedFrame shared;
  if (result.code == ResultCode::Ok) {
    const std::size_t size = static_cast<std::size_t>(frame.header.stride) * frame.header.height;
    std::string why;
    const ResultCode created = preview_channel::SharedFrame::create(size, shared, why);
    if (created != ResultCode::Ok) {
      const auto sequence = result.client_sequence;
      result = {};
      result.client_sequence = sequence;
      result.code = created;
      result.diagnostic = single_line(why);
    } else {
      std::memcpy(shared.data(), frame.rgba.data(), size);
    }
  }
  const auto encode = thumbnail ? ipc::encode_thumbnail_result : ipc::encode_preview_result;
  std::vector<std::uint8_t> bytes;
  ResultCode encoded = encode(result, bytes);
  if (encoded != ResultCode::Ok) {
    // Diagnostics too long for one message: send the frame without them.
    result.diagnostics.clear();
    encoded = encode(result, bytes);
  }
  if (encoded != ResultCode::Ok) {
    diagnostic = "preview result cannot be encoded";
    return encoded;
  }
  note(thumbnail ? "renderd.thumbnail" : "renderd.frame", result.code,
       result.code == ResultCode::Ok ? subject : result.diagnostic, duration_ms);
  return channel.send(bytes, result.code == ResultCode::Ok ? shared.fd() : -1, diagnostic);
}

ResultCode PreviewServer::serve(preview_channel::Channel& channel, std::string& diagnostic) {
  for (;;) {
    // Wait for messages only when no thumbnail is queued.
    const ResultCode code = step(channel, thumbnails_.empty() ? -1 : 0, diagnostic);
    if (code == ResultCode::Ok || code == ResultCode::Cancelled) continue;
    if (code == ResultCode::IoFailed) {
      // Peer closed (or vanished): the session is over, the process keeps running.
      note("renderd.session", ResultCode::Ok, diagnostic);
      diagnostic.clear();
      return ResultCode::Ok;
    }
    note("renderd.session", code, diagnostic);
    return code;
  }
}

}  // namespace pixcake::renderd
