// PixCake's port of PhotoCraft's Camera Raw stage 5 (parametric tone curve, master/red/green/blue
// point curves) in camera_raw.cpp, against PhotoCraft's own tables and develop
// (tools/photocraft_vectors/src/curves.rs).
#include <pixcake/gpu/camera_raw.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#define CHECK(x)                                                              \
  do {                                                                        \
    if (!(x)) {                                                               \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x);      \
      std::abort();                                                           \
    }                                                                         \
  } while (0)

namespace {

namespace cr = pixcake::gpu::camera_raw;
#include "data/curve_vectors.inc"

cr::CurvePoints points_of(const std::vector<float>& flat) {
  cr::CurvePoints out;
  for (std::size_t i = 0; i + 1 < flat.size(); i += 2) out.push_back({flat[i], flat[i + 1]});
  return out;
}

cr::CurveSettings settings_of(const CurveCase& c) {
  cr::CurveSettings s;
  s.highlights = c.highlights;
  s.lights = c.lights;
  s.darks = c.darks;
  s.shadows = c.shadows;
  s.splits = {c.splits[0], c.splits[1], c.splits[2]};
  s.points = {points_of(c.master), points_of(c.red), points_of(c.green), points_of(c.blue)};
  return s;
}

// Entries 0, 4, ..., 1020 and 1023 of a table, as the vectors keep them.
std::vector<float> sampled(const std::vector<float>& lut) {
  if (lut.empty()) return {};
  std::vector<float> out;
  for (std::size_t i = 0; i < lut.size(); i += 4) out.push_back(lut[i]);
  out.push_back(lut.back());
  return out;
}

int differing(const std::vector<float>& a, const std::vector<float>& b) {
  if (a.size() != b.size()) return 1 << 20;
  int n = 0;
  for (std::size_t i = 0; i < a.size(); ++i) n += a[i] != b[i];
  return n;
}

void test_tables_match_photocraft() {
  for (const auto& c : kCurves) {
    const cr::Curves curves = cr::resolveCurves(settings_of(c));
    const int d = differing(sampled(curves.parametric), c.parametric_lut) + differing(sampled(curves.point[0]), c.master_lut) +
                  differing(sampled(curves.point[1]), c.red_lut) + differing(sampled(curves.point[2]), c.green_lut) +
                  differing(sampled(curves.point[3]), c.blue_lut);
    std::printf("curve tables %-28s %d entries differ\n", c.name, d);
    CHECK(d == 0);
  }
}

// develop's per-pixel stages for these settings: exposure, Presence, stage 5, Color Mixer, clip.
void test_develop_matches_photocraft() {
  for (const auto& c : kCurves) {
    const cr::Curves curves = cr::resolveCurves(settings_of(c));
    const cr::Rgb gains = cr::balanceGains(0.0F, 0.0F, c.exposure);
    std::array<float, 8> hue{}, none{};
    hue[0] = c.hue_red;
    hue[4] = c.hue_aqua;
    float worst = 0.0F;
    for (std::size_t i = 0; i + 2 < c.in.size(); i += 3) {
      cr::Rgb p{c.in[i], c.in[i + 1], c.in[i + 2]};
      if (c.exposure != 0.0F) p = cr::applyBalance(p, gains);
      if (c.vibrance != 0.0F) p = cr::applyPresence(p, c.vibrance, 0.0F);
      p = cr::applyCurves(p, curves);
      if (!cr::colorMixerNeutral(hue, none, none)) p = cr::applyColorMixer(p, hue, none, none);
      for (int k = 0; k < 3; ++k) worst = std::max(worst, std::abs(std::clamp(p[k], 0.0F, 1.0F) - c.out[i + k]));
    }
    std::printf("curve develop %-28s worst %g\n", c.name, worst);
    CHECK(worst == 0.0F);
  }
}

void test_neutral_and_linear() {
  cr::CurveSettings s;
  CHECK(cr::curvesNeutral(s));
  s.points[0] = {{0.0F, 0.0F}, {128.0F, 128.0F}, {255.0F, 255.0F}};  // the identity (is_linear)
  CHECK(cr::curvesNeutral(s));
  CHECK(cr::resolveCurves(s).point[0].empty());
  s.points[0] = {{0.0F, 10.0F}};  // one point: linear (curve_lut needs two)
  CHECK(cr::curvesNeutral(s));
  s.lights = 1.0F;
  CHECK(!cr::curvesNeutral(s));
  // lut_eval extends linearly outside 0..1.
  const std::vector<float> lut = cr::curveLut({{0.0F, 20.0F}, {255.0F, 240.0F}}, cr::kCurveLut);
  CHECK(cr::lutEval(lut, -0.5F) == -0.5F + lut.front());
  CHECK(cr::lutEval(lut, 1.25F) == lut.back() + 0.25F);
}

}  // namespace

int main() {
  test_neutral_and_linear();
  test_tables_match_photocraft();
  test_develop_matches_photocraft();
  std::printf("camera raw curves ok\n");
  return 0;
}
