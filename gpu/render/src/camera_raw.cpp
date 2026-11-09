// Ported from PhotoCraft crates/algo/src/camera_raw.rs and camera_raw/per_pixel.rs
// @ 885a6ddf27cc58c07faf1722e5c327cd38953657, MIT (elected from MIT OR Apache-2.0).
// Copyright (c) 2026 ArtCraft Team and the PhotoCraft contributors.
// Modified: rewritten in C++17 for one pixel at a time; stage numbers follow develop().
#include <pixcake/gpu/camera_raw.hpp>

#include <algorithm>
#include <cmath>

namespace pixcake::gpu::camera_raw {
namespace {

constexpr float kTau = 6.28318530717958647692F;

bool wbNeutral(const Settings& s) { return s.temperature == 0.0F && s.tint == 0.0F && s.exposure == 0.0F; }

// Stage 1: white balance and exposure as gains in linear light.
Rgb balance(Rgb c, const Settings& s) {
  const Rgb g = whiteBalanceGains(s.temperature, s.tint);
  const float k = std::exp2(s.exposure) / luma(g);
  for (int i = 0; i < 3; ++i) c[i] = linearToSrgb(std::max(srgbToLinear(c[i]) * g[i] * k, 0.0F));
  return c;
}

// Stage 3 with Contrast as its only control: an S-curve on luminance, colour ratios kept.
Rgb contrast(Rgb c, float k_con) {
  const float l0 = std::max(luma(c), 0.0F);
  const float lc = std::min(l0, 1.0F);
  float lv = lc - k_con * std::sin(kTau * std::clamp(lc, 0.0F, 1.0F)) / kTau;
  lv = std::max(lv, 0.0F) + (l0 - lc);
  const float ratio = l0 > 1e-5F ? lv / l0 : 1.0F;
  for (float& v : c) v = l0 > 1e-5F ? v * ratio : v + (lv - l0);
  return c;
}

}  // namespace

bool isIdentity(const Settings& s) { return wbNeutral(s) && s.contrast == 0.0F; }

float srgbToLinear(float v) { return v <= 0.04045F ? v / 12.92F : std::pow((v + 0.055F) / 1.055F, 2.4F); }

float linearToSrgb(float v) { return v <= 0.0031308F ? v * 12.92F : 1.055F * std::pow(v, 1.0F / 2.4F) - 0.055F; }

float luma(const Rgb& c) { return 0.2126F * c[0] + 0.7152F * c[1] + 0.0722F * c[2]; }

Rgb whiteBalanceGains(float temperature, float tint) {
  const float t = temperature / 100.0F;
  const float tn = tint / 100.0F;
  return {1.0F + 0.35F * t, 1.0F - 0.25F * tn, 1.0F - 0.35F * t};
}

Rgb developPixel(const Rgb& encoded, const Settings& s) {
  if (isIdentity(s)) return encoded;
  Rgb c = encoded;
  if (!wbNeutral(s)) c = balance(c, s);
  if (s.contrast != 0.0F) c = contrast(c, s.contrast / 100.0F * 0.6F);
  for (float& v : c) v = std::clamp(v, 0.0F, 1.0F);
  return c;
}

}  // namespace pixcake::gpu::camera_raw
