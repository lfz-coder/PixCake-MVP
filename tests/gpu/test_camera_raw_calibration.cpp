// PixCake's port of PhotoCraft's Camera Raw stage 1b (Calibration primaries) in camera_raw.cpp,
// against PhotoCraft's own develop (tools/photocraft_vectors/src/calibration.rs).
#include <pixcake/gpu/camera_raw.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
#include "data/calibration_vectors.inc"

void test_weights_match_photocraft() {
  for (const auto& row : kCalibrationWeights) {
    const auto w = cr::calibrationWeights(row[0]);
    CHECK(w[0] == row[1] && w[1] == row[2] && w[2] == row[3]);
  }
}

// develop's stages 1 and 1b, then the final clip.
void test_develop_matches_photocraft() {
  for (const auto& c : kCalibration) {
    const std::array<float, 3> hue{c.hue[0], c.hue[1], c.hue[2]}, sat{c.sat[0], c.sat[1], c.sat[2]};
    const bool balance = c.temperature != 0.0F || c.tint != 0.0F || c.exposure != 0.0F;
    const cr::Rgb gains = cr::balanceGains(c.temperature, c.tint, c.exposure);
    float worst = 0.0F;
    for (std::size_t i = 0; i + 2 < c.in.size(); i += 3) {
      cr::Rgb p{c.in[i], c.in[i + 1], c.in[i + 2]};
      if (balance) p = cr::applyBalance(p, gains);
      CHECK(!cr::calibrationNeutral(hue, sat));
      p = cr::applyCalibration(p, hue, sat);
      for (int k = 0; k < 3; ++k) worst = std::max(worst, std::abs(std::clamp(p[k], 0.0F, 1.0F) - c.out[i + k]));
    }
    std::printf("calibration %-22s worst %g\n", c.name, worst);
    CHECK(worst == 0.0F);
  }
  CHECK(cr::calibrationNeutral({}, {}));
}

}  // namespace

int main() {
  test_weights_match_photocraft();
  test_develop_matches_photocraft();
  std::printf("camera raw calibration ok\n");
  return 0;
}
