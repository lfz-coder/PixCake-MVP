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

// PhotoCraft lut_extended (tone.rs:134): inside 0..1 the table, outside it the end segments'
// slope over 16 steps; non-finite samples stay with the table.
void test_sample_lut_extended() {
  for (const float b : {40.0F, -60.0F}) {
    const auto lut = br::modernLut(b);
    const float per_unit = 4095.0F / 16.0F;
    const float lo = (lut[16] - lut[0]) * per_unit, hi = (lut[4095] - lut[4079]) * per_unit;
    assert(br::sampleLutExtended(lut, 0.3F) == br::sampleLut(lut, 0.3F));
    assert(std::abs(br::sampleLutExtended(lut, -0.2F) - (lut[0] - 0.2F * lo)) < 1e-6F);
    assert(std::abs(br::sampleLutExtended(lut, 1.5F) - (lut[4095] + 0.5F * hi)) < 1e-6F);
    const auto slopes = br::endSlopes(lut);
    assert(slopes[0] == lo && slopes[1] == hi);
  }
  // The slopes follow the curve's own ends: s = 1.375^(b/50) at black, 1 / (1 + 0.084 b) at
  // white, both 1 at b = 0 (b = +1: 1.0064 and 0.9235; lowered, their inverses' slopes).
  const auto up = br::endSlopes(br::modernLut(1.0F));
  assert(std::abs(up[0] - 1.0064F) < 2e-3F && std::abs(up[1] - 0.9235F) < 2e-3F);
  const auto down = br::endSlopes(br::modernLut(-1.0F));
  assert(down[0] < 1.0F && down[1] > 1.0F);
}

int main() {
  test_sample_lut_extended();
  test_matches_photocraft_lut();
  test_zero_is_identity();
  test_sample_lut_clamps();
  test_curve_matches_lut();
  std::puts("test_brightness_port: ok");
  return 0;
}
