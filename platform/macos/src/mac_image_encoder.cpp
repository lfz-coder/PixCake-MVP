#include "pixcake/platform/mac_image_encoder.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <limits>

namespace pixcake::platform {
namespace {

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

struct ImageHolder {
  CGImageRef ref = nullptr;
  ~ImageHolder() { CGImageRelease(ref); }
};

}  // namespace

ResultCode mac_encode_image(const MacEncodeRequest& request, std::uint32_t width, std::uint32_t height,
                            std::uint32_t stride, const std::uint8_t* rgba, std::size_t bytes,
                            std::vector<std::uint8_t>& out, std::string& diagnostic) {
  out.clear();
  if (rgba == nullptr || width == 0 || height == 0 || stride < static_cast<std::uint64_t>(width) * 4 ||
      bytes < static_cast<std::size_t>(stride) * (height - 1) + static_cast<std::size_t>(width) * 4) {
    diagnostic = "encode source buffer is empty or truncated";
    return ResultCode::StateConflict;
  }
  if (!(request.quality >= 0.0 && request.quality <= 1.0)) {
    diagnostic = "encode quality is outside 0..1";
    return ResultCode::StateConflict;
  }
  const CGBitmapInfo info = (request.has_alpha ? kCGImageAlphaPremultipliedLast : kCGImageAlphaNoneSkipLast) |
                            kCGBitmapByteOrder32Big;
  CfHolder<CGColorSpaceRef> srgb(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
  CfHolder<CFDataRef> data(CFDataCreateWithBytesNoCopy(nullptr, rgba, static_cast<CFIndex>(bytes), kCFAllocatorNull));
  CfHolder<CGDataProviderRef> provider(CGDataProviderCreateWithCFData(data.ref));
  ImageHolder image{CGImageCreate(width, height, 8, 32, stride, srgb.ref, info, provider.ref, nullptr, false,
                                  kCGRenderingIntentDefault)};
  if (image.ref == nullptr) {
    diagnostic = "cannot wrap the pixels as an image";
    return ResultCode::IoFailed;
  }

  // Keeping a non-sRGB original colour space: draw the sRGB pixels into a
  // bitmap of that space (ColorSync converts) and encode that.
  ImageHolder converted;
  if (!request.icc_profile.empty()) {
    CfHolder<CFDataRef> icc(CFDataCreate(nullptr, request.icc_profile.data(),
                                         static_cast<CFIndex>(request.icc_profile.size())));
    CfHolder<CGColorSpaceRef> target(CGColorSpaceCreateWithICCData(icc.ref));
    if (target.ref == nullptr || CGColorSpaceGetModel(target.ref) != kCGColorSpaceModelRGB) {
      diagnostic = "the colour profile to keep is not an RGB ICC profile";
      return ResultCode::StateConflict;
    }
    CGContextRef context = CGBitmapContextCreate(nullptr, width, height, 8, 0, target.ref, info);
    if (context == nullptr) {
      diagnostic = "cannot create a bitmap in the original colour space";
      return ResultCode::IoFailed;
    }
    CGContextSetBlendMode(context, kCGBlendModeCopy);
    CGContextDrawImage(context, CGRectMake(0, 0, width, height), image.ref);
    converted.ref = CGBitmapContextCreateImage(context);
    CGContextRelease(context);
    if (converted.ref == nullptr) {
      diagnostic = "cannot convert to the original colour space";
      return ResultCode::IoFailed;
    }
  }

  CfHolder<CFMutableDataRef> encoded(CFDataCreateMutable(nullptr, 0));
  const CFStringRef type = request.format == MacEncodeFormat::Jpeg ? CFSTR("public.jpeg") : CFSTR("public.tiff");
  CfHolder<CGImageDestinationRef> destination(CGImageDestinationCreateWithData(encoded.ref, type, 1, nullptr));
  if (destination.ref == nullptr) {
    diagnostic = "ImageIO cannot create the encoder";
    return ResultCode::IoFailed;
  }
  CfHolder<CFMutableDictionaryRef> properties(
      CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
  if (request.format == MacEncodeFormat::Jpeg) {
    const double quality = request.quality;
    CfHolder<CFNumberRef> number(CFNumberCreate(nullptr, kCFNumberDoubleType, &quality));
    CFDictionarySetValue(properties.ref, kCGImageDestinationLossyCompressionQuality, number.ref);
  }
  CGImageDestinationAddImage(destination.ref, converted.ref != nullptr ? converted.ref : image.ref, properties.ref);
  if (!CGImageDestinationFinalize(destination.ref)) {
    diagnostic = "ImageIO failed to encode the image";
    return ResultCode::IoFailed;
  }
  const auto* begin = CFDataGetBytePtr(encoded.ref);
  out.assign(begin, begin + CFDataGetLength(encoded.ref));
  diagnostic.clear();
  return ResultCode::Ok;
}

}  // namespace pixcake::platform
