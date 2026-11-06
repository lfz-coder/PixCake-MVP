#pragma once

#include "pixcake/ipc/process_messages.hpp"
#include "pixcake/preview/preview_coordinator.hpp"
#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pixcake::ipc {

// 预览消息与 process_messages.hpp 的消息共用同一套记录编码（契约 C-30）。

inline constexpr std::uint32_t kPreviewProtocolVersion = 1;
inline constexpr std::size_t kMaxPreviewMessageBytes = 16 * 1024;

struct PreviewRequestMessage {
  std::uint32_t protocol_version = kPreviewProtocolVersion;
  std::uint64_t client_sequence = 0;
  preview::PreviewRequest request;
};

struct PreviewCancelMessage {
  std::uint32_t protocol_version = kPreviewProtocolVersion;
  std::uint64_t client_sequence = 0;
  std::string image_id;
  std::string editing_instance_id;
  std::uint64_t base_revision = 0;
};

enum class PixelFormat : std::uint32_t {
  Rgba8 = 1,
};

struct PreviewFrameHeader {
  std::uint32_t protocol_version = kPreviewProtocolVersion;
  std::uint64_t sequence = 0;
  std::uint64_t document_revision = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t stride = 0;
  PixelFormat format = PixelFormat::Rgba8;
};

ResultCode encode_preview_request(const PreviewRequestMessage& message, std::vector<std::uint8_t>& out);
ResultCode decode_preview_request(const std::vector<std::uint8_t>& bytes, PreviewRequestMessage& out);
ResultCode encode_preview_cancel(const PreviewCancelMessage& message, std::vector<std::uint8_t>& out);
ResultCode decode_preview_cancel(const std::vector<std::uint8_t>& bytes, PreviewCancelMessage& out);
ResultCode encode_preview_frame_header(const PreviewFrameHeader& header, std::vector<std::uint8_t>& out);
ResultCode decode_preview_frame_header(const std::vector<std::uint8_t>& bytes, PreviewFrameHeader& out);

}  // namespace pixcake::ipc
