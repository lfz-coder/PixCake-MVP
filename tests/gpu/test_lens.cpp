// PixCake's port of PhotoCraft's Lens Correction filter (lens.cpp), against PhotoCraft's own
// correct() (tools/photocraft_vectors/src/lens.rs).
#include <pixcake/gpu/lens.hpp>

#include "spatial_frames.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
namespace lens = pixcake::gpu::lens;
#include "data/lens_vectors.inc"

cr::Frame frame_named(const std::string& name) {
  if (name != "alpha") return spatial_frames::frame_of(name);
  cr::Frame f = spatial_frames::frame_of("detail");
  const float alphas[4] = {1.0F, 0.6F, 0.0F, 0.25F};
  for (std::size_t i = 0; i < f.px.size(); ++i) f.px[i][3] = alphas[(i % f.width + 2 * (i / f.width)) % 4];
  return f;
}

void test_lens_matches_photocraft() {
  int cases = 0;
  for (const auto& c : kLens) {
    cr::Frame f = frame_named(c.frame);
    lens::Correction lc;
    lc.distortion = c.distortion;
    lc.vignette_amount = c.amount;
    lc.vignette_midpoint = c.midpoint;
    lc.extension = c.extension;
    lens::correct(f, lc);
    ++cases;
    for (std::size_t k = 0; k * static_cast<std::size_t>(c.stride) < f.px.size(); ++k) {
      for (int ch = 0; ch < 4; ++ch) {
        const float got = f.px[k * c.stride][ch], want = c.out[k * 4 + ch];
        if (std::memcmp(&got, &want, sizeof got) != 0 && !(got == 0.0F && want == 0.0F)) {
          std::printf("%s / %s pixel %zu c%d: got %.9g want %.9g\n", c.frame, c.name, k * c.stride, ch, got, want);
          CHECK(false);
        }
      }
    }
  }
  std::printf("lens: %d cases, bit for bit\n", cases);
}

}  // namespace

int main() {
  CHECK(lens::isIdentity(lens::Correction{}));
  test_lens_matches_photocraft();
  std::printf("lens ok\n");
  return 0;
}
