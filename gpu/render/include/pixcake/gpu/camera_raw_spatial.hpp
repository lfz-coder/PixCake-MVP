#pragma once

// Camera Raw's whole-frame stages, ported from PhotoCraft crates/algo/src/camera_raw.rs
// (dehaze, stage 3 Contrast / Texture / Clarity with box_mean, gauss, guided_fast, guided,
// luminance_points), camera_raw/local_tone.rs (Highlights / Shadows) and
// camera_raw/whites_blacks.rs, and crates/algo/src/fxutil.rs gauss_box_radii,
// @ 885a6ddf27cc58c07faf1722e5c327cd38953657, MIT (elected from MIT OR Apache-2.0).
// Copyright (c) 2026 ArtCraft Team and the PhotoCraft contributors.
// Licence text: third_party/photocraft-LICENSES/LICENSE-MIT.
//
// A Frame holds sRGB-encoded samples (Camera Raw's encoding_gamma 0), unclipped between stages.

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace pixcake::gpu::camera_raw {

struct Frame {
  std::uint32_t width = 0, height = 0;
  std::vector<std::array<float, 4>> px;  // row-major RGBA
};

// Stage 2: dehaze; amount is the slider / 100.
void dehaze(Frame& frame, float amount);
// Stage 3: Contrast with Texture and Clarity (sliders -100..100); pixel_scale as in grainOf.
void toneDetail(Frame& frame, float contrast, float texture, float clarity, float pixel_scale);

// Building blocks, exposed for tests.
std::array<std::size_t, 3> gaussBoxRadii(float sigma);
std::vector<float> boxMean(const std::vector<float>& v, std::size_t w, std::size_t h, std::size_t r);
std::vector<float> gauss(const std::vector<float>& v, std::size_t w, std::size_t h, float sigma);
std::vector<float> guided(const std::vector<float>& i, const std::vector<float>& p, std::size_t w, std::size_t h,
                          std::size_t r, float eps);
std::vector<float> guidedFast(const std::vector<float>& i, const std::vector<float>& p, std::size_t w, std::size_t h,
                              std::size_t r, float eps);
float bilinear(const std::vector<float>& buf, std::size_t w, std::size_t h, float x, float y);

}  // namespace pixcake::gpu::camera_raw
