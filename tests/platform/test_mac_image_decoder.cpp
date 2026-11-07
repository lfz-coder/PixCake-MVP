// ImageIO decoding: formats, EXIF orientation, preview downscale, limits and
// failures.  Fixtures are written by ImageIO itself in a temporary directory.
#include "pixcake/platform/mac_image_decoder.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

using pixcake::ResultCode;
using namespace pixcake::platform;

// Left half red, right half blue, opaque (or a transparent right half).
void write_image(const std::string& path, CFStringRef type, std::uint32_t w, std::uint32_t h, int orientation,
                 bool transparent_right = false) {
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w) * h * 4);
  for (std::uint32_t y = 0; y < h; ++y) {
    for (std::uint32_t x = 0; x < w; ++x) {
      std::uint8_t* p = &pixels[(static_cast<std::size_t>(y) * w + x) * 4];
      const bool left = x < w / 2;
      p[0] = left ? 255 : 0;
      p[1] = 0;
      p[2] = left ? 0 : 255;
      p[3] = 255;
      if (!left && transparent_right) p[0] = p[1] = p[2] = p[3] = 0;
    }
  }
  CGColorSpaceRef srgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGContextRef context = CGBitmapContextCreate(pixels.data(), w, h, 8, w * 4, srgb,
                                               kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
  CGImageRef image = CGBitmapContextCreateImage(context);
  CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(path.c_str()),
                                                         static_cast<CFIndex>(path.size()), false);
  CGImageDestinationRef destination = CGImageDestinationCreateWithURL(url, type, 1, nullptr);
  CFNumberRef number = CFNumberCreate(nullptr, kCFNumberIntType, &orientation);
  const void* keys[] = {kCGImagePropertyOrientation};
  const void* values[] = {number};
  CFDictionaryRef properties = CFDictionaryCreate(nullptr, keys, values, 1, &kCFTypeDictionaryKeyCallBacks,
                                                  &kCFTypeDictionaryValueCallBacks);
  CGImageDestinationAddImage(destination, image, properties);
  const bool written = CGImageDestinationFinalize(destination);
  assert(written);
  (void)written;
  CFRelease(properties);
  CFRelease(number);
  CFRelease(destination);
  CFRelease(url);
  CGImageRelease(image);
  CGContextRelease(context);
  CGColorSpaceRelease(srgb);
}

const std::uint8_t* at(const MacDecodedImage& image, std::uint32_t x, std::uint32_t y) {
  return image.rgba.data() + static_cast<std::size_t>(y) * image.stride + x * 4;
}

MacDecodeOptions limits(std::uint32_t max_edge = 0) {
  MacDecodeOptions options;
  options.max_edge = max_edge;
  options.max_source_width = 20000;
  options.max_source_height = 20000;
  options.max_source_pixels = 200'000'000;
  return options;
}

}  // namespace

int main() {
  char pattern[] = "/tmp/pixcake-decoder-XXXXXX";
  const char* made = mkdtemp(pattern);
  assert(made != nullptr);
  const std::string dir = made;
  std::string diagnostic;

  // JPEG, full size: red left, blue right (JPEG is lossy: loose bounds).
  const std::string jpeg = dir + "/a.jpg";
  write_image(jpeg, CFSTR("public.jpeg"), 300, 200, 1);
  MacDecodedImage image;
  assert(mac_decode_image(jpeg, limits(), image, diagnostic) == ResultCode::Ok);
  assert(image.kind == MacImageKind::Jpeg && image.type_identifier == "public.jpeg");
  assert(image.width == 300 && image.height == 200 && image.stride == 1200);
  assert(image.source_width == 300 && image.source_height == 200);
  assert(image.rgba.size() == 1200u * 200u);
  assert(!image.has_alpha);
  assert(at(image, 20, 100)[0] > 230 && at(image, 20, 100)[2] < 30 && at(image, 20, 100)[3] == 255);
  assert(at(image, 280, 100)[2] > 230 && at(image, 280, 100)[0] < 30);

  // Preview downscale to the longest edge, aspect kept.
  assert(mac_decode_image(jpeg, limits(150), image, diagnostic) == ResultCode::Ok);
  assert(image.width == 150 && image.height == 100);
  assert(image.source_width == 300);
  assert(at(image, 10, 50)[0] > 230 && at(image, 140, 50)[2] > 230);
  // Never enlarged.
  assert(mac_decode_image(jpeg, limits(4000), image, diagnostic) == ResultCode::Ok);
  assert(image.width == 300 && image.height == 200);

  // EXIF orientation 6 (rotate 90° clockwise to display): 300x200 stored ->
  // 200x300 shown, the red (left) half becomes the top half.
  const std::string rotated = dir + "/r.jpg";
  write_image(rotated, CFSTR("public.jpeg"), 300, 200, 6);
  assert(mac_decode_image(rotated, limits(), image, diagnostic) == ResultCode::Ok);
  assert(image.source_width == 300 && image.source_height == 200);
  assert(image.width == 200 && image.height == 300);
  assert(at(image, 100, 20)[0] > 230 && at(image, 100, 280)[2] > 230);

  // PNG with transparency: alpha kept, RGB premultiplied.
  const std::string png = dir + "/t.png";
  write_image(png, CFSTR("public.png"), 40, 20, 1, true);
  assert(mac_decode_image(png, limits(), image, diagnostic) == ResultCode::Ok);
  assert(image.kind == MacImageKind::Png && image.has_alpha);
  assert(at(image, 5, 10)[0] == 255 && at(image, 5, 10)[3] == 255);
  assert(at(image, 35, 10)[3] == 0 && at(image, 35, 10)[2] == 0);

  // TIFF.
  const std::string tiff = dir + "/a.tif";
  write_image(tiff, CFSTR("public.tiff"), 30, 10, 1);
  assert(mac_decode_image(tiff, limits(), image, diagnostic) == ResultCode::Ok);
  assert(image.kind == MacImageKind::Tiff && image.width == 30);

  // Limits are checked on the stored size before decoding.
  auto small = limits();
  small.max_source_width = 299;
  assert(mac_decode_image(jpeg, small, image, diagnostic) == ResultCode::StateConflict);
  assert(image.rgba.empty() && !diagnostic.empty());
  small = limits();
  small.max_source_pixels = 300 * 200 - 1;
  assert(mac_decode_image(jpeg, small, image, diagnostic) == ResultCode::StateConflict);
  assert(mac_decode_image(jpeg, MacDecodeOptions{}, image, diagnostic) == ResultCode::StateConflict);

  // Failures.
  assert(mac_decode_image(dir + "/missing.jpg", limits(), image, diagnostic) == ResultCode::SourceMissing);
  {
    std::ofstream(dir + "/bad.jpg") << "\xff\xd8\xff not really a jpeg";
  }
  assert(mac_decode_image(dir + "/bad.jpg", limits(), image, diagnostic) == ResultCode::ImageCorrupt);
  {
    std::ofstream(dir + "/note.txt") << "plain text";
  }
  const ResultCode text = mac_decode_image(dir + "/note.txt", limits(), image, diagnostic);
  assert(text == ResultCode::ImageCorrupt || text == ResultCode::UnsupportedFormat);
  assert(image.rgba.empty());
  // A truncated JPEG: either refused or decoded at full size; never a wrong size.
  {
    std::ifstream in(jpeg, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), {});
    bytes.resize(bytes.size() / 3);
    std::ofstream(dir + "/cut.jpg", std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  }
  const ResultCode cut = mac_decode_image(dir + "/cut.jpg", limits(), image, diagnostic);
  assert(cut == ResultCode::ImageCorrupt || (cut == ResultCode::Ok && image.width == 300 && image.height == 200));

  for (const char* name : {"a.jpg", "r.jpg", "t.png", "a.tif", "bad.jpg", "note.txt", "cut.jpg"}) {
    std::remove((dir + "/" + name).c_str());
  }
  rmdir(dir.c_str());
  return 0;
}
