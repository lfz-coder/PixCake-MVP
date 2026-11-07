#include "pixcake/renderd/image_io.hpp"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

// A 24-bit bottom-up BMP: left half red, right half blue.
void write_bmp(const std::string& path, int w, int h) {
  const int row = (w * 3 + 3) / 4 * 4;
  const int size = 54 + row * h;
  std::vector<unsigned char> b(static_cast<std::size_t>(size), 0);
  auto put = [&](int at, int value, int bytes) {
    for (int i = 0; i < bytes; ++i) b[static_cast<std::size_t>(at + i)] = static_cast<unsigned char>(value >> (8 * i));
  };
  b[0] = 'B'; b[1] = 'M';
  put(2, size, 4); put(10, 54, 4); put(14, 40, 4); put(18, w, 4); put(22, h, 4);
  put(26, 1, 2); put(28, 24, 2); put(34, row * h, 4);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      unsigned char* p = &b[static_cast<std::size_t>(54 + y * row + x * 3)];  // B G R
      if (x < w / 2) p[2] = 255; else p[0] = 255;
    }
  }
  std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), size);
}

}  // namespace

int main() {
  using namespace pixcake;
  using namespace pixcake::renderd;

  std::string diagnostic;
  DecodeRequest request;
  request.source_path = "/tmp/input.jpg";
  request.limits = {4000, 4000, 16'000'000, 64'000'000};
  assert(validate_decode_request(request, diagnostic) == ResultCode::Ok);

  DecodedImage image;
  image.source_format = SourceFormat::Jpeg;
  image.width = 2;
  image.height = 1;
  image.stride = 8;
  image.bytes.assign(8, 255);
  assert(validate_decoded_image(image, request.limits, diagnostic) == ResultCode::Ok);

  image.stride = 4;
  assert(validate_decoded_image(image, request.limits, diagnostic) == ResultCode::ImageCorrupt);
  image.stride = 8;
  image.bytes.resize(7);
  assert(validate_decoded_image(image, request.limits, diagnostic) == ResultCode::ImageCorrupt);

  request.limits = {};
  assert(validate_decode_request(request, diagnostic) == ResultCode::StateConflict);
  request.limits = {4000, 4000, 16'000'000, 64'000'000};
  request.source_path.clear();
  assert(validate_decode_request(request, diagnostic) == ResultCode::SourceMissing);

#ifdef __APPLE__
  // The system decoder (ImageIO) behind the frozen interface.
  auto decoder = make_system_image_decoder();
  assert(decoder != nullptr);
  const std::string bmp = "/tmp/pixcake-renderd-decoder-" + std::to_string(::getpid()) + ".bmp";
  write_bmp(bmp, 64, 32);
  request.source_path = bmp;
  request.limits = {4000, 4000, 16'000'000, 64'000'000};
  DecodedImage decoded;
  assert(decoder->decode(request, decoded, diagnostic) == ResultCode::Ok);
  assert(decoded.source_format == SourceFormat::Bmp);
  assert(decoded.width == 64 && decoded.height == 32 && decoded.stride == 256);
  assert(decoded.bytes[0] == 255 && decoded.bytes[2] == 0 && decoded.bytes[3] == 255);
  assert(decoded.bytes[63 * 4 + 2] == 255 && decoded.bytes[63 * 4] == 0);
  request.max_edge = 16;  // preview downscale happens in the decoder
  assert(decoder->decode(request, decoded, diagnostic) == ResultCode::Ok);
  assert(decoded.width == 16 && decoded.height == 8);
  request.expected_format = SourceFormat::Jpeg;  // content decides, not the caller's guess
  assert(decoder->decode(request, decoded, diagnostic) == ResultCode::UnsupportedFormat);
  assert(decoded.bytes.empty());
  request.expected_format = SourceFormat::Unknown;
  request.max_edge = 0;
  request.limits = {63, 4000, 16'000'000, 64'000'000};
  assert(decoder->decode(request, decoded, diagnostic) == ResultCode::StateConflict);
  std::remove(bmp.c_str());
  request.limits = {4000, 4000, 16'000'000, 64'000'000};
  assert(decoder->decode(request, decoded, diagnostic) == ResultCode::SourceMissing);
#endif
  return 0;
}
