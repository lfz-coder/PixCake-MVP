// PixCake's port of PhotoCraft's Camera Raw whole-frame stages (camera_raw_spatial.cpp), against
// PhotoCraft's own develop on the frames of tools/photocraft_vectors/src/spatial.rs.
#include <pixcake/gpu/camera_raw_spatial.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

namespace cr = pixcake::gpu::camera_raw;
#include "data/spatial_vectors.inc"

std::array<float, 4> detail(std::size_t x, std::size_t y, std::size_t w, std::size_t h) {
  const float edge = x * 2 > w ? 0.35F : 0.0F;
  const float check = (x / 3 + y / 3) % 2 == 0 ? 0.08F : -0.08F;
  return {0.1F + 0.5F * static_cast<float>(x) / static_cast<float>(w - 1) + edge,
          0.15F + 0.6F * static_cast<float>(y) / static_cast<float>(h - 1) + check, 0.3F + edge * 0.5F - check, 1.0F};
}
std::array<float, 4> pixel(const std::string& frame, std::size_t x, std::size_t y, std::size_t w, std::size_t h) {
  if (frame == "grey") return {0.5F, 0.5F, 0.5F, 1.0F};
  if (frame == "block") {
    return x >= 8 && x < 17 && y >= 4 && y < 11 ? std::array<float, 4>{0.97F, 0.95F, 0.9F, 1.0F}
                                                 : std::array<float, 4>{0.25F, 0.3F, 0.35F, 1.0F};
  }
  return detail(x, y, w, h);
}
cr::Frame frame_of(const std::string& name) {
  cr::Frame f;
  f.width = name == "strip" ? 840 : 24;
  f.height = name == "strip" ? 6 : 16;
  for (std::size_t i = 0; i < std::size_t{f.width} * f.height; ++i) f.px.push_back(pixel(name, i % f.width, i / f.width, f.width, f.height));
  return f;
}

// develop's whole-frame stages in its order, with what this build has ported.
bool develop(const SpatialCase& c, cr::Frame& f) {
  if (c.shadows != 0.0F || c.highlights != 0.0F || c.whites != 0.0F || c.blacks != 0.0F) return false;
  if (c.dehaze != 0.0F) cr::dehaze(f, c.dehaze / 100.0F);
  if (c.contrast != 0.0F || c.texture != 0.0F || c.clarity != 0.0F) cr::toneDetail(f, c.contrast, c.texture, c.clarity, c.pixel_scale);
  return true;
}

void test_spatial_matches_photocraft() {
  float worst = 0.0F;
  int cases = 0;
  for (const auto& c : kSpatial) {
    cr::Frame f = frame_of(c.frame);
    if (!develop(c, f)) continue;
    ++cases;
    for (std::size_t k = 0; k * static_cast<std::size_t>(c.stride) < f.px.size(); ++k) {
      for (int ch = 0; ch < 3; ++ch) {
        const float got = std::clamp(f.px[k * c.stride][ch], 0.0F, 1.0F), want = c.out[k * 3 + ch];
        const float d = std::fabs(got - want);
        worst = std::max(worst, d);
        if (!(d <= 1e-5F)) {
          std::printf("%s / %s pixel %zu c%d: got %.9g want %.9g\n", c.frame, c.name, k * c.stride, ch, got, want);
          assert(false);
        }
      }
    }
  }
  std::printf("spatial: %d cases, worst %.3g\n", cases, worst);
}

void test_box_radii() {  // Kovesi's triple, and one radius-1 pass below sigma ~0.577
  assert((cr::gaussBoxRadii(0.4F) == std::array<std::size_t, 3>{1, 0, 0}));
  const auto r = cr::gaussBoxRadii(3.0F);
  assert(r[0] >= 2 && r[2] <= 4);
}

}  // namespace

int main() {
  test_box_radii();
  test_spatial_matches_photocraft();
  std::printf("test_camera_raw_spatial: ok\n");
  return 0;
}
