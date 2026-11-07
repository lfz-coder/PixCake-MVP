#pragma once

#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pixcake::platform {

// Image encoding through macOS ImageIO, the counterpart of mac_decode_image.
// Only project types cross this header; CoreFoundation stays inside
// platform/macos (architecture rule 19).

// Tiff16 widens the 8-bit pixels to 16 bits per channel (v * 257): the file
// has 16-bit samples, the precision is still that of the 8-bit pipeline.
enum class MacEncodeFormat { Jpeg, Tiff, Tiff16, Png };

struct MacEncodeRequest {
  MacEncodeFormat format = MacEncodeFormat::Jpeg;
  // ImageIO's lossy compression quality, 0..1 (JPEG only).
  double quality = 1.0;
  // Profile to convert to and embed; empty writes sRGB.
  std::vector<std::uint8_t> icc_profile;
  // True when the pixels carry alpha (premultiplied, as mac_decode_image
  // returns them); TIFF keeps it, JPEG has no alpha.
  bool has_alpha = false;
};

// The pixels are sRGB RGBA8 rows of `stride` bytes.  IoFailed when ImageIO
// cannot encode; StateConflict for an invalid buffer or ICC profile.
ResultCode mac_encode_image(const MacEncodeRequest& request, std::uint32_t width, std::uint32_t height,
                            std::uint32_t stride, const std::uint8_t* rgba, std::size_t bytes,
                            std::vector<std::uint8_t>& out, std::string& diagnostic);

}  // namespace pixcake::platform
