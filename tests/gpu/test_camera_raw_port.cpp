// PixCake's C++ port of PhotoCraft's Camera Raw per-pixel stages (gpu/render/src/camera_raw.cpp).
// Analytic checks first; test_matches_photocraft_vectors compares against PhotoCraft's own output.
#include <pixcake/gpu/camera_raw.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>

namespace {

using pixcake::gpu::camera_raw::bandWeights;
using pixcake::gpu::camera_raw::developPixel;
using pixcake::gpu::camera_raw::isIdentity;
using pixcake::gpu::camera_raw::linearToSrgb;
using pixcake::gpu::camera_raw::Rgb;
using pixcake::gpu::camera_raw::Settings;
using pixcake::gpu::camera_raw::srgbToLinear;
using pixcake::gpu::camera_raw::whiteBalanceGains;

bool near(float a, float b, float tol = 1e-6F) { return std::fabs(a - b) <= tol; }

void test_identity() {  // default settings (blending 50 included) leave pixels bit for bit
  Settings s;
  assert(isIdentity(s));
  for (Rgb c : {Rgb{0.2F, 0.4F, 0.6F}, Rgb{1.0F, 0.0F, 0.5F}}) assert(developPixel(c, s) == c);
}

void test_white_balance_gains() {
  const Rgb g = whiteBalanceGains(100.0F, -100.0F);
  assert(near(g[0], 1.35F) && near(g[1], 1.25F) && near(g[2], 0.65F));
}

void test_exposure_one_stop() {  // linear middle grey doubles
  Settings s;
  s.exposure = 1.0F;
  const float in = linearToSrgb(0.18F);
  const Rgb out = developPixel({in, in, in}, s);
  assert(near(srgbToLinear(out[0]), 0.36F, 1e-5F));
}

void test_contrast_black() {  // l0 <= 1e-5 takes the additive branch; clipped back to 0
  Settings s;
  s.contrast = 100.0F;
  assert(developPixel({0.0F, 0.0F, 0.0F}, s) == (Rgb{0.0F, 0.0F, 0.0F}));
}

void test_contrast_keeps_mid_grey() {  // sin(2*pi*0.5) = 0
  Settings s;
  s.contrast = 100.0F;
  const Rgb out = developPixel({0.5F, 0.5F, 0.5F}, s);
  assert(near(out[0], 0.5F, 1e-6F));
}

void test_band_weights() {
  const auto w0 = bandWeights(0.0F);
  assert(near(w0[0], 1.0F));
  const auto w15 = bandWeights(15.0F);
  assert(near(w15[0], 0.5F) && near(w15[1], 0.5F));
  const auto w359 = bandWeights(359.0F);
  assert(near(w359[0] + w359[7], 1.0F) && w359[0] > w359[7]);
}

void test_hsl_leaves_greys() {
  Settings s;
  s.hsl_hue.fill(100.0F);
  s.hsl_sat.fill(100.0F);
  s.hsl_lum.fill(100.0F);
  const Rgb g{0.4F, 0.4F, 0.4F};
  assert(developPixel(g, s) == g);
}

void test_saturation_minus_100_greys_out() {
  Settings s;
  s.saturation = -100.0F;
  const Rgb out = developPixel({0.8F, 0.2F, 0.1F}, s);
  assert(near(out[0], out[1], 1e-6F) && near(out[1], out[2], 1e-6F));
}

void test_grade_global_tint_direction() {  // a red global wheel raises R and lowers B
  Settings s;
  s.grade[3] = {0.0F, 100.0F, 0.0F};
  const Rgb out = developPixel({0.5F, 0.5F, 0.5F}, s);
  assert(out[0] > 0.5F && out[2] < 0.5F);
}

// PhotoCraft's own develop() of 73 pixels under 28 settings (tools/photocraft_vectors).
constexpr int kSettingCount = 44;
struct VectorCase {
  const char* name;
  float settings[kSettingCount];
  float in[3];
  float out[3];
};
const VectorCase kVectors[] = {
#include "data/camera_raw_per_pixel_vectors.inc"
};

// Same order as settings() in tools/photocraft_vectors/src/main.rs.
Settings settingsOf(const float* v) {
  Settings s;
  s.temperature = v[0];
  s.tint = v[1];
  s.exposure = v[2];
  s.contrast = v[3];
  s.vibrance = v[4];
  s.saturation = v[5];
  for (int k = 0; k < 8; ++k) {
    s.hsl_hue[k] = v[6 + k];
    s.hsl_sat[k] = v[14 + k];
    s.hsl_lum[k] = v[22 + k];
  }
  for (int z = 0; z < 4; ++z) s.grade[z] = {v[30 + 3 * z], v[31 + 3 * z], v[32 + 3 * z]};
  s.grade_blending = v[42];
  s.grade_balance = v[43];
  return s;
}

// Both sides are CPU f32 with the same expression order; what is left is libm's pow/sin.
constexpr float kTolerance = 2e-6F;

void test_matches_photocraft_vectors() {
  std::set<std::string> names;
  float worst = 0.0F;
  for (const VectorCase& v : kVectors) {
    names.insert(v.name);
    const Rgb out = developPixel({v.in[0], v.in[1], v.in[2]}, settingsOf(v.settings));
    for (int ch = 0; ch < 3; ++ch) {
      const float d = std::fabs(out[ch] - v.out[ch]);
      worst = std::max(worst, d);
      if (d > kTolerance) {
        std::fprintf(stderr, "%s: in (%.6f %.6f %.6f) ours (%.9f %.9f %.9f) photocraft (%.9f %.9f %.9f)\n", v.name,
                     v.in[0], v.in[1], v.in[2], out[0], out[1], out[2], v.out[0], v.out[1], v.out[2]);
      }
      assert(d <= kTolerance);
    }
  }
  assert(sizeof(kVectors) / sizeof(kVectors[0]) == 28U * 73U);
  assert(names.size() == 28U);
  std::printf("photocraft vectors: %zu pixels, worst difference %.3g\n", sizeof(kVectors) / sizeof(kVectors[0]),
              static_cast<double>(worst));
}

}  // namespace

int main() {
  test_identity();
  test_white_balance_gains();
  test_exposure_one_stop();
  test_contrast_black();
  test_contrast_keeps_mid_grey();
  test_band_weights();
  test_hsl_leaves_greys();
  test_saturation_minus_100_greys_out();
  test_grade_global_tint_direction();
  test_matches_photocraft_vectors();
  std::puts("test_camera_raw_port: ok");
  return 0;
}
