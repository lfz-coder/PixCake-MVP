#include "pixcake/renderd/image_io.hpp"

#include <limits>

namespace pixcake::renderd {
namespace {

bool known_format(SourceFormat format) { return format != SourceFormat::Unknown; }

}  // namespace

ResultCode validate_decode_request(const DecodeRequest& request, std::string& diagnostic) {
  if (request.source_path.empty()) {
    diagnostic = "image decode source path is empty";
    return ResultCode::SourceMissing;
  }
  const DecodeLimits& limits = request.limits;
  if (limits.max_width == 0 || limits.max_height == 0 || limits.max_pixels == 0 ||
      limits.max_output_bytes == 0) {
    diagnostic = "image decode limits are not configured";
    return ResultCode::StateConflict;
  }
  diagnostic.clear();
  return ResultCode::Ok;
}

ResultCode validate_decoded_image(const RgbaInput& image, const DecodeLimits& limits,
                                  std::string& diagnostic) {
  if (!known_format(image.source_format)) {
    diagnostic = "image decoder returned an unknown source format";
    return ResultCode::UnsupportedFormat;
  }
  if (image.width == 0 || image.height == 0 || image.width > limits.max_width ||
      image.height > limits.max_height) {
    diagnostic = "decoded image dimensions exceed the configured limits";
    return ResultCode::ImageCorrupt;
  }
  const std::uint64_t pixels = static_cast<std::uint64_t>(image.width) * image.height;
  if (pixels > limits.max_pixels) {
    diagnostic = "decoded image pixel count exceeds the configured limit";
    return ResultCode::ImageCorrupt;
  }
  const std::uint64_t minimum_stride = static_cast<std::uint64_t>(image.width) * 4ull;
  if (minimum_stride > std::numeric_limits<std::uint32_t>::max() || image.stride < minimum_stride) {
    diagnostic = "decoded image stride is smaller than RGBA width";
    return ResultCode::ImageCorrupt;
  }
  const std::uint64_t required_bytes = static_cast<std::uint64_t>(image.stride) * image.height;
  if (required_bytes > limits.max_output_bytes || required_bytes > std::numeric_limits<std::size_t>::max() ||
      image.bytes.size() < static_cast<std::size_t>(required_bytes)) {
    diagnostic = "decoded image buffer exceeds the configured limit or is truncated";
    return ResultCode::ImageCorrupt;
  }
  diagnostic.clear();
  return ResultCode::Ok;
}

}  // namespace pixcake::renderd
