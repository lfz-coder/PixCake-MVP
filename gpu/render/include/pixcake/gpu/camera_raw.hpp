#pragma once

// Camera Raw's per-pixel develop stages, ported from PhotoCraft.
// Ported from PhotoCraft crates/algo/src/camera_raw.rs and camera_raw/per_pixel.rs
// @ 885a6ddf27cc58c07faf1722e5c327cd38953657, MIT (elected from MIT OR Apache-2.0).
// Copyright (c) 2026 ArtCraft Team and the PhotoCraft contributors.
// Licence text: third_party/photocraft-LICENSES/LICENSE-MIT.
//
// Values are display-encoded (sRGB) RGB, as PhotoCraft's develop() takes them.  Slider units
// are Camera Raw's, which PixCake's parameter mode shares.  Spatial stages (Dehaze,
// Shadows/Highlights, Texture, Clarity, Detail) and Whites/Blacks are in camera_raw_spatial.hpp;
// vignette is not ported.

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace pixcake::gpu::camera_raw {

using Rgb = std::array<float, 3>;

struct Wheel {
  float hue = 0.0F;  // degrees 0..360
  float sat = 0.0F;  // 0..100
  float lum = 0.0F;  // -100..100
};

struct Settings {
  float temperature = 0.0F, tint = 0.0F, exposure = 0.0F, contrast = 0.0F;
  float vibrance = 0.0F, saturation = 0.0F;
  // Bands: red, orange, yellow, green, aqua, blue, purple, magenta.
  std::array<float, 8> hsl_hue{}, hsl_sat{}, hsl_lum{};
  // Wheels: shadows, midtones, highlights, global.
  std::array<Wheel, 4> grade{};
  float grade_blending = 50.0F, grade_balance = 0.0F;
};

// True when every control is at rest: developPixel returns its input unchanged.
bool isIdentity(const Settings& s);

float srgbToLinear(float v);
float linearToSrgb(float v);
float luma(const Rgb& c);
// Channel gains for Temperature / Tint (-100..100), before normalising to unit luminance.
Rgb whiteBalanceGains(float temperature, float tint);
// Weights of the eight Color Mixer bands at hue (degrees): hat functions between centres.
std::array<float, 8> bandWeights(float hue);
Rgb hsvToRgb(float h, float s, float v);

// The stages one at a time, without the final clip.  developPixel is these in order.
// Stage 1: per-channel gains (white balance normalised to unit luminance, times 2^exposure);
// applyBalance encodes(max(decode(v) * gain, 0)).  Skip it when all three sliders are 0.
Rgb balanceGains(float temperature, float tint, float exposure);
Rgb applyBalance(const Rgb& encoded, const Rgb& gains);
// Stage 1b, Calibration primaries: the red, green and blue primaries' Hue (+-100 turns +-30
// degrees towards the next primary) and Saturation (-100 greys), in linear light, luminance
// kept.  Order: red, green, blue.  Skip when calibrationNeutral.
std::array<float, 3> calibrationWeights(float hue);
bool calibrationNeutral(const std::array<float, 3>& hue, const std::array<float, 3>& sat);
Rgb applyCalibration(const Rgb& encoded, const std::array<float, 3>& hue, const std::array<float, 3>& sat);
// Stage 3 with Contrast as its only control (slider -100..100; skip at 0).
Rgb applyContrast(const Rgb& c, float contrast);
// Stage 4, Presence (sliders -100..100; skip when both are 0).
Rgb applyPresence(const Rgb& c, float vibrance, float saturation);
// Stage 5, the tone curves: the parametric curve (Highlights, Lights, Darks, Shadows, -100..100,
// regions split at three points 5..95), then the master, red, green and blue point curves
// ([input, output] in 0..255; PhotoCraft's monotone cubic, Fritsch-Carlson).  Each is a table of
// kCurveLut entries over 0..1, read by lutEval (linear inside, extended linearly outside).
constexpr std::size_t kCurveLut = 1024;
using CurvePoints = std::vector<std::array<float, 2>>;
struct CurveSettings {
  float highlights = 0.0F, lights = 0.0F, darks = 0.0F, shadows = 0.0F;
  std::array<float, 3> splits{25.0F, 50.0F, 75.0F};
  std::array<CurvePoints, 4> points{};  // master, red, green, blue; empty = linear
};
// PhotoCraft is_linear: fewer than two points, or every point on the diagonal (within 1e-3).
bool isLinearCurve(const CurvePoints& points);
bool curvesNeutral(const CurveSettings& s);
std::vector<float> curveLut(const CurvePoints& points, std::size_t n);
std::vector<float> parametricLut(float highlights, float lights, float darks, float shadows,
                                 const std::array<float, 3>& splits, std::size_t n);
float lutEval(const std::vector<float>& lut, float v);
// The tables develop builds; an empty table is a curve that is off.
struct Curves {
  std::vector<float> parametric;
  std::array<std::vector<float>, 4> point{};  // master, red, green, blue
};
Curves resolveCurves(const CurveSettings& s);
Rgb applyCurves(const Rgb& c, const Curves& curves);
// Stage 6, Color Mixer (sliders per band); greys come back unchanged.
bool colorMixerNeutral(const std::array<float, 8>& hue, const std::array<float, 8>& sat,
                       const std::array<float, 8>& lum);
Rgb applyColorMixer(const Rgb& c, const std::array<float, 8>& hue, const std::array<float, 8>& sat,
                    const std::array<float, 8>& lum);
// Stage 7, Color Grading, its wheels resolved once.
struct Grade {
  std::array<Rgb, 4> tints{};
  std::array<float, 4> lums{};
  float blend = 0.0F, balance = 0.0F;
};
bool gradeNeutral(const std::array<Wheel, 4>& wheels);
Grade resolveGrade(const std::array<Wheel, 4>& wheels, float blending, float balance);
Rgb applyColorGrading(const Rgb& c, const Grade& g);

// Stage 9, grain: value noise keyed on full-resolution position (photo_util.rs hash01,
// camera_raw.rs value_noise, per_pixel.rs Grain::new).
float hash01(std::int64_t x, std::int64_t y, std::uint64_t seed);
float valueNoise(float x, float y, std::uint64_t seed);
struct Grain {
  float cell = 1.0F;       // noise cell, full-resolution pixels
  float roughness = 0.0F;  // weight of the finer octave
  float amount = 0.0F;
  std::uint64_t seed = 0;
  float scale = 1.0F;      // full-resolution pixels per frame pixel (1 / pixel_scale)
};
// Camera Raw sliders: amount, size, roughness 0..100; pixel_scale = frame / full resolution.
Grain grainOf(float amount, float size, float roughness, std::uint64_t seed, float pixel_scale);
// The two-octave noise n (0..1) at frame pixel (x, y).
float grainNoise(const Grain& g, float x, float y);
// The tone weight 0.4 + 2.4 l (1 - l) at the pixel's clipped luminance.
float grainWeight(const Rgb& c);
// c + (n - 0.5) * 2 * amount * weight on every channel.
Rgb applyGrain(const Rgb& c, const Grain& g, float x, float y);

// One display-encoded pixel through stages 1, 3, 4, 6, 7, then clipped to 0..1 unless
// the settings are an identity (PhotoCraft's develop with float = false).
Rgb developPixel(const Rgb& encoded, const Settings& s);

}  // namespace pixcake::gpu::camera_raw
