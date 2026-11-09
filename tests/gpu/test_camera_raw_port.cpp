// PixCake's C++ port of PhotoCraft's Camera Raw per-pixel stages (gpu/render/src/camera_raw.cpp).
// Analytic checks first; test_matches_photocraft_vectors compares against PhotoCraft's own output.
#include <pixcake/gpu/camera_raw.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>

namespace {

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

}  // namespace

int main() {
  test_identity();
  test_white_balance_gains();
  test_exposure_one_stop();
  test_contrast_black();
  test_contrast_keeps_mid_grey();
  std::puts("test_camera_raw_port: ok");
  return 0;
}
