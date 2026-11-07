#include "pixcake/platform/mac_image_decoder.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <sys/stat.h>

#include <algorithm>
#include <limits>

namespace pixcake::platform {
namespace {

// Releases a CF object at scope exit.
template <typename T>
struct CfHolder {
  T ref = nullptr;
  explicit CfHolder(T value = nullptr) : ref(value) {}
  ~CfHolder() {
    if (ref != nullptr) CFRelease(ref);
  }
  CfHolder(const CfHolder&) = delete;
  CfHolder& operator=(const CfHolder&) = delete;
};

std::string to_string(CFStringRef value) {
  if (value == nullptr) return {};
  char buffer[256];
  return CFStringGetCString(value, buffer, sizeof(buffer), kCFStringEncodingUTF8) ? buffer : std::string();
}

bool number_of(CFDictionaryRef dictionary, CFStringRef key, std::int64_t& out) {
  const auto value = static_cast<CFNumberRef>(CFDictionaryGetValue(dictionary, key));
  return value != nullptr && CFGetTypeID(value) == CFNumberGetTypeID() &&
         CFNumberGetValue(value, kCFNumberSInt64Type, &out);
}

MacImageKind kind_of(const std::string& type) {
  if (type == "public.jpeg") return MacImageKind::Jpeg;
  if (type == "public.png") return MacImageKind::Png;
  if (type == "public.tiff") return MacImageKind::Tiff;
  if (type == "com.microsoft.bmp") return MacImageKind::Bmp;
  if (type == "public.heic" || type == "public.heif") return MacImageKind::Heif;
  return MacImageKind::Other;
}

bool is_raw(CGImageSourceRef source) {
  // ImageIO reports RAW files with their vendor type ("com.canon.cr2-raw-image",
  // "com.adobe.raw-image", ...); all of them end in "raw-image".
  const std::string type = to_string(CGImageSourceGetType(source));
  return type.size() >= 9 && type.compare(type.size() - 9, 9, "raw-image") == 0;
}

void set_number(CFMutableDictionaryRef dictionary, CFStringRef key, std::int64_t value) {
  CfHolder<CFNumberRef> number(CFNumberCreate(nullptr, kCFNumberSInt64Type, &value));
  CFDictionarySetValue(dictionary, key, number.ref);
}

}  // namespace

ResultCode mac_decode_image(const std::string& path, const MacDecodeOptions& options, MacDecodedImage& out,
                            std::string& diagnostic) {
  out = {};
  if (options.max_source_width == 0 || options.max_source_height == 0 || options.max_source_pixels == 0) {
    diagnostic = "image decode limits are not configured";
    return ResultCode::StateConflict;
  }
  struct stat info {};
  if (path.empty() || ::stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) {
    diagnostic = "source image does not exist: " + path;
    return ResultCode::SourceMissing;
  }

  CfHolder<CFURLRef> url(CFURLCreateFromFileSystemRepresentation(
      nullptr, reinterpret_cast<const UInt8*>(path.c_str()), static_cast<CFIndex>(path.size()), false));
  // Never let ImageIO keep a cache of the source after this call.
  CfHolder<CFMutableDictionaryRef> source_options(
      CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
  CFDictionarySetValue(source_options.ref, kCGImageSourceShouldCache, kCFBooleanFalse);
  CfHolder<CGImageSourceRef> source(url.ref == nullptr ? nullptr
                                                       : CGImageSourceCreateWithURL(url.ref, source_options.ref));
  if (source.ref == nullptr || CGImageSourceGetCount(source.ref) == 0 ||
      CGImageSourceGetStatus(source.ref) != kCGImageStatusComplete) {
    diagnostic = "ImageIO cannot read the image: " + path;
    return ResultCode::ImageCorrupt;
  }
  const std::string type = to_string(CGImageSourceGetType(source.ref));
  MacImageKind kind = kind_of(type);
  if (kind == MacImageKind::Other && is_raw(source.ref)) kind = MacImageKind::Raw;
  if (kind == MacImageKind::Other || kind == MacImageKind::Unknown) {
    diagnostic = "image type is not supported: " + (type.empty() ? std::string("unknown") : type);
    return ResultCode::UnsupportedFormat;
  }

  CfHolder<CFDictionaryRef> properties(CGImageSourceCopyPropertiesAtIndex(source.ref, 0, nullptr));
  std::int64_t width = 0;
  std::int64_t height = 0;
  if (properties.ref == nullptr || !number_of(properties.ref, kCGImagePropertyPixelWidth, width) ||
      !number_of(properties.ref, kCGImagePropertyPixelHeight, height) || width <= 0 || height <= 0) {
    diagnostic = "image has no readable pixel size: " + path;
    return ResultCode::ImageCorrupt;
  }
  if (width > options.max_source_width || height > options.max_source_height ||
      static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) > options.max_source_pixels) {
    diagnostic = "image " + std::to_string(width) + "x" + std::to_string(height) + " exceeds the decode limits";
    return ResultCode::StateConflict;
  }

  // The thumbnail path applies EXIF orientation and downsamples during decode;
  // with max_edge = 0 (or larger than the image) it keeps the full size.
  const std::int64_t longest = std::max(width, height);
  const std::int64_t edge = options.max_edge == 0 ? longest : std::min<std::int64_t>(options.max_edge, longest);
  CfHolder<CFMutableDictionaryRef> decode_options(
      CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
  CFDictionarySetValue(decode_options.ref, kCGImageSourceCreateThumbnailFromImageAlways, kCFBooleanTrue);
  CFDictionarySetValue(decode_options.ref, kCGImageSourceCreateThumbnailWithTransform, kCFBooleanTrue);
  CFDictionarySetValue(decode_options.ref, kCGImageSourceShouldCacheImmediately, kCFBooleanTrue);
  set_number(decode_options.ref, kCGImageSourceThumbnailMaxPixelSize, edge);
  CGImageRef decoded = CGImageSourceCreateThumbnailAtIndex(source.ref, 0, decode_options.ref);
  if (decoded == nullptr) {
    diagnostic = "ImageIO failed to decode the image: " + path;
    return ResultCode::ImageCorrupt;
  }
  struct ImageHolder {
    CGImageRef ref;
    ~ImageHolder() { CGImageRelease(ref); }
  } image{decoded};

  const std::size_t out_width = CGImageGetWidth(image.ref);
  const std::size_t out_height = CGImageGetHeight(image.ref);
  if (out_width == 0 || out_height == 0 || out_width > std::numeric_limits<std::uint32_t>::max() / 4 ||
      out_height > std::numeric_limits<std::uint32_t>::max()) {
    diagnostic = "ImageIO returned an empty image: " + path;
    return ResultCode::ImageCorrupt;
  }
  const CGImageAlphaInfo alpha = CGImageGetAlphaInfo(image.ref);
  const bool has_alpha = alpha != kCGImageAlphaNone && alpha != kCGImageAlphaNoneSkipFirst &&
                         alpha != kCGImageAlphaNoneSkipLast;

  std::vector<std::uint8_t> pixels(out_width * out_height * 4, 0);
  CfHolder<CGColorSpaceRef> srgb(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
  CGContextRef context = CGBitmapContextCreate(pixels.data(), out_width, out_height, 8, out_width * 4, srgb.ref,
                                               kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
  if (context == nullptr) {
    diagnostic = "cannot create an RGBA bitmap for the image";
    return ResultCode::IoFailed;
  }
  CGContextSetBlendMode(context, kCGBlendModeCopy);
  CGContextSetInterpolationQuality(context, kCGInterpolationHigh);
  CGContextDrawImage(context, CGRectMake(0, 0, static_cast<CGFloat>(out_width), static_cast<CGFloat>(out_height)),
                     image.ref);
  CGContextRelease(context);

  if (CGColorSpaceRef space = CGImageGetColorSpace(image.ref)) {
    CfHolder<CFDataRef> icc(CGColorSpaceCopyICCData(space));
    if (icc.ref != nullptr) {
      const auto* bytes = CFDataGetBytePtr(icc.ref);
      out.source_icc_profile.assign(bytes, bytes + CFDataGetLength(icc.ref));
    }
  }
  out.kind = kind;
  out.type_identifier = type;
  out.source_width = static_cast<std::uint32_t>(width);
  out.source_height = static_cast<std::uint32_t>(height);
  out.width = static_cast<std::uint32_t>(out_width);
  out.height = static_cast<std::uint32_t>(out_height);
  out.stride = out.width * 4;
  out.has_alpha = has_alpha;
  out.rgba = std::move(pixels);
  diagnostic.clear();
  return ResultCode::Ok;
}

}  // namespace pixcake::platform
