#pragma once

#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pixcake::platform {

// Image decoding through macOS ImageIO (docs/superpowers/evidence/
// 2026-11-06-renderd-transport-and-decoder.md).  Only project types cross this
// header; CoreFoundation stays inside platform/macos (architecture rule 19).

enum class MacImageKind { Unknown, Jpeg, Png, Tiff, Bmp, Raw, Heif, Other };

struct MacDecodeOptions {
  // Longest edge of the output after EXIF orientation; 0 keeps the full size.
  // Images are never enlarged.
  std::uint32_t max_edge = 0;
  // Refused before any pixel is allocated, measured on the stored image.
  std::uint32_t max_source_width = 0;
  std::uint32_t max_source_height = 0;
  std::uint64_t max_source_pixels = 0;
};

struct MacDecodedImage {
  MacImageKind kind = MacImageKind::Unknown;
  std::string type_identifier;  // e.g. "public.jpeg"
  std::uint32_t source_width = 0;   // stored size, before orientation
  std::uint32_t source_height = 0;
  std::uint32_t width = 0;          // output size, after orientation and max_edge
  std::uint32_t height = 0;
  std::uint32_t stride = 0;         // width * 4
  bool has_alpha = false;           // true: RGB values are premultiplied by alpha
  std::vector<std::uint8_t> rgba;   // sRGB, 8 bits per channel, R G B A
  // ICC profile of the stored image (empty when it has none): an export that
  // keeps the original colour space converts back to it.
  std::vector<std::uint8_t> source_icc_profile;
};

// SourceMissing: no such file.  ImageCorrupt: ImageIO cannot read it.
// UnsupportedFormat: readable container but not an image type we accept.
// StateConflict: limits not configured or the image exceeds them.
ResultCode mac_decode_image(const std::string& path, const MacDecodeOptions& options, MacDecodedImage& out,
                            std::string& diagnostic);

}  // namespace pixcake::platform
