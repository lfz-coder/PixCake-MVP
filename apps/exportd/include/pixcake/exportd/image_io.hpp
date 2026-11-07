#pragma once

#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pixcake::exportd {

enum class ExportFormat {
  Original,
  Jpeg,
  Tiff,
};

struct ExportEncodeLimits {
  std::uint64_t max_output_bytes = 0;
};

struct ExportEncodeRequest {
  ExportFormat format = ExportFormat::Original;
  // The product contract exposes the documented 0..14 quality scale.  A
  // format adapter maps it to the selected library's native quality scale.
  std::uint8_t quality = 0;
  // Requirement 19: empty keeps the original colour space (default), "sRGB"
  // converts to sRGB.  Nothing else is offered.
  std::string color_space;
  bool include_watermark = false;
  ExportEncodeLimits limits;
  // Filled by the export service from the decoded original: its ICC profile
  // (kept when color_space is empty) and whether the pixels carry alpha.
  std::vector<std::uint8_t> source_icc_profile;
  bool has_alpha = false;
};

struct RgbaSource {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t stride = 0;
  std::vector<std::uint8_t> rgba;
};

struct EncodedProduct {
  ExportFormat format = ExportFormat::Original;
  std::vector<std::uint8_t> bytes;
};

// The original, decoded at full resolution: sRGB RGBA8 (premultiplied when
// it has alpha), upright after EXIF orientation, with its ICC profile.
struct DecodedSource {
  RgbaSource pixels;
  bool has_alpha = false;
  std::vector<std::uint8_t> icc_profile;
};

struct ExportDecodeLimits {
  std::uint32_t max_width = 0;
  std::uint32_t max_height = 0;
  std::uint64_t max_pixels = 0;
};

ResultCode validate_encode_request(const ExportEncodeRequest& request, std::string& diagnostic);
ResultCode validate_rgba_source(const RgbaSource& source, std::string& diagnostic);
ResultCode validate_encoded_product(const EncodedProduct& product, const ExportEncodeLimits& limits,
                                   std::string& diagnostic);

class ExportDecoder {
 public:
  virtual ~ExportDecoder() = default;
  // SourceMissing, ImageCorrupt, UnsupportedFormat or StateConflict (over the
  // limits) as the renderd decoder reports them.
  virtual ResultCode decode(const std::string& path, const ExportDecodeLimits& limits, DecodedSource& out,
                            std::string& diagnostic) = 0;
};

class ExportEncoder {
 public:
  virtual ~ExportEncoder() = default;

  // The encoder returns bytes owned by the caller.  It never opens the final
  // destination or commits an ExportQueue product; that transaction remains
  // in kernel/exportq.
  virtual ResultCode encode(const ExportEncodeRequest& request, const RgbaSource& source,
                            EncodedProduct& out, std::string& diagnostic) = 0;
};

// ImageIO codecs on macOS (platform/macos); null elsewhere.  Quality 0..14
// maps linearly to ImageIO's 0..1 -- a project choice: the official mapping
// to JPEG quality is not published.
std::unique_ptr<ExportDecoder> make_system_export_decoder();
std::unique_ptr<ExportEncoder> make_system_export_encoder();

}  // namespace pixcake::exportd
