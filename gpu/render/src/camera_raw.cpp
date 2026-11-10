// Ported from PhotoCraft crates/algo/src/camera_raw.rs and camera_raw/per_pixel.rs
// @ 885a6ddf27cc58c07faf1722e5c327cd38953657, MIT (elected from MIT OR Apache-2.0).
// Copyright (c) 2026 ArtCraft Team and the PhotoCraft contributors.
// Licence text: third_party/photocraft-LICENSES/LICENSE-MIT.
// Grain from crates/algo/src/photo_util.rs (hash01) as well.
// Modified: rewritten in C++17 for one pixel at a time; stage numbers follow develop().
#include <pixcake/gpu/camera_raw.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>

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
// calibration_weights: triangular weights around 0, 120 and 240 degrees, summing to one.
std::array<float, 3> calibrationWeights(float hue) {
  const float h = remEuclid(hue, 360.0F);
  std::array<float, 3> w{};
  const float centres[3] = {0.0F, 120.0F, 240.0F};
  for (int i = 0; i < 3; ++i) {
    const float d = std::fmin(std::abs(h - centres[i]), 360.0F - std::abs(h - centres[i]));
    w[i] = std::fmax(1.0F - d / 120.0F, 0.0F);
  }
  const float sum = w[0] + w[1] + w[2];
  if (sum > 1e-6F) return {w[0] / sum, w[1] / sum, w[2] / sum};
  return {1.0F, 0.0F, 0.0F};
}

bool calibrationNeutral(const std::array<float, 3>& hue, const std::array<float, 3>& sat) {
  for (int i = 0; i < 3; ++i) {
    if (hue[i] != 0.0F || sat[i] != 0.0F) return false;
  }
  return true;
}

// develop stage 1b (shadow tint is not registered): decode, calibrate, encode(max(v, 0)).
Rgb applyCalibration(const Rgb& encoded, const std::array<float, 3>& hue, const std::array<float, 3>& sat) {
  const Rgb c{srgbToLinear(encoded[0]), srgbToLinear(encoded[1]), srgbToLinear(encoded[2])};
  Rgb o = c;
  const Rgb hsv = rgbToHsv({std::fmax(c[0], 0.0F), std::fmax(c[1], 0.0F), std::fmax(c[2], 0.0F)});
  if (!(hsv[1] <= 1e-5F || hsv[2] <= 1e-6F)) {
    const auto w = calibrationWeights(hsv[0]);
    float dh = 0.0F, ds = 0.0F;
    for (int i = 0; i < 3; ++i) {
      dh += w[i] * std::clamp(hue[i], -100.0F, 100.0F);
      ds += w[i] * std::clamp(sat[i], -100.0F, 100.0F);
    }
    const float nh = hsv[0] + dh / 100.0F * 30.0F;
    const float ns = std::clamp(hsv[1] * (1.0F + ds / 100.0F), 0.0F, 1.0F);
    o = hsvToRgb(nh, ns, hsv[2]);
    const float l0 = luma(c), l1 = luma(o);
    if (l1 > 1e-6F) o = {o[0] * l0 / l1, o[1] * l0 / l1, o[2] * l0 / l1};
  }
  return {linearToSrgb(std::fmax(o[0], 0.0F)), linearToSrgb(std::fmax(o[1], 0.0F)), linearToSrgb(std::fmax(o[2], 0.0F))};
}

bool isLinearCurve(const CurvePoints& points) {
  if (points.size() < 2) return true;
  for (const auto& p : points) {
    if (!(std::abs(p[0] - p[1]) < 1e-3F)) return false;
  }
  return true;
}

bool curvesNeutral(const CurveSettings& s) {
  if (s.highlights != 0.0F || s.lights != 0.0F || s.darks != 0.0F || s.shadows != 0.0F) return false;
  for (const auto& c : s.points) {
    if (!isLinearCurve(c)) return false;
  }
  return true;
}

// curve_lut: monotone cubic (Fritsch-Carlson) through the points (0..255), sorted and with
// inputs closer than 1e-6 (after / 255) merged into the first.
std::vector<float> curveLut(const CurvePoints& points, std::size_t n) {
  std::vector<std::array<float, 2>> p;
  for (const auto& q : points) p.push_back({q[0] / 255.0F, q[1] / 255.0F});
  std::stable_sort(p.begin(), p.end(), [](const auto& a, const auto& b) { return a[0] < b[0]; });
  std::vector<std::array<float, 2>> kept;
  for (const auto& q : p) {
    if (kept.empty() || !(std::abs(q[0] - kept.back()[0]) < 1e-6F)) kept.push_back(q);
  }
  p = std::move(kept);
  std::vector<float> out(n);
  if (p.size() < 2) {
    for (std::size_t k = 0; k < n; ++k) out[k] = static_cast<float>(k) / static_cast<float>(n - 1);
    return out;
  }
  const std::size_t m = p.size();
  std::vector<float> d(m - 1), t(m, 0.0F);
  for (std::size_t k = 0; k + 1 < m; ++k) d[k] = (p[k + 1][1] - p[k][1]) / std::fmax(p[k + 1][0] - p[k][0], 1e-6F);
  t[0] = d[0];
  t[m - 1] = d[m - 2];
  for (std::size_t k = 1; k + 1 < m; ++k) t[k] = d[k - 1] * d[k] <= 0.0F ? 0.0F : (d[k - 1] + d[k]) / 2.0F;
  for (std::size_t k = 0; k + 1 < m; ++k) {
    if (d[k] == 0.0F) {
      t[k] = 0.0F;
      t[k + 1] = 0.0F;
      continue;
    }
    const float a = t[k] / d[k], b = t[k + 1] / d[k];
    const float s = a * a + b * b;
    if (s > 9.0F) {
      const float tau = 3.0F / std::sqrt(s);
      t[k] = tau * a * d[k];
      t[k + 1] = tau * b * d[k];
    }
  }
  for (std::size_t i = 0; i < n; ++i) {
    const float x = static_cast<float>(i) / static_cast<float>(n - 1);
    if (x <= p[0][0]) {
      out[i] = p[0][1];
      continue;
    }
    if (x >= p[m - 1][0]) {
      out[i] = p[m - 1][1];
      continue;
    }
    std::size_t k = m - 2;
    for (std::size_t j = 0; j + 1 < m; ++j) {
      if (x <= p[j + 1][0]) {
        k = j;
        break;
      }
    }
    const float hh = p[k + 1][0] - p[k][0];
    const float s = (x - p[k][0]) / hh;
    const float h00 = 2.0F * s * s * s - 3.0F * s * s + 1.0F, h10 = s * s * s - 2.0F * s * s + s;
    const float h01 = -2.0F * s * s * s + 3.0F * s * s, h11 = s * s * s - s * s;
    out[i] = std::clamp(h00 * p[k][1] + h10 * hh * t[k] + h01 * p[k + 1][1] + h11 * hh * t[k + 1], 0.0F, 1.0F);
  }
  return out;
}

// parametric_lut: each slider (+-100) moves its region by up to a quarter of the range, as a
// Gaussian bump times x (1 - x); kept monotone.
std::vector<float> parametricLut(float highlights, float lights, float darks, float shadows,
                                 const std::array<float, 3>& splits, std::size_t n) {
  std::array<float, 3> s{};
  for (int k = 0; k < 3; ++k) s[k] = std::clamp(splits[k], 5.0F, 95.0F) / 100.0F;
  const std::array<float, 4> centres{s[0] / 2.0F, (s[0] + s[1]) / 2.0F, (s[1] + s[2]) / 2.0F, (s[2] + 1.0F) / 2.0F};
  const std::array<float, 4> amounts{shadows, darks, lights, highlights};
  const std::array<float, 4> widths{std::fmax(s[0], 0.1F), std::fmax(s[1] - s[0], 0.1F), std::fmax(s[2] - s[1], 0.1F),
                                    std::fmax(1.0F - s[2], 0.1F)};
  std::vector<float> lut(n);
  for (std::size_t i = 0; i < n; ++i) {
    const float x = static_cast<float>(i) / static_cast<float>(n - 1);
    float y = x;
    for (int k = 0; k < 4; ++k) {
      const float d = (x - centres[k]) / (widths[k] * 1.2F);
      const float bump = std::exp(-d * d * 2.0F) * x * (1.0F - x) * 4.0F;
      y += amounts[k] / 100.0F * 0.25F * bump;
    }
    lut[i] = std::clamp(y, 0.0F, 1.0F);
  }
  for (std::size_t i = 1; i < n; ++i) lut[i] = std::fmax(lut[i], lut[i - 1]);
  return lut;
}

float lutEval(const std::vector<float>& lut, float v) {
  if (!(v >= 0.0F && v <= 1.0F)) return v < 0.0F ? v + lut.front() : lut.back() + (v - 1.0F);
  const float x = v * static_cast<float>(lut.size() - 1);
  const std::size_t i = std::min(static_cast<std::size_t>(x), lut.size() - 2);
  const float t = x - static_cast<float>(i);
  return lut[i] + (lut[i + 1] - lut[i]) * t;
}

Curves resolveCurves(const CurveSettings& s) {
  Curves c;
  if (s.highlights != 0.0F || s.lights != 0.0F || s.darks != 0.0F || s.shadows != 0.0F) {
    c.parametric = parametricLut(s.highlights, s.lights, s.darks, s.shadows, s.splits, kCurveLut);
  }
  for (int k = 0; k < 4; ++k) {
    if (!isLinearCurve(s.points[k])) c.point[k] = curveLut(s.points[k], kCurveLut);
  }
  return c;
}

Rgb applyCurves(const Rgb& in, const Curves& curves) {
  Rgb c = in;
  if (!curves.parametric.empty()) {
    for (float& v : c) v = lutEval(curves.parametric, v);
  }
  if (!curves.point[0].empty()) {
    for (float& v : c) v = lutEval(curves.point[0], v);
  }
  for (int ch = 0; ch < 3; ++ch) {
    if (!curves.point[ch + 1].empty()) c[ch] = lutEval(curves.point[ch + 1], c[ch]);
  }
  return c;
}

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

float hash01(std::int64_t x, std::int64_t y, std::uint64_t seed) {
  std::uint64_t z = static_cast<std::uint64_t>(x) * 0x9E3779B97F4A7C15ULL ^
                    static_cast<std::uint64_t>(y) * 0xC2B2AE3D27D4EB4FULL ^ seed * 0x165667B19E3779F9ULL;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  z ^= z >> 31;
  return static_cast<float>(z >> 40) / static_cast<float>(1ULL << 24);
}

float valueNoise(float x, float y, std::uint64_t seed) {
  const float xi = std::floor(x), yi = std::floor(y);
  const float tx = x - xi, ty = y - yi;
  const float sx = tx * tx * (3.0F - 2.0F * tx), sy = ty * ty * (3.0F - 2.0F * ty);
  const auto ix = static_cast<std::int64_t>(xi), iy = static_cast<std::int64_t>(yi);
  const auto h = [&](std::int64_t dx, std::int64_t dy) { return hash01(ix + dx, iy + dy, seed); };
  const float top = h(0, 0) + (h(1, 0) - h(0, 0)) * sx;
  const float bot = h(0, 1) + (h(1, 1) - h(0, 1)) * sx;
  return top + (bot - top) * sy;
}

Grain grainOf(float amount, float size, float roughness, std::uint64_t seed, float pixel_scale) {
  Grain g;
  g.cell = 1.0F + size / 25.0F;
  g.roughness = roughness / 100.0F;
  g.amount = amount / 100.0F * 0.12F;
  g.seed = seed;
  g.scale = 1.0F / std::clamp(pixel_scale, 0.01F, 1.0F);
  return g;
}

float grainNoise(const Grain& g, float x, float y) {
  // Sample in full-resolution coordinates so previews match the result.
  const float fx = x * g.scale / g.cell, fy = y * g.scale / g.cell;
  return valueNoise(fx, fy, g.seed) * (1.0F - g.roughness) +
         (valueNoise(fx * 2.3F, fy * 2.3F, g.seed + 7) - 0.0F) * g.roughness;
}

float grainWeight(const Rgb& c) {
  const float l = std::clamp(luma(c), 0.0F, 1.0F);
  return 0.4F + 2.4F * l * (1.0F - l);
}

Rgb applyGrain(const Rgb& c, const Grain& g, float x, float y) {
  const float n = grainNoise(g, x, y);
  const float v = (n - 0.5F) * 2.0F * g.amount * grainWeight(c);
  return {c[0] + v, c[1] + v, c[2] + v};
}

}  // namespace pixcake::gpu::camera_raw
