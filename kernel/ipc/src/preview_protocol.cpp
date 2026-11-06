#include "pixcake/ipc/preview_protocol.hpp"

#include <limits>
#include <string>

namespace pixcake::ipc {
namespace {

constexpr std::uint32_t kRequestMagic = 0x52585050;  // PPXR
constexpr std::uint32_t kCancelMagic = 0x43585050;   // PPXC
constexpr std::uint32_t kFrameMagic = 0x46585050;    // PPXF
constexpr std::size_t kMaxIdBytes = 1024;
constexpr std::size_t kMaxDisplayBytes = 256;

void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
  }
}

void append_u64(std::vector<std::uint8_t>& out, std::uint64_t value) {
  for (int shift = 0; shift < 64; shift += 8) {
    out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
  }
}

bool read_u32(const std::vector<std::uint8_t>& bytes, std::size_t& offset, std::uint32_t& value) {
  if (offset > bytes.size() || bytes.size() - offset < 4) return false;
  value = 0;
  for (int shift = 0; shift < 32; shift += 8) {
    value |= static_cast<std::uint32_t>(bytes[offset++]) << shift;
  }
  return true;
}

bool read_u64(const std::vector<std::uint8_t>& bytes, std::size_t& offset, std::uint64_t& value) {
  if (offset > bytes.size() || bytes.size() - offset < 8) return false;
  value = 0;
  for (int shift = 0; shift < 64; shift += 8) {
    value |= static_cast<std::uint64_t>(bytes[offset++]) << shift;
  }
  return true;
}

bool append_string(std::vector<std::uint8_t>& out, const std::string& value, std::size_t limit) {
  if (value.empty() || value.size() > limit || value.size() > std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }
  append_u32(out, static_cast<std::uint32_t>(value.size()));
  out.insert(out.end(), value.begin(), value.end());
  return true;
}

bool read_string(const std::vector<std::uint8_t>& bytes, std::size_t& offset, std::string& value,
                 std::size_t limit) {
  std::uint32_t size = 0;
  if (!read_u32(bytes, offset, size) || size == 0 || size > limit ||
      static_cast<std::size_t>(size) > bytes.size() - offset) {
    return false;
  }
  value.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
               bytes.begin() + static_cast<std::ptrdiff_t>(offset + size));
  offset += size;
  return value.find('\0') == std::string::npos;
}

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

ResultCode finish(const std::vector<std::uint8_t>& out) {
  return out.size() <= kMaxPreviewMessageBytes ? ResultCode::Ok : ResultCode::StateConflict;
}

}  // namespace

ResultCode encode_preview_request(const PreviewRequestMessage& message, std::vector<std::uint8_t>& out) {
  out.clear();
  if (!valid_request(message)) return ResultCode::StateConflict;
  append_u32(out, kRequestMagic);
  append_u32(out, message.protocol_version);
  append_u64(out, message.client_sequence);
  const auto& request = message.request;
  if (!append_string(out, request.image_id, kMaxIdBytes) ||
      !append_string(out, request.editing_instance_id, kMaxIdBytes)) return ResultCode::StateConflict;
  append_u64(out, request.base_revision);
  if (!append_string(out, request.capability_id, kMaxIdBytes) ||
      !append_string(out, request.parameter_id, kMaxIdBytes) ||
      !append_string(out, request.display_value, kMaxDisplayBytes)) return ResultCode::StateConflict;
  append_u32(out, static_cast<std::uint32_t>(request.mode));
  append_u32(out, request.max_edge);
  return finish(out);
}

ResultCode decode_preview_request(const std::vector<std::uint8_t>& bytes, PreviewRequestMessage& out) {
  if (bytes.size() > kMaxPreviewMessageBytes) return ResultCode::StateConflict;
  std::size_t offset = 0;
  std::uint32_t magic = 0;
  std::uint32_t mode = 0;
  if (!read_u32(bytes, offset, magic) || magic != kRequestMagic ||
      !read_u32(bytes, offset, out.protocol_version) || !read_u64(bytes, offset, out.client_sequence) ||
      !read_string(bytes, offset, out.request.image_id, kMaxIdBytes) ||
      !read_string(bytes, offset, out.request.editing_instance_id, kMaxIdBytes) ||
      !read_u64(bytes, offset, out.request.base_revision) ||
      !read_string(bytes, offset, out.request.capability_id, kMaxIdBytes) ||
      !read_string(bytes, offset, out.request.parameter_id, kMaxIdBytes) ||
      !read_string(bytes, offset, out.request.display_value, kMaxDisplayBytes) ||
      !read_u32(bytes, offset, mode) || !read_u32(bytes, offset, out.request.max_edge) || offset != bytes.size()) {
    return ResultCode::StateConflict;
  }
  if (mode > static_cast<std::uint32_t>(preview::PreviewMode::ColorOnly)) return ResultCode::UnsupportedFormat;
  out.request.mode = static_cast<preview::PreviewMode>(mode);
  return valid_request(out) ? ResultCode::Ok : ResultCode::StateConflict;
}

ResultCode encode_preview_cancel(const PreviewCancelMessage& message, std::vector<std::uint8_t>& out) {
  out.clear();
  if (message.protocol_version != kPreviewProtocolVersion || message.client_sequence == 0 ||
      message.image_id.empty() || message.editing_instance_id.empty()) return ResultCode::StateConflict;
  append_u32(out, kCancelMagic);
  append_u32(out, message.protocol_version);
  append_u64(out, message.client_sequence);
  if (!append_string(out, message.image_id, kMaxIdBytes) ||
      !append_string(out, message.editing_instance_id, kMaxIdBytes)) return ResultCode::StateConflict;
  return finish(out);
}

ResultCode decode_preview_cancel(const std::vector<std::uint8_t>& bytes, PreviewCancelMessage& out) {
  if (bytes.size() > kMaxPreviewMessageBytes) return ResultCode::StateConflict;
  std::size_t offset = 0;
  std::uint32_t magic = 0;
  if (!read_u32(bytes, offset, magic) || magic != kCancelMagic ||
      !read_u32(bytes, offset, out.protocol_version) || !read_u64(bytes, offset, out.client_sequence) ||
      !read_string(bytes, offset, out.image_id, kMaxIdBytes) ||
      !read_string(bytes, offset, out.editing_instance_id, kMaxIdBytes) || offset != bytes.size()) {
    return ResultCode::StateConflict;
  }
  return out.protocol_version == kPreviewProtocolVersion && out.client_sequence != 0 ? ResultCode::Ok
                                                                                         : ResultCode::StateConflict;
}

ResultCode encode_preview_frame_header(const PreviewFrameHeader& header, std::vector<std::uint8_t>& out) {
  out.clear();
  if (!valid_header(header)) return ResultCode::StateConflict;
  append_u32(out, kFrameMagic);
  append_u32(out, header.protocol_version);
  append_u64(out, header.sequence);
  append_u64(out, header.document_revision);
  append_u32(out, header.width);
  append_u32(out, header.height);
  append_u32(out, header.stride);
  append_u32(out, static_cast<std::uint32_t>(header.format));
  return finish(out);
}

ResultCode decode_preview_frame_header(const std::vector<std::uint8_t>& bytes, PreviewFrameHeader& out) {
  if (bytes.size() > kMaxPreviewMessageBytes) return ResultCode::StateConflict;
  std::size_t offset = 0;
  std::uint32_t magic = 0;
  std::uint32_t format = 0;
  if (!read_u32(bytes, offset, magic) || magic != kFrameMagic ||
      !read_u32(bytes, offset, out.protocol_version) || !read_u64(bytes, offset, out.sequence) ||
      !read_u64(bytes, offset, out.document_revision) || !read_u32(bytes, offset, out.width) ||
      !read_u32(bytes, offset, out.height) || !read_u32(bytes, offset, out.stride) ||
      !read_u32(bytes, offset, format) || offset != bytes.size()) {
    return ResultCode::StateConflict;
  }
  if (format != static_cast<std::uint32_t>(PixelFormat::Rgba8)) return ResultCode::UnsupportedFormat;
  out.format = static_cast<PixelFormat>(format);
  return valid_header(out) ? ResultCode::Ok : ResultCode::StateConflict;
}

}  // namespace pixcake::ipc
