#pragma once

#include "pixcake/support/result_code.hpp"

#include <cstdint>
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
  std::string color_space;
  bool include_watermark = false;
  ExportEncodeLimits limits;
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

ResultCode validate_encode_request(const ExportEncodeRequest& request, std::string& diagnostic);
ResultCode validate_rgba_source(const RgbaSource& source, std::string& diagnostic);
ResultCode validate_encoded_product(const EncodedProduct& product, const ExportEncodeLimits& limits,
                                   std::string& diagnostic);

class ExportEncoder {
 public:
  virtual ~ExportEncoder() = default;

  // The encoder returns bytes owned by the caller.  It never opens the final
  // destination or commits an ExportQueue product; that transaction remains
  // in kernel/exportq.
  virtual ResultCode encode(const ExportEncodeRequest& request, const RgbaSource& source,
                            EncodedProduct& out, std::string& diagnostic) = 0;
};

}  // namespace pixcake::exportd
