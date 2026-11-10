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
// Stage 3: Highlights and Shadows (-100..100), a fast local Laplacian filter on log2 luminance,
// at most 1600 px across with a guided upsampling of the change (local_tone.rs apply).
void localTone(Frame& frame, float shadows, float highlights);
// The image's black and white points as sRGB-encoded grey levels (3rd percentile of the
// luminance, 99.5th of the brightest channel, on a copy at most 256 px across).
std::array<float, 2> luminancePoints(const Frame& frame);
// Stage 3b: Whites and Blacks (-100..100), adapted to the frame's black and white points.
void whitesBlacks(Frame& frame, float whites, float blacks);

// Planes of at least this many samples run their rows (or columns) on a pool of worker threads,
// as PhotoCraft's par_rows does (PAR_MIN 1 << 15); each output sample is computed exactly as on
// one thread, so the result is the same bits.  Returns the previous value; for tests.
std::size_t setParallelMinimum(std::size_t samples);

// Highlights/Shadows building blocks, shared with the GPU port (camera_raw_gl.cpp).
struct LocalToneConstants {
  float ref_step = 0, floor = 0;  // reference spacing (stops); log2 floor added to the luminance
  std::size_t work_side = 0;      // planes wider or taller than this are shrunk first
  std::array<float, 5> shadows_own{}, shadows_around{};
  float shadows_sigma = 0, shadows_down = 0;
  std::array<float, 5> highlights_pull{}, highlights_push{};
  float highlights_sigma = 0;
};
const LocalToneConstants& localToneConstants();
// The q quantile (0..1) of v in f32::total_cmp order.
float quantileOf(const std::vector<float>& v, float q);
// The 1st and 99th percentiles of a log-luminance plane, on a copy at most 256 px across.
std::array<float, 2> localToneRange(const std::vector<float>& u, std::size_t w, std::size_t h);

// Whites/Blacks building blocks, shared with the GPU port (camera_raw_gl.cpp).
// whites_row / blacks_row (whites_blacks.rs row_at): the curve at internal amount x as one row
// over grey levels 0, 1/32, ..., 1.
std::array<float, 33> whitesRow(float x);
std::array<float, 33> blacksRow(float x);
// whites / blacks (whites_blacks.rs): the curve at internal amount x of grey level v.
float whitesCurve(float v, float x);
float blacksCurve(float v, float x);
// amounts: the internal amounts for sliders whites/blacks (-100..100) and points bp, wp.
std::array<float, 2> whitesBlacksAmounts(float whites, float blacks, float bp, float wp);
// luminancePoints from the copy's cells: each cell's mean linear RGB (clamped to 0..1).
std::array<float, 2> whiteBlackPointsOfCells(const std::vector<std::array<float, 3>>& cells);

// Building blocks, exposed for tests.
std::array<std::size_t, 3> gaussBoxRadii(float sigma);
std::vector<float> boxMean(const std::vector<float>& v, std::size_t w, std::size_t h, std::size_t r);
std::vector<float> gauss(const std::vector<float>& v, std::size_t w, std::size_t h, float sigma);
std::vector<float> guided(const std::vector<float>& i, const std::vector<float>& p, std::size_t w, std::size_t h,
                          std::size_t r, float eps);
std::vector<float> guidedFast(const std::vector<float>& i, const std::vector<float>& p, std::size_t w, std::size_t h,
                              std::size_t r, float eps);
float bilinear(const std::vector<float>& buf, std::size_t w, std::size_t h, float x, float y);
// The minimum (or maximum) over each (2r + 1)^2 window cut to the frame (dehaze's dark channel
// and opening).
std::vector<float> rankFilter(const std::vector<float>& v, std::size_t w, std::size_t h, std::size_t r, bool take_max);

}  // namespace pixcake::gpu::camera_raw
