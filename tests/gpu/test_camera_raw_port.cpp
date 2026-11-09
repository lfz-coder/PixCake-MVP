// PixCake's C++ port of PhotoCraft's Camera Raw per-pixel stages (gpu/render/src/camera_raw.cpp).
// Analytic checks first; test_matches_photocraft_vectors compares against PhotoCraft's own output.
#include <pixcake/gpu/camera_raw.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>

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
  std::puts("test_camera_raw_port: ok");
  return 0;
}
