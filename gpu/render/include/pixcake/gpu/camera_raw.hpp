#pragma once

// Camera Raw's per-pixel develop stages, ported from PhotoCraft.
// Ported from PhotoCraft crates/algo/src/camera_raw.rs and camera_raw/per_pixel.rs
// @ 885a6ddf27cc58c07faf1722e5c327cd38953657, MIT (elected from MIT OR Apache-2.0).
// Copyright (c) 2026 ArtCraft Team and the PhotoCraft contributors.
// Licence text: third_party/photocraft-LICENSES/LICENSE-MIT.
//
// Values are display-encoded (sRGB) RGB, as PhotoCraft's develop() takes them.  Slider units
// are Camera Raw's, which PixCake's parameter mode shares.  Spatial stages (Dehaze,
// Shadows/Highlights, Texture, Clarity, Detail), Whites/Blacks, curves, vignette and grain
// are not here yet; see docs/superpowers/plans/2026-11-09-camera-raw-per-pixel-port.md.

#include <array>

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
// Stage 3 with Contrast as its only control (slider -100..100; skip at 0).
Rgb applyContrast(const Rgb& c, float contrast);
// Stage 4, Presence (sliders -100..100; skip when both are 0).
Rgb applyPresence(const Rgb& c, float vibrance, float saturation);
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

// One display-encoded pixel through stages 1, 3, 4, 6, 7, then clipped to 0..1 unless
// the settings are an identity (PhotoCraft's develop with float = false).
Rgb developPixel(const Rgb& encoded, const Settings& s);

}  // namespace pixcake::gpu::camera_raw
