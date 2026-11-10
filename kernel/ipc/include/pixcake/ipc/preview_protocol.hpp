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

// 壳层 → renderd：之后的预览请求所针对的原图和已提交文档（证据
// 2026-11-06-renderd-transport-and-decoder.md §3.2）。原图路径由壳层解析书签后给出；
// 文档是 EditDocument::encode() 的字节，预览值不在其中，由每个预览请求带来。
inline constexpr std::size_t kMaxPreviewSourceBytes = 8 * 1024 * 1024;
inline constexpr std::size_t kMaxSourcePathBytes = 4096;
inline constexpr std::size_t kMaxLayerSourceBytes = 4 * 1024 * 1024;

struct PreviewSourceMessage {
  std::uint32_t protocol_version = kPreviewProtocolVersion;
  std::string image_id;
  std::string editing_instance_id;
  std::uint64_t document_revision = 0;
  std::string source_path;
  std::vector<std::uint8_t> document;
  // Optional PXLD layer document.  Empty preserves the source-only preview
  // contract used by photos without creative results.
  std::vector<std::uint8_t> layer_document;
};

// renderd → 壳层：一个预览请求的结果。code 为 Ok 时 header 有效，像素在随消息传来的共享内存里
// （stride × height 字节）；否则只有原因。diagnostics 是逐条的不阻断原因，例如“牙齿不可用”。
inline constexpr std::size_t kMaxPreviewResultBytes = 64 * 1024;

struct PreviewResultDiagnostic {
  std::string capability_id;
  std::string reason;
};

struct PreviewResultMessage {
  std::uint32_t protocol_version = kPreviewProtocolVersion;
  std::uint64_t client_sequence = 0;
  ResultCode code = ResultCode::Ok;
  PreviewFrameHeader header;
  std::string diagnostic;
  std::vector<PreviewResultDiagnostic> diagnostics;
};

// 壳层 → renderd：一张照片的缩略图（图库、精修底部的缩略图条）。和预览不同，缩略图请求排队，
// 每个都回结果（种类 11，字段同预览结果，client_sequence 是 request_id）。document 为空表示原图；
// 否则是 EditDocument::encode() 的字节，按已提交的编辑绘制。
inline constexpr std::uint32_t kMaxThumbnailEdge = 1024;

struct ThumbnailRequestMessage {
  std::uint32_t protocol_version = kPreviewProtocolVersion;
  std::uint64_t request_id = 0;
  std::string image_id;
  std::string source_path;
  std::uint32_t max_edge = 320;
  std::vector<std::uint8_t> document;
  std::vector<std::uint8_t> layer_document;
};

// 读信封里的消息种类，供传输层分派。不是合法信封时返回 StateConflict。
enum class PreviewMessageKind : std::uint32_t {
  Request = 1,
  Cancel = 2,
  Source = 8,
  Result = 9,
  ThumbnailRequest = 10,
  ThumbnailResult = 11,
};
ResultCode peek_preview_message_kind(const std::vector<std::uint8_t>& bytes, PreviewMessageKind& kind);

ResultCode encode_preview_source(const PreviewSourceMessage& message, std::vector<std::uint8_t>& out);
ResultCode decode_preview_source(const std::vector<std::uint8_t>& bytes, PreviewSourceMessage& out);
ResultCode encode_preview_result(const PreviewResultMessage& message, std::vector<std::uint8_t>& out);
ResultCode decode_preview_result(const std::vector<std::uint8_t>& bytes, PreviewResultMessage& out);

ResultCode encode_thumbnail_request(const ThumbnailRequestMessage& message, std::vector<std::uint8_t>& out);
ResultCode decode_thumbnail_request(const std::vector<std::uint8_t>& bytes, ThumbnailRequestMessage& out);
ResultCode encode_thumbnail_result(const PreviewResultMessage& message, std::vector<std::uint8_t>& out);
ResultCode decode_thumbnail_result(const std::vector<std::uint8_t>& bytes, PreviewResultMessage& out);

ResultCode encode_preview_request(const PreviewRequestMessage& message, std::vector<std::uint8_t>& out);
ResultCode decode_preview_request(const std::vector<std::uint8_t>& bytes, PreviewRequestMessage& out);
ResultCode encode_preview_cancel(const PreviewCancelMessage& message, std::vector<std::uint8_t>& out);
ResultCode decode_preview_cancel(const std::vector<std::uint8_t>& bytes, PreviewCancelMessage& out);
ResultCode encode_preview_frame_header(const PreviewFrameHeader& header, std::vector<std::uint8_t>& out);
ResultCode decode_preview_frame_header(const std::vector<std::uint8_t>& bytes, PreviewFrameHeader& out);

}  // namespace pixcake::ipc
