// Ported from PhotoCraft crates/compose/src/adjust/tone.rs @ 885a6ddf27cc58c07faf1722e5c327cd38953657,
// MIT (elected from MIT OR Apache-2.0).
// Copyright (c) 2026 ArtCraft Team and the PhotoCraft contributors.
// Licence text: third_party/photocraft-LICENSES/LICENSE-MIT.
// Modified: rewritten in C++17; modern_contrast is left out (PixCake passes contrast 0).
#include <pixcake/gpu/brightness.hpp>

#include <algorithm>
#include <cmath>

namespace pixcake::gpu::brightness {
namespace {

// Output level of the curve's knee.
constexpr float kKnee = 126.0F / 255.0F;

float step(std::size_t k, std::size_t n) { return static_cast<float>(k) / static_cast<float>(n - 1); }

}  // namespace

Curve curve(float brightness) {
  const float b = std::fabs(brightness);
  Curve c;
  c.raise = brightness > 0.0F;
  c.s = std::pow(1.375F, b / 50.0F);
  c.e = 1.0F / (1.0F + 0.084F * b);
  c.x0 = std::min(kKnee / c.s, 1.0F);
  const float secant = (1.0F - kKnee) / std::max(1.0F - c.x0, 1e-6F);
  c.alpha = c.s / secant;
  const float beta = c.e / secant;
  c.hermite = c.alpha * c.alpha + beta * beta <= 9.0F;
  return c;
}

// The line, then a monotone roll-off to (1, 1).
float raised(const Curve& c, float x) {
  const float y0 = kKnee;
  if (x <= c.x0) return std::min(y0, c.s * x);
  const float h = std::max(1.0F - c.x0, 1e-6F);
  const float t = std::clamp((x - c.x0) / h, 0.0F, 1.0F);
  float y = 0.0F;
  if (c.hermite) {
    const float t2 = t * t;
    const float t3 = t * t * t;
    y = (2.0F * t3 - 3.0F * t2 + 1.0F) * y0 + (t3 - 2.0F * t2 + t) * h * c.s + (-2.0F * t3 + 3.0F * t2) +
        (t3 - t2) * h * c.e;
  } else {
    y = 1.0F - (1.0F - y0) * std::pow(1.0F - t, c.alpha);
  }
  return std::clamp(y, 0.0F, 1.0F);
}

std::vector<float> modernLut(float brightness) {
  std::vector<float> table(kLutSize);
  if (brightness == 0.0F || !std::isfinite(brightness)) {
    for (std::size_t k = 0; k < kLutSize; ++k) table[k] = step(k, kLutSize);
  } else if (brightness > 0.0F) {
    const Curve c = curve(brightness);
    for (std::size_t k = 0; k < kLutSize; ++k) table[k] = raised(c, step(k, kLutSize));
  } else {
    // Lowered: invert the raised curve, sampled four times finer, in one monotone pass.
    const Curve c = curve(-brightness);
    const std::size_t fine = 4 * kLutSize;
    std::vector<float> ys(fine);
    for (std::size_t j = 0; j < fine; ++j) ys[j] = raised(c, step(j, fine));
    std::size_t j = 0;
    for (std::size_t k = 0; k < kLutSize; ++k) {
      const float y = step(k, kLutSize);
      while (j + 2 < fine && ys[j + 1] < y) ++j;
      const float y0 = ys[j];
      const float y1 = ys[j + 1];
      const float f = y1 > y0 ? std::clamp((y - y0) / (y1 - y0), 0.0F, 1.0F) : 0.0F;
      table[k] = std::clamp(step(j, fine) + f / static_cast<float>(fine - 1), 0.0F, 1.0F);
    }
  }
  // modern_contrast(b, 0) is the identity; the final clamp stays.
  for (float& v : table) v = std::clamp(v, 0.0F, 1.0F);
  return table;
}

float sampleLut(const std::vector<float>& table, float v) {
  const float x = std::clamp(v, 0.0F, 1.0F) * static_cast<float>(table.size() - 1);
  const std::size_t i = std::isnan(x) ? 0 : static_cast<std::size_t>(x);
  const std::size_t j = std::min(i + 1, table.size() - 1);
  const float f = x - static_cast<float>(i);
  return table[i] * (1.0F - f) + table[j] * f;
}

std::array<float, 2> endSlopes(const std::vector<float>& table) {
  const std::size_t n = table.size();
  if (n < 2) return {1.0F, 1.0F};
  const std::size_t k = std::min(kSlopeSpan, n - 1);
  const float per_unit = static_cast<float>(n - 1) / static_cast<float>(k);
  return {(table[k] - table[0]) * per_unit, (table[n - 1] - table[n - 1 - k]) * per_unit};
}

float sampleLutExtended(const std::vector<float>& table, float v) {
  const std::size_t n = table.size();
  if (!std::isfinite(v) || (v >= 0.0F && v <= 1.0F) || n < 2) return sampleLut(table, v);
  const auto slopes = endSlopes(table);
  return v < 0.0F ? table[0] + v * slopes[0] : table[n - 1] + (v - 1.0F) * slopes[1];
}

}  // namespace pixcake::gpu::brightness
