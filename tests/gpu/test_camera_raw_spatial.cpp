// PixCake's port of PhotoCraft's Camera Raw whole-frame stages (camera_raw_spatial.cpp), against
// PhotoCraft's own develop on the frames of tools/photocraft_vectors/src/spatial.rs.
#include <pixcake/gpu/camera_raw_spatial.hpp>

#include "spatial_frames.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// Checked in every build type (the release benchmark build defines NDEBUG).
#define CHECK(x)                                                              \
  do {                                                                        \
    if (!(x)) {                                                               \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x);      \
      std::abort();                                                           \
    }                                                                         \
  } while (0)

namespace {

namespace cr = pixcake::gpu::camera_raw;
#include "data/spatial_vectors.inc"

using spatial_frames::frame_of;

// develop's whole-frame stages in its order, with what this build has ported.
bool develop(const SpatialCase& c, cr::Frame& f) {
  if (c.dehaze != 0.0F) cr::dehaze(f, c.dehaze / 100.0F);
  if (c.shadows != 0.0F || c.highlights != 0.0F) cr::localTone(f, c.shadows, c.highlights);
  if (c.contrast != 0.0F || c.texture != 0.0F || c.clarity != 0.0F) cr::toneDetail(f, c.contrast, c.texture, c.clarity, c.pixel_scale);
  if (c.whites != 0.0F || c.blacks != 0.0F) cr::whitesBlacks(f, c.whites, c.blacks);
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
          CHECK(false);
        }
      }
    }
  }
  std::printf("spatial: %d cases, worst %.3g\n", cases, worst);
}

void test_box_radii() {  // Kovesi's triple, and one radius-1 pass below sigma ~0.577
  CHECK((cr::gaussBoxRadii(0.4F) == std::array<std::size_t, 3>{1, 0, 0}));
  CHECK(cr::gaussBoxRadii(3.0F)[0] >= 2 && cr::gaussBoxRadii(3.0F)[2] <= 4);
}

}  // namespace

int main() {
  test_box_radii();
  test_spatial_matches_photocraft();
  // Every plane split into row/column bands across the worker threads: the same bits.
  const std::size_t default_minimum = cr::setParallelMinimum(1);
  test_spatial_matches_photocraft();
  cr::setParallelMinimum(default_minimum);
  std::printf("test_camera_raw_spatial: ok\n");
  return 0;
}
