#pragma once

#include "pixcake/support/result_code.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pixcake::renderd {

// The decoder owns file-format handling.  renderd owns only the contract and
// the validation needed before a decoded buffer can enter the preview path.
enum class SourceFormat {
  Unknown,
  Jpeg,
  Png,
  Tiff,
  Bmp,
  Raw,
};

struct DecodeLimits {
  std::uint32_t max_width = 0;
  std::uint32_t max_height = 0;
  std::uint64_t max_pixels = 0;
  std::uint64_t max_output_bytes = 0;
};

struct DecodeRequest {
  std::string source_path;
  SourceFormat expected_format = SourceFormat::Unknown;
  DecodeLimits limits;  // checked on the stored image before decoding and on the output
  // Longest output edge after orientation; 0 decodes at full resolution
  // (export).  Preview passes its maximum edge (<= 4000) so the decoder
  // downsamples instead of renderd resampling afterwards.
  std::uint32_t max_edge = 0;
};

struct RgbaInput {
  SourceFormat source_format = SourceFormat::Unknown;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t stride = 0;
  std::vector<std::uint8_t> bytes;
};

using DecodedImage = RgbaInput;

ResultCode validate_decode_request(const DecodeRequest& request, std::string& diagnostic);
ResultCode validate_decoded_image(const RgbaInput& image, const DecodeLimits& limits,
                                  std::string& diagnostic);

class ImageDecoder {
 public:
  virtual ~ImageDecoder() = default;

  // Implementations may use a third-party decoder, but they must return a
  // fully-owned RGBA buffer.  The implementation must not return a view into
  // a library-owned object or retain the source path after this call.
  virtual ResultCode decode(const DecodeRequest& request, DecodedImage& out,
                            std::string& diagnostic) = 0;
};

// The platform decoder: macOS ImageIO (evidence 2026-11-06-renderd-transport-
// and-decoder.md).  Null where no system decoder is built.
std::unique_ptr<ImageDecoder> make_system_image_decoder();

}  // namespace pixcake::renderd
