// PixCake's C++ port of PhotoCraft's Photoshop modern brightness (gpu/render/src/brightness.cpp).
#include <pixcake/gpu/brightness.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdio>

namespace {

namespace br = pixcake::gpu::brightness;

struct LutSample {
  float brightness;
  std::size_t k;
  float value;
};
const LutSample kVectors[] = {
#include "data/modern_brightness_vectors.inc"
};

void test_matches_photocraft_lut() {
  float worst = 0.0F;
  float last = 0.0F;
  std::vector<float> table;
  for (const LutSample& v : kVectors) {
    if (table.empty() || v.brightness != last) {
      table = br::modernLut(v.brightness);
      last = v.brightness;
    }
    assert(table.size() == br::kLutSize);
    const float d = std::fabs(table[v.k] - v.value);
    worst = std::max(worst, d);
    if (d > 2e-6F) std::fprintf(stderr, "b %g k %zu ours %.9f photocraft %.9f\n", v.brightness, v.k, table[v.k], v.value);
    assert(d <= 2e-6F);
  }
  assert(sizeof(kVectors) / sizeof(kVectors[0]) == 650U);
  std::printf("photocraft brightness: %zu samples, worst difference %.3g\n", sizeof(kVectors) / sizeof(kVectors[0]),
              static_cast<double>(worst));
}

void test_zero_is_identity() {
  const auto t = br::modernLut(0.0F);
  for (std::size_t k = 0; k < t.size(); ++k) assert(t[k] == static_cast<float>(k) / 4095.0F);
}

void test_sample_lut_clamps() {
  const auto t = br::modernLut(40.0F);
  assert(br::sampleLut(t, -0.5F) == t[0]);
  assert(br::sampleLut(t, 1.5F) == t[4095]);
}

// The GPU evaluates the curve instead of a table: the raised branch directly, the lowered
// branch by 32 bisection steps on the raised curve.
float lowered(const br::Curve& c, float x) {
  float lo = 0.0F, hi = 1.0F;
  for (int i = 0; i < 32; ++i) {
    const float mid = 0.5F * (lo + hi);
    if (br::raised(c, mid) < x) lo = mid; else hi = mid;
  }
  return 0.5F * (lo + hi);
}

void test_curve_matches_lut() {
  for (float b : {-100.0F, -10.0F, 10.0F, 100.0F}) {
    const auto t = br::modernLut(b);
    const br::Curve c = br::curve(b);
    for (int i = 0; i <= 10; ++i) {
      const float x = static_cast<float>(i) / 10.0F;
      const float gpu = c.raise ? br::raised(c, x) : (x <= 0.0F || x >= 1.0F ? x : lowered(c, x));
      const float d = std::fabs(gpu - br::sampleLut(t, x));
      if (d > (c.raise ? 2e-6F : 1e-4F)) std::fprintf(stderr, "b %g x %g curve %.7f lut %.7f\n", b, x, gpu, br::sampleLut(t, x));
      assert(d <= (c.raise ? 2e-6F : 1e-4F));
    }
  }
}

}  // namespace

int main() {
  test_matches_photocraft_lut();
  test_zero_is_identity();
  test_sample_lut_clamps();
  test_curve_matches_lut();
  std::puts("test_brightness_port: ok");
  return 0;
}
