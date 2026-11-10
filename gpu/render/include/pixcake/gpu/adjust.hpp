#pragma once

// Photoshop adjustment-layer functions, ported from PhotoCraft crates/compose:
// Levels (adjust/tone.rs levels_q, levels_gamma, tone_luts_q, lut), Selective Color
// (adjust/color.rs selective_color_weights, selective_color), 3D LUT sampling
// (adjust/color.rs lut3d_sample) and the normal / multiply / screen composite (psblend.rs
// composite, normal_rgb; photocraft_color::blend channel)
// @ 885a6ddf27cc58c07faf1722e5c327cd38953657, MIT (elected from MIT OR Apache-2.0).
// Copyright (c) 2026 ArtCraft Team and the PhotoCraft contributors.
// Licence text: third_party/photocraft-LICENSES/LICENSE-MIT.

#include <array>
#include <cstddef>
#include <vector>

namespace pixcake::gpu::adjust {

using Rgb = std::array<float, 3>;
using Rgba = std::array<float, 4>;

// One Levels channel in 0..1 units (Photoshop's 0..255 / 255).
struct LevelsChannel {
  float in_black = 0.0F, in_white = 1.0F, gamma = 1.0F, out_black = 0.0F, out_white = 1.0F;
};

// Entries of the Levels / Curves tables (tone.rs LUT_SIZE).
inline constexpr std::size_t kLutSize = 4096;

// The midtone gamma on u in 0..1, with Photoshop's shadow toe above gamma 1.
float levelsGamma(float u, float g);
// Levels on samples with `quantum` steps per unit (8-bit: 255).
float levelsQ(const LevelsChannel& channel, float v, float quantum);
// The RGB table of a Levels whose master is `master` and whose channels are identity
// (PixCake's composite RGB levels), kLutSize entries.
std::vector<float> levelsLut(const LevelsChannel& master, float quantum);
// PhotoCraft's lut(): v clamped to 0..1, linear between entries.
float sampleLut(const std::vector<float>& table, float v);

// Membership of reds, yellows, greens, cyans, blues, magentas, whites, neutrals, blacks.
std::array<float, 9> selectiveWeights(const Rgb& c);
// adjustments[range] = (cyan, magenta, yellow, black) in percent, ranges as above.
Rgb selectiveColor(const Rgb& c, bool relative, const std::array<std::array<float, 4>, 9>& adjustments);

// n^3 RGB triplets, red fastest; a short or degenerate table leaves c alone.
Rgb lut3dSample(const std::vector<float>& table, std::size_t n, const Rgb& c, bool tetrahedral);

enum class BlendMode { Normal, Multiply, Screen };
// Straight-alpha source over straight-alpha backdrop (W3C general formula).
Rgba composite(BlendMode mode, const Rgba& backdrop, const Rgba& source, float opacity);

}  // namespace pixcake::gpu::adjust
