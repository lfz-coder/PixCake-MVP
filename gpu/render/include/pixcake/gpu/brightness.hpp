#pragma once

// Photoshop's modern (CS3+) Brightness, ported from PhotoCraft.
// Ported from PhotoCraft crates/compose/src/adjust/tone.rs (modern_brightness, modern_lut, lut)
// @ 885a6ddf27cc58c07faf1722e5c327cd38953657, MIT (elected from MIT OR Apache-2.0).
// Copyright (c) 2026 ArtCraft Team and the PhotoCraft contributors.
// Licence text: third_party/photocraft-LICENSES/LICENSE-MIT.
//
// PixCake's brightness slider (-100..100) takes Photoshop's units directly; contrast stays
// with Camera Raw (camera_raw.hpp), so the table is modern_lut(brightness, 0).

#include <cstddef>
#include <vector>

namespace pixcake::gpu::brightness {

constexpr std::size_t kLutSize = 4096;

// modern_lut(brightness, 0): kLutSize samples of the curve over 0..1.
std::vector<float> modernLut(float brightness);
// tone::lut: v clamped to 0..1, linear between neighbouring samples.
float sampleLut(const std::vector<float>& table, float v);

// The raised curve's constants (Brightness::new of |brightness|), for the GPU, which
// evaluates the curve instead of a table.  raise is brightness > 0; a lowered curve is the
// inverse of the raised one.
struct Curve {
  bool raise = true;
  float s = 1.0F, e = 1.0F, x0 = 1.0F, alpha = 1.0F;
  bool hermite = true;
};
Curve curve(float brightness);
float raised(const Curve& c, float x);

}  // namespace pixcake::gpu::brightness
