// Ported from PhotoCraft crates/algo/src/lens.rs (LensMap::new, LensMap::sample, remap_impl,
// sample_channels_impl, correct) and crates/algo/src/photo_util.rs catmull_rom
// @ 885a6ddf27cc58c07faf1722e5c327cd38953657, MIT (elected from MIT OR Apache-2.0).
// Copyright (c) 2026 ArtCraft Team and the PhotoCraft contributors.
// Licence text: third_party/photocraft-LICENSES/LICENSE-MIT.
#include <pixcake/gpu/lens.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace pixcake::gpu::lens {

bool isIdentity(const Correction& c) { return c.distortion == 0.0 && c.vignette_amount == 0.0; }

std::array<float, 4> catmullRom(float t) {
  const float t2 = t * t, t3 = t2 * t;
  return {-0.5F * t3 + t2 - 0.5F * t, 1.5F * t3 - 2.5F * t2 + 1.0F, -1.5F * t3 + 2.0F * t2 + 0.5F * t, 0.5F * t3 - 0.5F * t2};
}

// LensMap::new with no profile, no chromatic aberration, no keystone, angle 0, scale 100 %.
Map::Map(const Correction& c, std::uint32_t width, std::uint32_t height) {
  cx = static_cast<double>(0 + static_cast<std::int64_t>(width)) / 2.0;
  cy = static_cast<double>(0 + static_cast<std::int64_t>(height)) / 2.0;
  r = std::fmax(std::hypot(static_cast<double>(width), static_cast<double>(height)), 2.0) / 2.0;
  k1 = 0.0 - c.distortion / 100.0 * 0.25;
  vig_amount = c.vignette_amount / 100.0;
  vig_mid = std::clamp(c.vignette_midpoint, 0.0, 100.0) / 100.0;
}

bool Map::sample(double x, double y, Sample& out) const {
  // The terms at rest, kept so each operation rounds as PhotoCraft's does.
  const double scale = 1.0, s = 0.0, co = 1.0;  // scale, sin and cos of angle 0
  const double m6 = 0.0, m7 = 0.0, m8 = 1.0;     // inverse keystone's last row (-h, -g, 1)
  const double k2 = 0.0, p1 = 0.0, p2 = 0.0, v1 = 0.0, v2 = 0.0, v3 = 0.0;
  const double dx = (x - cx) / scale, dy = (y - cy) / scale;
  const double rx = co * dx + s * dy, ry = -s * dx + co * dy;
  const double nx = rx / r, ny = ry / r;
  const double w = m6 * nx + m7 * ny + m8;
  if (w <= 0.05) return false;
  const double ux = nx / w, uy = ny / w;
  const double r2 = ux * ux + uy * uy;
  const double radial = 1.0 + k1 * r2 + k2 * r2 * r2;
  const double sx = ux * radial + 2.0 * p1 * ux * uy + p2 * (r2 + 2.0 * ux * ux);
  const double sy = uy * radial + p1 * (r2 + 2.0 * uy * uy) + 2.0 * p2 * ux * uy;
  out.p = {cx + sx * 1.0 * r, cy + sy * 1.0 * r};
  const double rs2 = sx * sx + sy * sy;
  const double falloff = std::fmax(1.0 + v1 * rs2 + v2 * rs2 * rs2 + v3 * rs2 * rs2 * rs2, 0.05);
  const double start = (1.0 - vig_mid) * 0.9;
  const double t = std::clamp((std::sqrt(rs2) - start) / (1.0 - start + 1e-9), 0.0, 1.0);
  const double ramp = t * t * (3.0 - 2.0 * t);
  out.gain = static_cast<float>(std::pow(2.0, vig_amount * 1.5 * ramp) / falloff);
  return true;
}

// remap over the frame (out_area = frame): every tap of a sample lies inside the tile's source
// window, so the window's clamping is the frame's.
void correct(camera_raw::Frame& frame, const Correction& c) {
  if (frame.width == 0 || frame.height == 0) return;
  const Map map(c, frame.width, frame.height);
  const std::size_t w = frame.width, h = frame.height;
  // Premultiply colour by alpha so transparent pixels don't bleed.
  std::vector<std::array<float, 4>> buf = frame.px;
  for (auto& q : buf) {
    for (int k = 0; k < 3; ++k) q[k] *= q[3];
  }
  const double fx0 = 0.0, fy0 = 0.0, fx1 = static_cast<double>(w), fy1 = static_cast<double>(h);
  camera_raw::parallelRows(h, w * h, [&](std::size_t first, std::size_t end) {
    for (std::size_t yy = first; yy < end; ++yy) {
      for (std::size_t xx = 0; xx < w; ++xx) {
        auto& o = frame.px[yy * w + xx];
        Sample s;
        if (!map.sample(static_cast<double>(xx) + 0.5, static_cast<double>(yy) + 0.5, s)) {
          o = {0.0F, 0.0F, 0.0F, 0.0F};
          continue;
        }
        float inside = 1.0F;
        if (!c.extension) {
          const double fx = std::clamp(std::fmin(s.p[0] - fx0, fx1 - s.p[0]) + 0.5, 0.0, 1.0);
          const double fy = std::clamp(std::fmin(s.p[1] - fy0, fy1 - s.p[1]) + 0.5, 0.0, 1.0);
          inside = static_cast<float>(fx * fy);
        }
        // sample_channels: 16 taps, pixel centres at +0.5, clamped to the frame.
        const double x = std::clamp(s.p[0] - 0.5, fx0, fx1 - 1.0), y = std::clamp(s.p[1] - 0.5, fy0, fy1 - 1.0);
        const double xi = std::floor(x), yi = std::floor(y);
        const auto wx = catmullRom(static_cast<float>(x - xi)), wy = catmullRom(static_cast<float>(y - yi));
        std::array<float, 4> acc{};
        for (int j = 0; j < 4; ++j) {
          const auto ty = static_cast<std::size_t>(std::clamp<std::int64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(yi)) + j - 1, 0,
                                                                             static_cast<std::int64_t>(h) - 1));
          for (int i = 0; i < 4; ++i) {
            const auto tx = static_cast<std::size_t>(std::clamp<std::int64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(xi)) + i - 1, 0,
                                                                               static_cast<std::int64_t>(w) - 1));
            const float weight = wx[i] * wy[j];
            const auto& v = buf[ty * w + tx];
            for (int k = 0; k < 4; ++k) acc[k] += weight * v[k];
          }
        }
        const float alpha = std::clamp(acc[3], 0.0F, 1.0F);
        for (int k = 0; k < 3; ++k) {
          const float straight = alpha > 1e-6F ? acc[k] / alpha : 0.0F;
          o[k] = straight * s.gain;
        }
        o[3] = alpha * inside;
      }
    }
  });
}

}  // namespace pixcake::gpu::lens
