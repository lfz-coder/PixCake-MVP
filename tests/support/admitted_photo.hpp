#pragma once

#include "pixcake/image_import/source_probe.hpp"
#include "support/fake_file_system.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

namespace pixcake::test {

// 最小的 PNG 签名加 IHDR 头，足以通过导入准入的格式和尺寸检查。
inline std::vector<std::uint8_t> tiny_png(std::uint32_t width = 4, std::uint32_t height = 3) {
  std::vector<std::uint8_t> bytes = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a, 0, 0, 0, 13, 'I', 'H', 'D', 'R'};
  for (const std::uint32_t value : {width, height}) {
    for (int shift = 24; shift >= 0; shift -= 8) bytes.push_back(static_cast<std::uint8_t>(value >> shift));
  }
  bytes.resize(33, 0);
  return bytes;
}

// PNG 头后面接上测试自己的标记字节：仍能通过准入，又能用标记字节判断原图有没有被改动。
inline std::vector<std::uint8_t> png_with(std::vector<std::uint8_t> marker) {
  std::vector<std::uint8_t> bytes = tiny_png();
  bytes.insert(bytes.end(), marker.begin(), marker.end());
  return bytes;
}

// 让假文件系统里的这个路径成为一张可准入的图片，并返回准入凭证。
// 路径上已有可准入的图片时保留原字节；否则写入最小 PNG。准入仍失败时直接终止测试。
inline AdmittedSource admitted_png(FakeFileSystem& files, const std::string& path) {
  std::optional<AdmittedSource> admitted;
  if (image_import::admit_source(files, path, admitted) != ResultCode::Ok) {
    files.files[path] = tiny_png();
    image_import::admit_source(files, path, admitted);
  }
  if (!admitted) {
    std::fprintf(stderr, "test fixture could not admit %s\n", path.c_str());
    std::abort();
  }
  return *admitted;
}

}  // namespace pixcake::test
