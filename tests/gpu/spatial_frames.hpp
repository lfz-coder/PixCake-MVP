#pragma once

// Test frames for Camera Raw's whole-frame stages: the frames of
// tools/photocraft_vectors/src/spatial.rs (detail, grey, block, strip, long), "scaled" (detail
// times 2.5, past 1) and the synthetic scene of the executor tests.
#include <pixcake/gpu/camera_raw_spatial.hpp>
#include <pixcake/gpu/render_executor.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace spatial_frames {

inline std::array<float, 4> detail(std::size_t x, std::size_t y, std::size_t w, std::size_t h) {
  const float edge = x * 2 > w ? 0.35F : 0.0F;
  const float check = (x / 3 + y / 3) % 2 == 0 ? 0.08F : -0.08F;
  return {0.1F + 0.5F * static_cast<float>(x) / static_cast<float>(w - 1) + edge,
          0.15F + 0.6F * static_cast<float>(y) / static_cast<float>(h - 1) + check, 0.3F + edge * 0.5F - check, 1.0F};
}
inline std::array<float, 4> pixel(const std::string& frame, std::size_t x, std::size_t y, std::size_t w, std::size_t h) {
  if (frame == "grey") return {0.5F, 0.5F, 0.5F, 1.0F};
  if (frame == "block") {
    return x >= 8 && x < 17 && y >= 4 && y < 11 ? std::array<float, 4>{0.97F, 0.95F, 0.9F, 1.0F}
                                                 : std::array<float, 4>{0.25F, 0.3F, 0.35F, 1.0F};
  }
  auto p = detail(x, y, w, h);
  if (frame == "scaled") p = {p[0] * 2.5F, p[1] * 2.5F, p[2] * 2.5F, 1.0F};
  return p;
}
inline pixcake::gpu::camera_raw::Frame frame_of(const std::string& name) {
  pixcake::gpu::camera_raw::Frame f;
  f.width = name == "strip" ? 840 : name == "long" ? 1700 : 24;
  f.height = name == "strip" ? 6 : name == "long" ? 12 : 16;
  for (std::size_t i = 0; i < std::size_t{f.width} * f.height; ++i) f.px.push_back(pixel(name, i % f.width, i / f.width, f.width, f.height));
  return f;
}

// A frame with smooth gradients, a hard edge, a bright block and fine detail.
inline pixcake::gpu::RgbaImage scene(std::uint32_t w, std::uint32_t h) {
  pixcake::gpu::RgbaImage image;
  image.width = w;
  image.height = h;
  image.row_stride = w * 4;
  for (std::uint32_t y = 0; y < h; ++y) {
    for (std::uint32_t x = 0; x < w; ++x) {
      const float fx = static_cast<float>(x) / static_cast<float>(w), fy = static_cast<float>(y) / static_cast<float>(h);
      float r = 0.1F + 0.6F * fx, g = 0.15F + 0.5F * fy, b = 0.3F;
      if (fx > 0.55F) r += 0.25F, b += 0.2F;
      if (fx > 0.2F && fx < 0.45F && fy > 0.2F && fy < 0.5F) r = g = b = 0.95F;
      const auto q = [](float v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255.0F)); };
      image.pixels.insert(image.pixels.end(), {q(r), q(g), q(b), 255});
    }
  }
  return image;
}
inline pixcake::gpu::camera_raw::Frame frame_of_image(const pixcake::gpu::RgbaImage& image) {
  pixcake::gpu::camera_raw::Frame f;
  f.width = image.width;
  f.height = image.height;
  for (std::size_t i = 0; i < std::size_t{f.width} * f.height; ++i) {
    const auto* p = &image.pixels[i * 4];
    f.px.push_back({p[0] / 255.0F, p[1] / 255.0F, p[2] / 255.0F, p[3] / 255.0F});
  }
  return f;
}

}  // namespace spatial_frames
