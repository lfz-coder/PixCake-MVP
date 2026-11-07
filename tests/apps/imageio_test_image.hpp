#pragma once

// Test-only image loading through the macOS ImageIO framework.  Production
// decoding goes through renderd's ImageDecoder; tests only need pixels of a
// fixture whose SHA-256 is locked in third_party/models.lock.cmake.

#include "pixcake/renderd/image_io.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <cassert>
#include <string>

namespace pixcake::test {

inline pixcake::renderd::RgbaInput load_rgba_with_imageio(const std::string& path) {
  using namespace pixcake::renderd;
  CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(path.c_str()),
                                                         static_cast<CFIndex>(path.size()), false);
  CGImageSourceRef source = CGImageSourceCreateWithURL(url, nullptr);
  assert(source != nullptr);
  CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
  assert(image != nullptr);
  RgbaInput out;
  out.source_format = SourceFormat::Jpeg;
  out.width = static_cast<std::uint32_t>(CGImageGetWidth(image));
  out.height = static_cast<std::uint32_t>(CGImageGetHeight(image));
  out.stride = out.width * 4;
  out.bytes.assign(static_cast<std::size_t>(out.stride) * out.height, 0);
  CGColorSpaceRef srgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGContextRef context = CGBitmapContextCreate(out.bytes.data(), out.width, out.height, 8, out.stride, srgb,
                                               kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
  CGContextDrawImage(context, CGRectMake(0, 0, out.width, out.height), image);
  CGContextRelease(context);
  CGColorSpaceRelease(srgb);
  CGImageRelease(image);
  CFRelease(source);
  CFRelease(url);
  return out;
}

}  // namespace pixcake::test
