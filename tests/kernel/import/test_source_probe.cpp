#include "pixcake/image_import/source_probe.hpp"
#include "support/fake_file_system.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

int g_failures = 0;

void expect(bool condition, const char* expression, int line) {
  if (!condition) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, expression);
    ++g_failures;
  }
}

#define EXPECT(expression) expect(static_cast<bool>(expression), #expression, __LINE__)

void put_be16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value >> 8);
  bytes[offset + 1] = static_cast<std::uint8_t>(value);
}

void put_be32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value >> 24);
  bytes[offset + 1] = static_cast<std::uint8_t>(value >> 16);
  bytes[offset + 2] = static_cast<std::uint8_t>(value >> 8);
  bytes[offset + 3] = static_cast<std::uint8_t>(value);
}

void put_le16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value);
  bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

void put_le32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value);
  bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
  bytes[offset + 2] = static_cast<std::uint8_t>(value >> 16);
  bytes[offset + 3] = static_cast<std::uint8_t>(value >> 24);
}

std::vector<std::uint8_t> png_header(std::uint32_t width, std::uint32_t height) {
  std::vector<std::uint8_t> bytes = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
  bytes.resize(33, 0);
  put_be32(bytes, 8, 13);
  bytes[12] = 'I';
  bytes[13] = 'H';
  bytes[14] = 'D';
  bytes[15] = 'R';
  put_be32(bytes, 16, width);
  put_be32(bytes, 20, height);
  return bytes;
}

std::vector<std::uint8_t> jpeg_header(std::uint16_t width, std::uint16_t height) {
  std::vector<std::uint8_t> bytes = {0xff, 0xd8, 0xff, 0xc0, 0x00, 0x11, 8, 0, 0, 0, 0, 1, 1, 0, 0, 0,
                                     0, 0, 0, 0, 0, 0, 0};
  put_be16(bytes, 7, height);
  put_be16(bytes, 9, width);
  return bytes;
}

std::vector<std::uint8_t> bmp_header(std::uint32_t width, std::uint32_t height) {
  std::vector<std::uint8_t> bytes(54, 0);
  bytes[0] = 'B';
  bytes[1] = 'M';
  put_le32(bytes, 14, 40);
  put_le32(bytes, 18, width);
  put_le32(bytes, 22, height);
  put_le16(bytes, 26, 1);
  put_le16(bytes, 28, 24);
  return bytes;
}

std::vector<std::uint8_t> tiff_header(std::uint32_t width, std::uint32_t height) {
  std::vector<std::uint8_t> bytes(38, 0);
  bytes[0] = 'I';
  bytes[1] = 'I';
  put_le16(bytes, 2, 42);
  put_le32(bytes, 4, 8);
  put_le16(bytes, 8, 2);
  put_le16(bytes, 10, 256);
  put_le16(bytes, 12, 4);
  put_le32(bytes, 14, 1);
  put_le32(bytes, 18, width);
  put_le16(bytes, 22, 257);
  put_le16(bytes, 24, 4);
  put_le32(bytes, 26, 1);
  put_le32(bytes, 30, height);
  return bytes;
}

void test_known_headers_and_unknown_content() {
  pixcake::image_import::SourceFormat format = pixcake::image_import::SourceFormat::Jpeg;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  EXPECT(pixcake::image_import::probe_bytes(png_header(640, 480), format, width, height) == pixcake::ResultCode::Ok);
  EXPECT(format == pixcake::image_import::SourceFormat::Png && width == 640 && height == 480);
  EXPECT(pixcake::image_import::probe_bytes(jpeg_header(320, 240), format, width, height) == pixcake::ResultCode::Ok);
  EXPECT(format == pixcake::image_import::SourceFormat::Jpeg && width == 320 && height == 240);
  EXPECT(pixcake::image_import::probe_bytes(tiff_header(120, 80), format, width, height) == pixcake::ResultCode::Ok);
  EXPECT(format == pixcake::image_import::SourceFormat::Tiff && width == 120 && height == 80);
  EXPECT(pixcake::image_import::probe_bytes(bmp_header(40, 20), format, width, height) == pixcake::ResultCode::Ok);
  EXPECT(format == pixcake::image_import::SourceFormat::Bmp && width == 40 && height == 20);
  EXPECT(pixcake::image_import::probe_bytes({'n', 'o', 't', ' ', 'a', ' ', 'p', 'h', 'o', 't', 'o'}, format, width,
                                           height) == pixcake::ResultCode::UnsupportedFormat);
}

void test_bad_headers_are_rejected() {
  pixcake::image_import::SourceFormat format = pixcake::image_import::SourceFormat::Jpeg;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  auto broken_png = png_header(0, 480);
  EXPECT(pixcake::image_import::probe_bytes(broken_png, format, width, height) == pixcake::ResultCode::ImageCorrupt);
  auto truncated_jpeg = jpeg_header(320, 240);
  truncated_jpeg.resize(5);
  EXPECT(pixcake::image_import::probe_bytes(truncated_jpeg, format, width, height) == pixcake::ResultCode::ImageCorrupt);
  EXPECT(pixcake::image_import::probe_bytes({0x89, 'P', 'N'}, format, width, height) ==
         pixcake::ResultCode::ImageCorrupt);
  auto broken_tiff = tiff_header(120, 80);
  broken_tiff.resize(9);
  EXPECT(pixcake::image_import::probe_bytes(broken_tiff, format, width, height) == pixcake::ResultCode::ImageCorrupt);
  auto unsupported_bmp = bmp_header(40, 20);
  put_le32(unsupported_bmp, 14, 12);
  EXPECT(pixcake::image_import::probe_bytes(unsupported_bmp, format, width, height) == pixcake::ResultCode::UnsupportedFormat);
}

void test_file_admission_and_identity() {
  pixcake::test::FakeFileSystem files;
  files.files["/photos/a.png"] = png_header(10, 20);
  pixcake::image_import::SourceProbe out;
  EXPECT(pixcake::image_import::probe_source(files, "relative.png", out) == pixcake::ResultCode::StateConflict);
  EXPECT(pixcake::image_import::probe_source(files, "/photos/missing.png", out) == pixcake::ResultCode::SourceMissing);
  files.probe_errors["/photos/denied.png"] = pixcake::ResultCode::PermissionDenied;
  EXPECT(pixcake::image_import::probe_source(files, "/photos/denied.png", out) == pixcake::ResultCode::PermissionDenied);
  EXPECT(pixcake::image_import::probe_source(files, "/photos/a.png", out) == pixcake::ResultCode::Ok);
  EXPECT(out.absolute_path == "/photos/a.png");
  EXPECT(out.format == pixcake::image_import::SourceFormat::Png && out.width == 10 && out.height == 20);
  EXPECT(out.identity.valid);
  EXPECT(files.writes.empty());
}

}  // namespace

int main() {
  test_known_headers_and_unknown_content();
  test_bad_headers_are_rejected();
  test_file_admission_and_identity();
  return g_failures == 0 ? 0 : 1;
}
