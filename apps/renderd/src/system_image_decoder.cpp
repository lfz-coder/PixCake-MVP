#include "pixcake/renderd/image_io.hpp"

#ifdef __APPLE__
#include "pixcake/platform/mac_image_decoder.hpp"
#endif

namespace pixcake::renderd {

#ifdef __APPLE__
namespace {

SourceFormat format_of(platform::MacImageKind kind) {
  switch (kind) {
    case platform::MacImageKind::Jpeg: return SourceFormat::Jpeg;
    case platform::MacImageKind::Png: return SourceFormat::Png;
    case platform::MacImageKind::Tiff: return SourceFormat::Tiff;
    case platform::MacImageKind::Bmp: return SourceFormat::Bmp;
    case platform::MacImageKind::Raw: return SourceFormat::Raw;
    default: return SourceFormat::Unknown;
  }
}

class MacImageDecoder final : public ImageDecoder {
 public:
  ResultCode decode(const DecodeRequest& request, DecodedImage& out, std::string& diagnostic) override {
    out = {};
    ResultCode code = validate_decode_request(request, diagnostic);
    if (code != ResultCode::Ok) return code;
    platform::MacDecodeOptions options;
    options.max_edge = request.max_edge;
    options.max_source_width = request.limits.max_width;
    options.max_source_height = request.limits.max_height;
    options.max_source_pixels = request.limits.max_pixels;
    platform::MacDecodedImage image;
    code = platform::mac_decode_image(request.source_path, options, image, diagnostic);
    if (code != ResultCode::Ok) return code;
    const SourceFormat format = format_of(image.kind);
    if (format == SourceFormat::Unknown) {
      // HEIF decodes but is outside the formats the project accepts (SourceFormat).
      diagnostic = "image type is not supported: " + image.type_identifier;
      return ResultCode::UnsupportedFormat;
    }
    if (request.expected_format != SourceFormat::Unknown && request.expected_format != format) {
      diagnostic = "image content is " + image.type_identifier + ", not the expected format";
      return ResultCode::UnsupportedFormat;
    }
    DecodedImage decoded;
    decoded.source_format = format;
    decoded.width = image.width;
    decoded.height = image.height;
    decoded.stride = image.stride;
    decoded.bytes = std::move(image.rgba);
    code = validate_decoded_image(decoded, request.limits, diagnostic);
    if (code != ResultCode::Ok) return code;
    out = std::move(decoded);
    return ResultCode::Ok;
  }
};

}  // namespace

std::unique_ptr<ImageDecoder> make_system_image_decoder() { return std::make_unique<MacImageDecoder>(); }
#else
std::unique_ptr<ImageDecoder> make_system_image_decoder() { return nullptr; }
#endif

}  // namespace pixcake::renderd
