#include "pixcake/ipc/preview_protocol.hpp"

#include "message_codec.hpp"

#include <string>

namespace pixcake::ipc {
namespace {

using detail::MessageReader;
using detail::MessageWriter;

// 消息种类（信封里的第一个数）。与 process_messages.cpp 的 4–7 共用一个编号空间。
constexpr std::uint32_t kRequestKind = 1;
constexpr std::uint32_t kCancelKind = 2;
constexpr std::uint32_t kFrameKind = 3;
constexpr std::uint32_t kSourceKind = 8;
constexpr std::uint32_t kResultKind = 9;
constexpr std::uint32_t kThumbnailRequestKind = 10;
constexpr std::uint32_t kThumbnailResultKind = 11;
constexpr std::size_t kMaxReasonBytes = 2048;
constexpr std::size_t kMaxDiagnostics = 64;
constexpr std::size_t kMaxIdBytes = 1024;
constexpr std::size_t kMaxDisplayBytes = 256;

bool valid_mode(preview::PreviewMode mode) {
  return mode == preview::PreviewMode::FullEffect || mode == preview::PreviewMode::ColorOnly;
}

bool valid_request(const PreviewRequestMessage& message) {
  const auto& request = message.request;
  return message.protocol_version == kPreviewProtocolVersion && message.client_sequence != 0 &&
         !request.image_id.empty() && request.image_id.size() <= kMaxIdBytes &&
         !request.editing_instance_id.empty() && request.editing_instance_id.size() <= kMaxIdBytes &&
         !request.capability_id.empty() && request.capability_id.size() <= kMaxIdBytes &&
         !request.parameter_id.empty() && request.parameter_id.size() <= kMaxIdBytes &&
         !request.display_value.empty() && request.display_value.size() <= kMaxDisplayBytes &&
         valid_mode(request.mode) && request.max_edge > 0 && request.max_edge <= 4000;
}

bool valid_header(const PreviewFrameHeader& header) {
  if (header.protocol_version != kPreviewProtocolVersion || header.sequence == 0 || header.width == 0 ||
      header.height == 0 || header.width > 4000 || header.height > 4000 || header.format != PixelFormat::Rgba8) {
    return false;
  }
  const std::uint64_t minimum_stride = static_cast<std::uint64_t>(header.width) * 4;
  const std::uint64_t total = static_cast<std::uint64_t>(header.stride) * header.height;
  return header.stride >= minimum_stride && total <= 4000ull * 4000ull * 4ull;
}

ResultCode finish(const MessageWriter& writer, std::vector<std::uint8_t>& out) {
  out = writer.bytes();
  return out.size() <= kMaxPreviewMessageBytes ? ResultCode::Ok : ResultCode::StateConflict;
}

}  // namespace

ResultCode encode_preview_request(const PreviewRequestMessage& message, std::vector<std::uint8_t>& out) {
  out.clear();
  if (!valid_request(message)) return ResultCode::StateConflict;
  const auto& request = message.request;
  MessageWriter writer(kRequestKind, message.protocol_version);
  writer.u64(1, message.client_sequence);
  if (!writer.text(2, request.image_id, kMaxIdBytes) || !writer.text(3, request.editing_instance_id, kMaxIdBytes) ||
      !writer.text(5, request.capability_id, kMaxIdBytes) || !writer.text(6, request.parameter_id, kMaxIdBytes) ||
      !writer.text(7, request.display_value, kMaxDisplayBytes)) {
    return ResultCode::StateConflict;
  }
  writer.u64(4, request.base_revision);
  writer.u32(8, static_cast<std::uint32_t>(request.mode));
  writer.u32(9, request.max_edge);
  return finish(writer, out);
}

ResultCode decode_preview_request(const std::vector<std::uint8_t>& bytes, PreviewRequestMessage& out) {
  MessageReader reader;
  if (const ResultCode opened = reader.open(bytes, kRequestKind, kPreviewProtocolVersion, kMaxPreviewMessageBytes);
      opened != ResultCode::Ok) {
    return opened;
  }
  PreviewRequestMessage message;
  std::uint32_t mode = 0;
  auto& request = message.request;
  if (!reader.u64(1, message.client_sequence) || !reader.text(2, request.image_id, kMaxIdBytes) ||
      !reader.text(3, request.editing_instance_id, kMaxIdBytes) || !reader.u64(4, request.base_revision) ||
      !reader.text(5, request.capability_id, kMaxIdBytes) || !reader.text(6, request.parameter_id, kMaxIdBytes) ||
      !reader.text(7, request.display_value, kMaxDisplayBytes) || !reader.u32(8, mode) ||
      !reader.u32(9, request.max_edge)) {
    return ResultCode::StateConflict;
  }
  if (mode > static_cast<std::uint32_t>(preview::PreviewMode::ColorOnly)) return ResultCode::UnsupportedFormat;
  request.mode = static_cast<preview::PreviewMode>(mode);
  if (!valid_request(message)) return ResultCode::StateConflict;
  out = std::move(message);
  return ResultCode::Ok;
}

ResultCode encode_preview_cancel(const PreviewCancelMessage& message, std::vector<std::uint8_t>& out) {
  out.clear();
  if (message.protocol_version != kPreviewProtocolVersion || message.client_sequence == 0) {
    return ResultCode::StateConflict;
  }
  MessageWriter writer(kCancelKind, message.protocol_version);
  writer.u64(1, message.client_sequence);
  if (!writer.text(2, message.image_id, kMaxIdBytes) || !writer.text(3, message.editing_instance_id, kMaxIdBytes)) {
    return ResultCode::StateConflict;
  }
  writer.u64(4, message.base_revision);
  return finish(writer, out);
}

ResultCode decode_preview_cancel(const std::vector<std::uint8_t>& bytes, PreviewCancelMessage& out) {
  MessageReader reader;
  if (const ResultCode opened = reader.open(bytes, kCancelKind, kPreviewProtocolVersion, kMaxPreviewMessageBytes);
      opened != ResultCode::Ok) {
    return opened;
  }
  PreviewCancelMessage message;
  if (!reader.u64(1, message.client_sequence) || message.client_sequence == 0 ||
      !reader.text(2, message.image_id, kMaxIdBytes) || !reader.text(3, message.editing_instance_id, kMaxIdBytes) ||
      !reader.u64(4, message.base_revision)) {
    return ResultCode::StateConflict;
  }
  out = std::move(message);
  return ResultCode::Ok;
}

ResultCode encode_preview_frame_header(const PreviewFrameHeader& header, std::vector<std::uint8_t>& out) {
  out.clear();
  if (!valid_header(header)) return ResultCode::StateConflict;
  MessageWriter writer(kFrameKind, header.protocol_version);
  writer.u64(1, header.sequence);
  writer.u64(2, header.document_revision);
  writer.u32(3, header.width);
  writer.u32(4, header.height);
  writer.u32(5, header.stride);
  writer.u32(6, static_cast<std::uint32_t>(header.format));
  return finish(writer, out);
}

ResultCode decode_preview_frame_header(const std::vector<std::uint8_t>& bytes, PreviewFrameHeader& out) {
  MessageReader reader;
  if (const ResultCode opened = reader.open(bytes, kFrameKind, kPreviewProtocolVersion, kMaxPreviewMessageBytes);
      opened != ResultCode::Ok) {
    return opened;
  }
  PreviewFrameHeader header;
  std::uint32_t format = 0;
  if (!reader.u64(1, header.sequence) || !reader.u64(2, header.document_revision) || !reader.u32(3, header.width) ||
      !reader.u32(4, header.height) || !reader.u32(5, header.stride) || !reader.u32(6, format)) {
    return ResultCode::StateConflict;
  }
  if (format != static_cast<std::uint32_t>(PixelFormat::Rgba8)) return ResultCode::UnsupportedFormat;
  header.format = static_cast<PixelFormat>(format);
  if (!valid_header(header)) return ResultCode::StateConflict;
  out = header;
  return ResultCode::Ok;
}

ResultCode peek_preview_message_kind(const std::vector<std::uint8_t>& bytes, PreviewMessageKind& kind) {
  std::uint32_t raw = 0;
  std::uint32_t version = 0;
  if (!detail::peek_kind(bytes, raw, version)) return ResultCode::StateConflict;
  if (raw != kRequestKind && raw != kCancelKind && raw != kSourceKind && raw != kResultKind &&
      raw != kThumbnailRequestKind && raw != kThumbnailResultKind) {
    return ResultCode::UnsupportedFormat;
  }
  kind = static_cast<PreviewMessageKind>(raw);
  return ResultCode::Ok;
}

ResultCode encode_preview_source(const PreviewSourceMessage& message, std::vector<std::uint8_t>& out) {
  out.clear();
  if (message.protocol_version != kPreviewProtocolVersion || message.document.empty()) return ResultCode::StateConflict;
  MessageWriter writer(kSourceKind, message.protocol_version);
  if (!writer.text(1, message.image_id, kMaxIdBytes) || !writer.text(2, message.editing_instance_id, kMaxIdBytes) ||
      !writer.text(4, message.source_path, kMaxSourcePathBytes) ||
      !writer.blob(5, message.document, kMaxPreviewSourceBytes)) {
    return ResultCode::StateConflict;
  }
  writer.u64(3, message.document_revision);
  out = writer.bytes();
  if (out.size() > kMaxPreviewSourceBytes) {
    out.clear();
    return ResultCode::StateConflict;
  }
  return ResultCode::Ok;
}

ResultCode decode_preview_source(const std::vector<std::uint8_t>& bytes, PreviewSourceMessage& out) {
  MessageReader reader;
  if (const ResultCode opened = reader.open(bytes, kSourceKind, kPreviewProtocolVersion, kMaxPreviewSourceBytes);
      opened != ResultCode::Ok) {
    return opened;
  }
  PreviewSourceMessage message;
  if (!reader.text(1, message.image_id, kMaxIdBytes) || !reader.text(2, message.editing_instance_id, kMaxIdBytes) ||
      !reader.u64(3, message.document_revision) || !reader.text(4, message.source_path, kMaxSourcePathBytes) ||
      !reader.blob(5, message.document, kMaxPreviewSourceBytes) || message.document.empty()) {
    return ResultCode::StateConflict;
  }
  out = std::move(message);
  return ResultCode::Ok;
}

namespace {

bool acceptable_line(const std::string& value) {
  return value.find('\n') == std::string::npos && value.find('\t') == std::string::npos &&
         value.find('\0') == std::string::npos;
}

// Diagnostics travel as lines "<capability id>\t<reason>".
bool join_diagnostics(const std::vector<PreviewResultDiagnostic>& diagnostics, std::string& out) {
  out.clear();
  if (diagnostics.size() > kMaxDiagnostics) return false;
  for (const auto& d : diagnostics) {
    if (d.capability_id.size() > kMaxIdBytes || d.reason.empty() || d.reason.size() > kMaxReasonBytes ||
        !acceptable_line(d.capability_id) || !acceptable_line(d.reason)) {
      return false;
    }
    out += d.capability_id + "\t" + d.reason + "\n";
  }
  return true;
}

bool split_diagnostics(const std::string& joined, std::vector<PreviewResultDiagnostic>& out) {
  out.clear();
  std::size_t start = 0;
  while (start < joined.size()) {
    const std::size_t end = joined.find('\n', start);
    if (end == std::string::npos) return false;
    const std::string line = joined.substr(start, end - start);
    const std::size_t tab = line.find('\t');
    if (tab == std::string::npos || tab + 1 >= line.size()) return false;
    out.push_back({line.substr(0, tab), line.substr(tab + 1)});
    if (out.size() > kMaxDiagnostics) return false;
    start = end + 1;
  }
  return true;
}

}  // namespace

namespace {

ResultCode encode_result_of_kind(std::uint32_t kind, const PreviewResultMessage& message,
                                 std::vector<std::uint8_t>& out);
ResultCode decode_result_of_kind(std::uint32_t kind, const std::vector<std::uint8_t>& bytes,
                                 PreviewResultMessage& out);

}  // namespace

ResultCode encode_preview_result(const PreviewResultMessage& message, std::vector<std::uint8_t>& out) {
  return encode_result_of_kind(kResultKind, message, out);
}

ResultCode decode_preview_result(const std::vector<std::uint8_t>& bytes, PreviewResultMessage& out) {
  return decode_result_of_kind(kResultKind, bytes, out);
}

ResultCode encode_thumbnail_result(const PreviewResultMessage& message, std::vector<std::uint8_t>& out) {
  return encode_result_of_kind(kThumbnailResultKind, message, out);
}

ResultCode decode_thumbnail_result(const std::vector<std::uint8_t>& bytes, PreviewResultMessage& out) {
  return decode_result_of_kind(kThumbnailResultKind, bytes, out);
}

ResultCode encode_thumbnail_request(const ThumbnailRequestMessage& message, std::vector<std::uint8_t>& out) {
  out.clear();
  if (message.protocol_version != kPreviewProtocolVersion || message.request_id == 0 || message.max_edge == 0 ||
      message.max_edge > kMaxThumbnailEdge) {
    return ResultCode::StateConflict;
  }
  MessageWriter writer(kThumbnailRequestKind, message.protocol_version);
  writer.u64(1, message.request_id);
  writer.u32(4, message.max_edge);
  if (!writer.text(2, message.image_id, kMaxIdBytes) || !writer.text(3, message.source_path, kMaxSourcePathBytes) ||
      !writer.blob(5, message.document, kMaxPreviewSourceBytes)) {
    return ResultCode::StateConflict;
  }
  out = writer.bytes();
  if (out.size() > kMaxPreviewSourceBytes) {
    out.clear();
    return ResultCode::StateConflict;
  }
  return ResultCode::Ok;
}

ResultCode decode_thumbnail_request(const std::vector<std::uint8_t>& bytes, ThumbnailRequestMessage& out) {
  MessageReader reader;
  if (const ResultCode opened =
          reader.open(bytes, kThumbnailRequestKind, kPreviewProtocolVersion, kMaxPreviewSourceBytes);
      opened != ResultCode::Ok) {
    return opened;
  }
  ThumbnailRequestMessage message;
  if (!reader.u64(1, message.request_id) || message.request_id == 0 ||
      !reader.text(2, message.image_id, kMaxIdBytes) || !reader.text(3, message.source_path, kMaxSourcePathBytes) ||
      !reader.u32(4, message.max_edge) || message.max_edge == 0 || message.max_edge > kMaxThumbnailEdge ||
      !reader.blob(5, message.document, kMaxPreviewSourceBytes)) {
    return ResultCode::StateConflict;
  }
  out = std::move(message);
  return ResultCode::Ok;
}

namespace {

ResultCode encode_result_of_kind(std::uint32_t kind, const PreviewResultMessage& message,
                                 std::vector<std::uint8_t>& out) {
  out.clear();
  if (message.protocol_version != kPreviewProtocolVersion || message.client_sequence == 0) {
    return ResultCode::StateConflict;
  }
  if (message.code == ResultCode::Ok && (!valid_header(message.header) || message.header.sequence != message.client_sequence)) {
    return ResultCode::StateConflict;
  }
  std::string joined;
  if (!join_diagnostics(message.diagnostics, joined)) return ResultCode::StateConflict;
  MessageWriter writer(kind, message.protocol_version);
  writer.u64(1, message.client_sequence);
  writer.u32(2, static_cast<std::uint32_t>(message.code));
  if (message.code == ResultCode::Ok) {
    std::vector<std::uint8_t> header;
    if (encode_preview_frame_header(message.header, header) != ResultCode::Ok) return ResultCode::StateConflict;
    writer.blob(3, header, kMaxPreviewMessageBytes);
  }
  if (!writer.text(4, message.diagnostic, kMaxReasonBytes, true) ||
      !writer.text(5, joined, kMaxPreviewResultBytes, true)) {
    return ResultCode::StateConflict;
  }
  out = writer.bytes();
  if (out.size() > kMaxPreviewResultBytes) {
    out.clear();
    return ResultCode::StateConflict;
  }
  return ResultCode::Ok;
}

ResultCode decode_result_of_kind(std::uint32_t kind, const std::vector<std::uint8_t>& bytes,
                                 PreviewResultMessage& out) {
  MessageReader reader;
  if (const ResultCode opened = reader.open(bytes, kind, kPreviewProtocolVersion, kMaxPreviewResultBytes);
      opened != ResultCode::Ok) {
    return opened;
  }
  PreviewResultMessage message;
  std::uint32_t code = 0;
  std::string joined;
  if (!reader.u64(1, message.client_sequence) || message.client_sequence == 0 || !reader.u32(2, code) ||
      !reader.text(4, message.diagnostic, kMaxReasonBytes, true) ||
      !reader.text(5, joined, kMaxPreviewResultBytes, true) || !split_diagnostics(joined, message.diagnostics)) {
    return ResultCode::StateConflict;
  }
  if (code > static_cast<std::uint32_t>(ResultCode::IoFailed)) return ResultCode::UnsupportedFormat;
  message.code = static_cast<ResultCode>(code);
  if (message.code == ResultCode::Ok) {
    std::vector<std::uint8_t> header;
    if (!reader.blob(3, header, kMaxPreviewMessageBytes) ||
        decode_preview_frame_header(header, message.header) != ResultCode::Ok ||
        message.header.sequence != message.client_sequence) {
      return ResultCode::StateConflict;
    }
  } else if (reader.has(3)) {
    return ResultCode::StateConflict;
  }
  out = std::move(message);
  return ResultCode::Ok;
}

}  // namespace

}  // namespace pixcake::ipc
