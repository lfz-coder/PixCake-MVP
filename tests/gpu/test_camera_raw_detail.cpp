// PixCake's port of PhotoCraft's Camera Raw stage 8 (noise reduction and sharpening) in
// camera_raw_spatial.cpp, against PhotoCraft's own develop (tools/photocraft_vectors/src/detail.rs).
#include <pixcake/gpu/camera_raw_spatial.hpp>

#include "spatial_frames.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#define CHECK(x)                                                              \
  do {                                                                        \
    if (!(x)) {                                                               \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x);      \
      std::abort();                                                           \
    }                                                                         \
  } while (0)

namespace {

namespace cr = pixcake::gpu::camera_raw;
#include "data/detail_vectors.inc"

cr::DetailSettings settings_of(const DetailCase& c) {
  cr::DetailSettings s;
  s.sharpen_amount = c.sharpen_amount;
  s.sharpen_radius = c.sharpen_radius;
  s.sharpen_detail = c.sharpen_detail;
  s.sharpen_masking = c.sharpen_masking;
  s.noise_luminance = c.noise_luminance;
  s.noise_luminance_detail = c.noise_luminance_detail;
  s.noise_color = c.noise_color;
  s.noise_color_detail = c.noise_color_detail;
  return s;
}

void test_detail_matches_photocraft() {
  float worst = 0.0F;
  int cases = 0;
  for (const auto& c : kDetail) {
    cr::Frame f = spatial_frames::frame_of(c.frame);
    const cr::DetailSettings s = settings_of(c);
    CHECK(!cr::detailNeutral(s));
    cr::detail(f, s, c.pixel_scale);
    ++cases;
    for (std::size_t k = 0; k * static_cast<std::size_t>(c.stride) < f.px.size(); ++k) {
      for (int ch = 0; ch < 3; ++ch) {
        const float got = std::clamp(f.px[k * c.stride][ch], 0.0F, 1.0F), want = c.out[k * 3 + ch];
        worst = std::max(worst, std::fabs(got - want));
        if (got != want) {
          std::printf("%s / %s pixel %zu c%d: got %.9g want %.9g\n", c.frame, c.name, k * c.stride, ch, got, want);
          CHECK(false);
        }
      }
    }
  }
  std::printf("detail: %d cases, worst %.3g\n", cases, worst);
}

}  // namespace

int main() {
  CHECK(cr::detailNeutral(cr::DetailSettings{}));
  test_detail_matches_photocraft();
  const std::size_t default_minimum = cr::setParallelMinimum(1);  // banded across the workers: the same bits
  test_detail_matches_photocraft();
  cr::setParallelMinimum(default_minimum);
  std::printf("camera raw detail ok\n");
  return 0;
}
