// Ported from PhotoCraft crates/compose/src/adjust/tone.rs, adjust/color.rs and psblend.rs, and
// crates/color/src/blend.rs, @ 885a6ddf27cc58c07faf1722e5c327cd38953657,
// MIT (elected from MIT OR Apache-2.0).
// Copyright (c) 2026 ArtCraft Team and the PhotoCraft contributors.
// Licence text: third_party/photocraft-LICENSES/LICENSE-MIT.
// Modified: rewritten in C++17; RGB documents only (no Lab/CMYK tone spaces, no 32-bit
// float levels, no Lab mixing in the normal composite).
#include <pixcake/gpu/adjust.hpp>

#include <algorithm>
#include <cmath>

namespace pixcake::gpu::adjust {
namespace {

// Exponent of the shadow toe in levelsGamma (fitted by PhotoCraft).
constexpr float kLevelsToePower = 2.2F;

std::size_t index_of(float p) { return std::isnan(p) ? 0 : static_cast<std::size_t>(std::floor(p)); }

float channel(BlendMode mode, float cb, float cs) {
  switch (mode) {
    case BlendMode::Multiply: return cb * cs;
    case BlendMode::Screen: return cb + cs - cb * cs;
    case BlendMode::Normal: break;
  }
  return cs;
}

}  // namespace

float levelsGamma(float u, float g) {
  const float c = 1.0F / g;
  if (g <= 1.0F) return std::pow(u, c);
  const float a = std::pow(2.0F, g), q = kLevelsToePower;
  // Where the toe meets the curve: a*u1 - b*u1^q = u1^c and a - q*b*u1^(q-1) = c*u1^(c-1).
  const float u1 = std::pow((1.0F - c / q) / (a * (1.0F - 1.0F / q)), 1.0F / (1.0F - c));
  if (u >= u1 || u1 <= 0.0F) return std::pow(u, c);
  const float b = (a * u1 - std::pow(u1, c)) / std::pow(u1, q);
  // Just above gamma 1 the toe shrinks to nothing and the power curve is its limit.
  if (!std::isfinite(b)) return std::pow(u, c);
  return a * u - b * std::pow(u, q);
}

float levelsQ(const LevelsChannel& ch, float v, float quantum) {
  const float range = std::max(ch.in_white - ch.in_black, 1e-6F);
  const bool stepped = std::isfinite(quantum) && quantum >= 1.0F;
  const auto whole = [&](float x) { return stepped ? std::floor(x * quantum + 0.5F) / quantum : x; };
  float u;
  if (stepped) {
    const float w = std::round(v * quantum) / quantum;
    u = std::floor(std::clamp((w - ch.in_black) / range, 0.0F, 1.0F) * quantum + 0.5F + 1e-3F) / quantum;
  } else {
    u = std::clamp((v - ch.in_black) / range, 0.0F, 1.0F);
  }
  const float t = whole(levelsGamma(u, std::max(ch.gamma, 0.01F)));
  return whole(ch.out_black + t * (ch.out_white - ch.out_black));
}

std::vector<float> levelsLut(const LevelsChannel& master, float quantum) {
  const LevelsChannel identity;
  std::vector<float> table(kLutSize);
  for (std::size_t k = 0; k < kLutSize; ++k) {
    const float x = static_cast<float>(k) / static_cast<float>(kLutSize - 1);
    table[k] = levelsQ(master, levelsQ(identity, x, quantum), quantum);
  }
  return table;
}

float sampleLut(const std::vector<float>& table, float v) {
  const float x = std::clamp(v, 0.0F, 1.0F) * static_cast<float>(table.size() - 1);
  const std::size_t i = std::isnan(x) ? 0 : static_cast<std::size_t>(x);
  const std::size_t j = std::min(i + 1, table.size() - 1);
  const float f = x - static_cast<float>(i);
  return table[i] * (1.0F - f) + table[j] * f;
}

std::array<float, 9> selectiveWeights(const Rgb& c) {
  const float max = std::max(std::max(c[0], c[1]), c[2]);
  const float min = std::min(std::min(c[0], c[1]), c[2]);
  const float mid = c[0] + c[1] + c[2] - max - min;
  const auto top = [&](int i) { return c[i] >= max ? max - mid : 0.0F; };
  const auto bottom = [&](int i) { return c[i] <= min ? mid - min : 0.0F; };
  return {top(0),
          bottom(2),
          top(1),
          bottom(0),
          top(2),
          bottom(1),
          std::max((min - 0.5F) * 2.0F, 0.0F),
          std::clamp(1.0F - std::fabs(max - 0.5F) - std::fabs(min - 0.5F), 0.0F, 1.0F),
          std::max((0.5F - max) * 2.0F, 0.0F)};
}

Rgb selectiveColor(const Rgb& c, bool relative, const std::array<std::array<float, 4>, 9>& adjustments) {
  const auto w = selectiveWeights(c);
  Rgb delta{0.0F, 0.0F, 0.0F};
  for (std::size_t r = 0; r < 9; ++r) {
    const float wr = w[r];
    if (wr <= 0.0F) continue;
    std::array<float, 4> a{};
    for (int k = 0; k < 4; ++k) a[k] = adjustments[r][k] / 100.0F;
    for (int i = 0; i < 3; ++i) {
      const float ink = 1.0F - c[i];
      float d = a[i] + a[3];
      if (relative) {
        d = d * ink;
      } else if (d > 0.0F) {
        d = d * std::min(2.0F * (1.0F - ink), 1.0F);
      } else {
        d = d * std::min(2.0F * ink, 1.0F);
      }
      delta[i] += d * wr;
    }
  }
  Rgb out{};
  for (int i = 0; i < 3; ++i) out[i] = std::clamp(c[i] - delta[i], 0.0F, 1.0F);
  return out;
}

Rgb lut3dSample(const std::vector<float>& lut, std::size_t n, const Rgb& c, bool tetrahedral) {
  if (n < 2 || lut.size() / 3 / n / n < n) return c;
  const float m = static_cast<float>(n - 1);
  Rgb pos{}, f{};
  std::array<std::size_t, 3> i0{};
  for (int k = 0; k < 3; ++k) {
    pos[k] = std::clamp(c[k], 0.0F, 1.0F) * m;
    i0[k] = std::min(index_of(pos[k]), n - 2);
    f[k] = pos[k] - static_cast<float>(i0[k]);
  }
  const auto at = [&](std::size_t dr, std::size_t dg, std::size_t db) {
    const std::size_t idx = (((i0[2] + db) * n + i0[1] + dg) * n + i0[0] + dr) * 3;
    return Rgb{lut[idx], lut[idx + 1], lut[idx + 2]};
  };
  using Term = std::pair<float, Rgb>;
  const auto mix = [](std::initializer_list<Term> w) {
    Rgb out{};
    for (int k = 0; k < 3; ++k) {
      float sum = 0.0F;
      for (const auto& [a, v] : w) sum += a * v[k];
      out[k] = sum;
    }
    return out;
  };
  const float fr = f[0], fg = f[1], fb = f[2];
  if (tetrahedral) {
    const Rgb c000 = at(0, 0, 0), c111 = at(1, 1, 1);
    if (fr > fg) {
      if (fg > fb) return mix({{1.0F - fr, c000}, {fr - fg, at(1, 0, 0)}, {fg - fb, at(1, 1, 0)}, {fb, c111}});
      if (fr > fb) return mix({{1.0F - fr, c000}, {fr - fb, at(1, 0, 0)}, {fb - fg, at(1, 0, 1)}, {fg, c111}});
      return mix({{1.0F - fb, c000}, {fb - fr, at(0, 0, 1)}, {fr - fg, at(1, 0, 1)}, {fg, c111}});
    }
    if (fb > fg) return mix({{1.0F - fb, c000}, {fb - fg, at(0, 0, 1)}, {fg - fr, at(0, 1, 1)}, {fr, c111}});
    if (fb > fr) return mix({{1.0F - fg, c000}, {fg - fb, at(0, 1, 0)}, {fb - fr, at(0, 1, 1)}, {fr, c111}});
    return mix({{1.0F - fg, c000}, {fg - fr, at(0, 1, 0)}, {fr - fb, at(1, 1, 0)}, {fb, c111}});
  }
  const auto lerp = [](const Rgb& a, const Rgb& b, float t) {
    return Rgb{a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t};
  };
  const Rgb c00 = lerp(at(0, 0, 0), at(1, 0, 0), fr);
  const Rgb c10 = lerp(at(0, 1, 0), at(1, 1, 0), fr);
  const Rgb c01 = lerp(at(0, 0, 1), at(1, 0, 1), fr);
  const Rgb c11 = lerp(at(0, 1, 1), at(1, 1, 1), fr);
  return lerp(lerp(c00, c10, fg), lerp(c01, c11, fg), fb);
}

Rgba composite(BlendMode mode, const Rgba& backdrop, const Rgba& source, float opacity) {
  const float ab = backdrop[3];
  const float as = source[3] * opacity;
  if (as <= 0.0F) return backdrop;
  const float ao = as + ab * (1.0F - as);
  if (ao <= 0.0F) return {0.0F, 0.0F, 0.0F, 0.0F};
  Rgba out{};
  if (mode == BlendMode::Normal) {
    const float kb = ab * (1.0F - as);
    const float inv = 1.0F / ao;
    for (int i = 0; i < 3; ++i) out[i] = (kb * backdrop[i] + as * source[i]) * inv;
  } else {
    for (int i = 0; i < 3; ++i) {
      const float b = channel(mode, backdrop[i], source[i]);
      out[i] = ((1.0F - as) * ab * backdrop[i] + (1.0F - ab) * as * source[i] + as * ab * b) / ao;
    }
  }
  out[3] = ao;
  return out;
}

}  // namespace pixcake::gpu::adjust
