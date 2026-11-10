// Ported from PhotoCraft crates/algo/src/camera_raw.rs and camera_raw/per_pixel.rs
// @ 885a6ddf27cc58c07faf1722e5c327cd38953657, MIT (elected from MIT OR Apache-2.0).
// Copyright (c) 2026 ArtCraft Team and the PhotoCraft contributors.
// Licence text: third_party/photocraft-LICENSES/LICENSE-MIT.
// Modified: rewritten in C++17 for one pixel at a time; stage numbers follow develop().
#include <pixcake/gpu/camera_raw.hpp>

#include <algorithm>
#include <cmath>

namespace pixcake::gpu::camera_raw {
namespace {

constexpr float kTau = 6.28318530717958647692F;
constexpr std::array<float, 8> kBands = {0.0F, 30.0F, 60.0F, 120.0F, 180.0F, 240.0F, 270.0F, 300.0F};

// Rust's f32::rem_euclid: the non-negative remainder.
float remEuclid(float x, float m) {
  const float r = std::fmod(x, m);
  return r < 0.0F ? r + std::fabs(m) : r;
}

float smoothstep(float e0, float e1, float x) {
  const float t = std::clamp((x - e0) / (e1 - e0), 0.0F, 1.0F);
  return t * t * (3.0F - 2.0F * t);
}

Rgb rgbToHsv(const Rgb& c) {
  const float mx = std::max({c[0], c[1], c[2]});
  const float mn = std::min({c[0], c[1], c[2]});
  const float d = mx - mn;
  float h = 0.0F;
  if (d <= 1e-9F) {
    h = 0.0F;
  } else if (mx == c[0]) {
    h = 60.0F * remEuclid((c[1] - c[2]) / d, 6.0F);
  } else if (mx == c[1]) {
    h = 60.0F * ((c[2] - c[0]) / d + 2.0F);
  } else {
    h = 60.0F * ((c[0] - c[1]) / d + 4.0F);
  }
  return {h, mx > 1e-9F ? d / mx : 0.0F, mx};
}

bool wbNeutral(const Settings& s) { return s.temperature == 0.0F && s.tint == 0.0F && s.exposure == 0.0F; }

}  // namespace

bool isIdentity(const Settings& s) {
  return wbNeutral(s) && s.contrast == 0.0F && s.vibrance == 0.0F && s.saturation == 0.0F &&
         colorMixerNeutral(s.hsl_hue, s.hsl_sat, s.hsl_lum) && gradeNeutral(s.grade);
}

Rgb balanceGains(float temperature, float tint, float exposure) {
  const Rgb g = whiteBalanceGains(temperature, tint);
  const float norm = luma(g);
  Rgb gains{};
  for (int i = 0; i < 3; ++i) gains[i] = g[i] / norm * std::pow(2.0F, exposure);
  return gains;
}

Rgb applyBalance(const Rgb& encoded, const Rgb& gains) {
  Rgb c = encoded;
  for (int i = 0; i < 3; ++i) c[i] = linearToSrgb(std::max(srgbToLinear(c[i]) * gains[i], 0.0F));
  return c;
}

// An S-curve on luminance, colour ratios kept.
Rgb applyContrast(const Rgb& in, float contrast) {
  const float k_con = contrast / 100.0F * 0.6F;
  Rgb c = in;
  const float l0 = std::max(luma(c), 0.0F);
  const float lc = std::min(l0, 1.0F);
  float lv = lc - k_con * std::sin(kTau * std::clamp(lc, 0.0F, 1.0F)) / kTau;
  lv = std::max(lv, 0.0F) + (l0 - lc);
  const float ratio = l0 > 1e-5F ? lv / l0 : 1.0F;
  for (float& v : c) v = l0 > 1e-5F ? v * ratio : v + (lv - l0);
  return c;
}

// Vibrance weighted towards less saturated, non-skin colours.
Rgb applyPresence(const Rgb& in, float vibrance, float saturation) {
  Rgb c = in;
  const float l = luma(c);
  const float mx = std::max({c[0], c[1], c[2]});
  const float mn = std::min({c[0], c[1], c[2]});
  const float sat = mx > 1e-6F ? (mx - mn) / mx : 0.0F;
  const float hue = rgbToHsv(c)[0];
  const float skin = std::clamp(1.0F - std::fabs(hue - 25.0F) / 25.0F, 0.0F, 1.0F) * 0.5F;
  const float k = 1.0F + saturation / 100.0F + vibrance / 100.0F * (1.0F - sat) * (1.0F - skin);
  for (float& v : c) v = l + (v - l) * std::max(k, 0.0F);
  return c;
}

bool colorMixerNeutral(const std::array<float, 8>& hue, const std::array<float, 8>& sat,
                       const std::array<float, 8>& lum) {
  for (int k = 0; k < 8; ++k) {
    if (hue[k] != 0.0F || sat[k] != 0.0F || lum[k] != 0.0F) return false;
  }
  return true;
}

Rgb applyColorMixer(const Rgb& c, const std::array<float, 8>& hue, const std::array<float, 8>& sat,
                    const std::array<float, 8>& lum) {
  const Rgb hsv = rgbToHsv({std::max(c[0], 0.0F), std::max(c[1], 0.0F), std::max(c[2], 0.0F)});
  if (hsv[1] <= 1e-4F) return c;
  const std::array<float, 8> w = bandWeights(hsv[0]);
  float dh = 0.0F, ds = 0.0F, dl = 0.0F;
  for (int k = 0; k < 8; ++k) {
    dh += w[k] * hue[k];
    ds += w[k] * sat[k];
    dl += w[k] * lum[k];
  }
  const float nh = hsv[0] + dh / 100.0F * 30.0F;
  const float ns = std::clamp(hsv[1] * (1.0F + ds / 100.0F), 0.0F, 1.0F);
  Rgb out = hsvToRgb(nh, ns, hsv[2]);
  const float l0 = luma(c);
  const float l1 = luma(out);
  if (l1 > 1e-6F) {
    for (float& v : out) v = v * l0 / l1;
  }
  const float k = std::pow(2.0F, dl / 100.0F * 1.0F * hsv[1]);
  for (float& v : out) v = v * k;
  return out;
}

bool gradeNeutral(const std::array<Wheel, 4>& wheels) {
  for (const Wheel& w : wheels) {
    if (w.sat != 0.0F || w.lum != 0.0F) return false;
  }
  return true;
}

Grade resolveGrade(const std::array<Wheel, 4>& wheels, float blending, float balance) {
  Grade g;
  for (int k = 0; k < 4; ++k) {
    const Rgb hue = hsvToRgb(wheels[k].hue, 1.0F, 1.0F);
    const float l = luma(hue);
    for (int ch = 0; ch < 3; ++ch) g.tints[k][ch] = (hue[ch] - l) * wheels[k].sat / 100.0F * 0.3F;
    g.lums[k] = wheels[k].lum;
  }
  g.blend = 0.15F + std::clamp(blending, 0.0F, 100.0F) / 100.0F * 0.35F;
  g.balance = std::clamp(balance, -100.0F, 100.0F) / 100.0F * 0.25F;
  return g;
}

// Shadows / midtones / highlights / global wheels.
Rgb applyColorGrading(const Rgb& in, const Grade& g) {
  Rgb c = in;
  const float l = std::clamp(luma(c), 0.0F, 1.0F);
  const float sh = 1.0F - smoothstep(0.0F, 0.5F + g.balance + g.blend, l);
  const float hi = smoothstep(0.5F + g.balance - g.blend, 1.0F, l);
  const float mid = std::max(1.0F - sh - hi, 0.0F);
  const std::array<float, 4> masks = {sh, mid, hi, 1.0F};
  for (int k = 0; k < 4; ++k) {
    for (int ch = 0; ch < 3; ++ch) c[ch] += masks[k] * g.tints[k][ch];
    if (g.lums[k] != 0.0F) {
      const float f = std::pow(2.0F, masks[k] * g.lums[k] / 100.0F * 0.6F);
      for (float& v : c) v = v * f;
    }
  }
  return c;
}

std::array<float, 8> bandWeights(float hue) {
  std::array<float, 8> w{};
  for (int k = 0; k < 8; ++k) {
    const float c = kBands[k];
    const float prev = kBands[(k + 7) % 8];
    const float next = kBands[(k + 1) % 8];
    const float diff = std::fmod(hue - c + 540.0F, 360.0F) - 180.0F;
    const float span = diff >= 0.0F ? remEuclid(next - c, 360.0F) : remEuclid(c - prev, 360.0F);
    w[k] = std::max(1.0F - std::fabs(diff) / std::max(span, 1e-3F), 0.0F);
  }
  return w;
}

Rgb hsvToRgb(float h, float s, float v) {
  const float hh = remEuclid(h, 360.0F) / 60.0F;
  const float c = v * s;
  const float x = c * (1.0F - std::fabs(std::fmod(hh, 2.0F) - 1.0F));
  float r = 0.0F, g = 0.0F, b = 0.0F;
  switch (static_cast<unsigned>(hh)) {
    case 0: r = c; g = x; break;
    case 1: r = x; g = c; break;
    case 2: g = c; b = x; break;
    case 3: g = x; b = c; break;
    case 4: r = x; b = c; break;
    default: r = c; b = x; break;
  }
  const float m = v - c;
  return {r + m, g + m, b + m};
}

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
  if (!wbNeutral(s)) c = applyBalance(c, balanceGains(s.temperature, s.tint, s.exposure));
  if (s.contrast != 0.0F) c = applyContrast(c, s.contrast);
  if (s.vibrance != 0.0F || s.saturation != 0.0F) c = applyPresence(c, s.vibrance, s.saturation);
  if (!colorMixerNeutral(s.hsl_hue, s.hsl_sat, s.hsl_lum)) c = applyColorMixer(c, s.hsl_hue, s.hsl_sat, s.hsl_lum);
  if (!gradeNeutral(s.grade)) c = applyColorGrading(c, resolveGrade(s.grade, s.grade_blending, s.grade_balance));
  for (float& v : c) v = std::clamp(v, 0.0F, 1.0F);
  return c;
}

}  // namespace pixcake::gpu::camera_raw
