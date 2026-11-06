#include "pixcake/exportd/image_io.hpp"

#include <limits>

namespace pixcake::exportd {

ResultCode validate_encode_request(const ExportEncodeRequest& request, std::string& diagnostic) {
  if (request.format == ExportFormat::Original) {
    diagnostic = "original export is a source-file transaction, not an RGBA encode";
    return ResultCode::UnsupportedFormat;
  }
  if (request.quality > 14) {
    diagnostic = "export quality is outside the documented 0..14 range";
    return ResultCode::StateConflict;
  }
  if (request.limits.max_output_bytes == 0) {
    diagnostic = "export output limit is not configured";
    return ResultCode::StateConflict;
  }
  diagnostic.clear();
  return ResultCode::Ok;
}

ResultCode validate_rgba_source(const RgbaSource& source, std::string& diagnostic) {
  if (source.width == 0 || source.height == 0) {
    diagnostic = "export source dimensions are empty";
    return ResultCode::StateConflict;
  }
  const std::uint64_t minimum_stride = static_cast<std::uint64_t>(source.width) * 4ull;
  if (minimum_stride > std::numeric_limits<std::uint32_t>::max() || source.stride < minimum_stride) {
    diagnostic = "export source stride is smaller than RGBA width";
    return ResultCode::StateConflict;
  }
  const std::uint64_t required = static_cast<std::uint64_t>(source.stride) * source.height;
  if (required > std::numeric_limits<std::size_t>::max() ||
      source.rgba.size() < static_cast<std::size_t>(required)) {
    diagnostic = "export source buffer is truncated";
    return ResultCode::StateConflict;
  }
  diagnostic.clear();
  return ResultCode::Ok;
}

ResultCode validate_encoded_product(const EncodedProduct& product, const ExportEncodeLimits& limits,
                                   std::string& diagnostic) {
  if (product.format == ExportFormat::Original || product.bytes.empty()) {
    diagnostic = "encoder returned an empty or non-encoded product";
    return ResultCode::IoFailed;
  }
  if (limits.max_output_bytes == 0 || product.bytes.size() > limits.max_output_bytes) {
    diagnostic = "encoded product exceeds the configured output limit";
    return ResultCode::IoFailed;
  }
  diagnostic.clear();
  return ResultCode::Ok;
}

}  // namespace pixcake::exportd
