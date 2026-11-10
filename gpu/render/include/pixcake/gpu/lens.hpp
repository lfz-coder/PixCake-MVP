#pragma once
// Photoshop's Lens Correction filter (manual Remove Distortion, Vignette Amount / Midpoint),
// ported from PhotoCraft crates/algo/src/lens.rs (LensMap, remap, correct) and
// crates/algo/src/photo_util.rs catmull_rom
// @ 885a6ddf27cc58c07faf1722e5c327cd38953657, MIT (elected from MIT OR Apache-2.0).
// Copyright (c) 2026 ArtCraft Team and the PhotoCraft contributors.
// Licence text: third_party/photocraft-LICENSES/LICENSE-MIT.
//
// No lens profile, chromatic aberration or perspective: those sliders are at rest (PixCake has
// no parameters for them).  The frame is the whole image.
#include <pixcake/gpu/camera_raw_spatial.hpp>

#include <array>

namespace pixcake::gpu::lens {

struct Correction {
  double distortion = 0.0;         // Remove Distortion -100..100 (positive corrects barrel)
  double vignette_amount = 0.0;    // -100..100, positive lightens the corners
  double vignette_midpoint = 50.0;  // 0..100
  bool extension = false;          // edges: repeat the edge pixels; false = transparency
};

bool isIdentity(const Correction& c);

// Where an output pixel centre (x, y) samples the source, and the gain on its colour.
struct Sample {
  std::array<double, 2> p{};
  float gain = 1.0F;
};
class Map {
 public:
  Map(const Correction& c, std::uint32_t width, std::uint32_t height);
  // False where the map has no sample (the keystone's infinity; never without perspective).
  bool sample(double x, double y, Sample& out) const;
  // The map's terms, for the GPU shader: centre, radius, k1, vignette amount and midpoint.
  double cx = 0, cy = 0, r = 0, k1 = 0, vig_amount = 0, vig_mid = 0;
};

std::array<float, 4> catmullRom(float t);

// correct(): the frame (straight RGBA, any range) resampled through the map, colour times the
// gain; alpha times the 1 px soft edge unless edges extend.
void correct(camera_raw::Frame& frame, const Correction& c);

}  // namespace pixcake::gpu::lens
