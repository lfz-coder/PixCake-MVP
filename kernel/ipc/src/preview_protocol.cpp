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

}  // namespace pixcake::ipc
