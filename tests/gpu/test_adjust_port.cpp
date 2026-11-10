// PixCake's C++ port of PhotoCraft's Photoshop adjustment-layer functions (gpu/render/src/adjust.cpp):
// Levels, Selective Color, 3D LUT sampling and the normal / multiply / screen composite, checked
// against PhotoCraft's own output (tools/photocraft_vectors adjust).
#include <pixcake/gpu/adjust.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>

namespace {

namespace adj = pixcake::gpu::adjust;
#include "data/adjust_vectors.inc"

constexpr float kTolerance = 2e-6F;
float worst = 0.0F;
void check(float got, float want, const char* what) {
  const float d = std::fabs(got - want);
  worst = std::max(worst, d);
  if (!(d <= kTolerance)) {
    std::printf("%s: got %.9g want %.9g\n", what, got, want);
    assert(false);
  }
}
// The tool generates the same table (crates/compose sampling vectors).
std::vector<float> lut_table(std::size_t n) {
  std::vector<float> t;
  for (std::size_t b = 0; b < n; ++b)
    for (std::size_t g = 0; g < n; ++g)
      for (std::size_t r = 0; r < n; ++r) {
        const float c[3] = {r / float(n - 1), g / float(n - 1), b / float(n - 1)};
        t.push_back(std::clamp(c[1] + 0.1F * std::sin(6.0F * c[0]), 0.0F, 1.0F));
        t.push_back(std::clamp(c[2] + 0.1F * std::cos(5.0F * c[1]), 0.0F, 1.0F));
        t.push_back(std::clamp(c[0] * 0.8F + 0.1F, 0.0F, 1.0F));
      }
  return t;
}

void test_levels_matches_photocraft() {
  for (const auto& c : kLevels) {
    const adj::LevelsChannel ch{c.in_black, c.in_white, c.gamma, c.out_black, c.out_white};
    const auto lut = adj::levelsLut(ch, 255.0F);
    assert(lut.size() == 4096);
    for (std::size_t k = 0; k < 512; ++k) check(lut[k * 8], c.table[k], c.name);
  }
}

void test_levels_identity() {  // default channel: every sample rounds to its whole level
  const adj::LevelsChannel ch;
  for (int k = 0; k <= 255; ++k) {
    const float v = k / 255.0F + 0.3F / 255.0F;
    assert(std::fabs(adj::levelsQ(ch, v, 255.0F) - k / 255.0F) < 1e-6F);
  }
  assert(adj::levelsGamma(0.25F, 1.0F) == 0.25F);
}

void test_selective_matches_photocraft() {
  for (const auto& c : kSelective) {
    std::array<std::array<float, 4>, 9> a{};
    a[c.target][c.ink] = c.amount;
    const auto o = adj::selectiveColor({c.in[0], c.in[1], c.in[2]}, c.relative, a);
    for (int i = 0; i < 3; ++i) check(o[i], c.out[i], "selective");
  }
}

void test_selective_relative_leaves_white() {  // relative mode scales ink that is there
  std::array<std::array<float, 4>, 9> a{};
  a[6] = {100, 100, 100, 0};
  const auto o = adj::selectiveColor({1, 1, 1}, true, a);
  assert(o[0] == 1 && o[1] == 1 && o[2] == 1);
}

void test_lut3d_matches_photocraft() {
  const auto table = lut_table(17);
  for (const auto& c : kLut) {
    const auto o = adj::lut3dSample(table, 17, {c.in[0], c.in[1], c.in[2]}, c.tetrahedral);
    for (int i = 0; i < 3; ++i) check(o[i], c.out[i], c.tetrahedral ? "lut tetrahedral" : "lut trilinear");
  }
}

void test_composite_matches_photocraft() {
  const adj::BlendMode modes[] = {adj::BlendMode::Normal, adj::BlendMode::Multiply, adj::BlendMode::Screen};
  for (const auto& c : kComposite) {
    const auto o = adj::composite(modes[c.mode], {c.backdrop[0], c.backdrop[1], c.backdrop[2], c.backdrop[3]},
                                  {c.source[0], c.source[1], c.source[2], c.source[3]}, c.opacity);
    for (int i = 0; i < 4; ++i) check(o[i], c.out[i], "composite");
  }
}

}  // namespace

int main() {
  test_levels_identity();
  test_selective_relative_leaves_white();
  test_levels_matches_photocraft();
  test_selective_matches_photocraft();
  test_lut3d_matches_photocraft();
  test_composite_matches_photocraft();
  std::printf("test_adjust_port: ok (worst %.3g)\n", worst);
  return 0;
}
