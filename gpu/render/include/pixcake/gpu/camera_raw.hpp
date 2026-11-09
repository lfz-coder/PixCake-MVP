#pragma once

// Camera Raw's per-pixel develop stages, ported from PhotoCraft.
// Ported from PhotoCraft crates/algo/src/camera_raw.rs and camera_raw/per_pixel.rs
// @ 885a6ddf27cc58c07faf1722e5c327cd38953657, MIT (elected from MIT OR Apache-2.0).
// Copyright (c) 2026 ArtCraft Team and the PhotoCraft contributors.
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

// One display-encoded pixel through stages 1, 3, 4, 6, 7, then clipped to 0..1 unless
// the settings are an identity (PhotoCraft's develop with float = false).
Rgb developPixel(const Rgb& encoded, const Settings& s);

}  // namespace pixcake::gpu::camera_raw
