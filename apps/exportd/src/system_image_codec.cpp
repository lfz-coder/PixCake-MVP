#include "pixcake/exportd/image_io.hpp"

#ifdef __APPLE__
#include "pixcake/platform/mac_image_decoder.hpp"
#include "pixcake/platform/mac_image_encoder.hpp"
#endif

namespace pixcake::exportd {

#ifdef __APPLE__
namespace {

class MacExportDecoder final : public ExportDecoder {
 public:
  ResultCode decode(const std::string& path, const ExportDecodeLimits& limits, DecodedSource& out,
                    std::string& diagnostic) override {
    out = {};
    platform::MacDecodeOptions options;  // max_edge 0: full resolution
    options.max_source_width = limits.max_width;
    options.max_source_height = limits.max_height;
    options.max_source_pixels = limits.max_pixels;
    platform::MacDecodedImage image;
    const ResultCode code = platform::mac_decode_image(path, options, image, diagnostic);
    if (code != ResultCode::Ok) return code;
    if (image.kind == platform::MacImageKind::Heif || image.kind == platform::MacImageKind::Other ||
        image.kind == platform::MacImageKind::Unknown) {
      diagnostic = "image type is not supported: " + image.type_identifier;
      return ResultCode::UnsupportedFormat;
    }
    out.kind = image.kind == platform::MacImageKind::Jpeg  ? SourceKind::Jpeg
               : image.kind == platform::MacImageKind::Png ? SourceKind::Png
               : image.kind == platform::MacImageKind::Tiff ? SourceKind::Tiff
                                                            : SourceKind::Other;
    out.pixels.width = image.width;
    out.pixels.height = image.height;
    out.pixels.stride = image.stride;
    out.pixels.rgba = std::move(image.rgba);
    out.has_alpha = image.has_alpha;
    out.icc_profile = std::move(image.source_icc_profile);
    return validate_rgba_source(out.pixels, diagnostic);
  }
};

class MacExportEncoder final : public ExportEncoder {
 public:
  ResultCode encode(const ExportEncodeRequest& request, const RgbaSource& source, EncodedProduct& out,
                    std::string& diagnostic) override {
    out = {};
    ResultCode code = validate_encode_request(request, diagnostic);
    if (code != ResultCode::Ok) return code;
    code = validate_rgba_source(source, diagnostic);
    if (code != ResultCode::Ok) return code;
    platform::MacEncodeRequest mac;
    switch (request.format) {
      case ExportFormat::Jpeg: mac.format = platform::MacEncodeFormat::Jpeg; break;
      case ExportFormat::Tiff: mac.format = platform::MacEncodeFormat::Tiff; break;
      case ExportFormat::Tiff16: mac.format = platform::MacEncodeFormat::Tiff16; break;
      case ExportFormat::Png: mac.format = platform::MacEncodeFormat::Png; break;
      case ExportFormat::Original: return ResultCode::UnsupportedFormat;  // validated above
    }
    mac.quality = request.native_quality >= 0.0 ? request.native_quality : request.quality / 14.0;
    if (request.color_space.empty()) mac.icc_profile = request.source_icc_profile;
    mac.has_alpha = request.has_alpha;
    EncodedProduct product;
    product.format = request.format;
    code = platform::mac_encode_image(mac, source.width, source.height, source.stride, source.rgba.data(),
                                      source.rgba.size(), product.bytes, diagnostic);
    if (code != ResultCode::Ok) return code;
    code = validate_encoded_product(product, request.limits, diagnostic);
    if (code != ResultCode::Ok) return code;
    out = std::move(product);
    return ResultCode::Ok;
  }
};

}  // namespace

std::unique_ptr<ExportDecoder> make_system_export_decoder() { return std::make_unique<MacExportDecoder>(); }
std::unique_ptr<ExportEncoder> make_system_export_encoder() { return std::make_unique<MacExportEncoder>(); }
#else
std::unique_ptr<ExportDecoder> make_system_export_decoder() { return nullptr; }
std::unique_ptr<ExportEncoder> make_system_export_encoder() { return nullptr; }
#endif

}  // namespace pixcake::exportd
