#pragma once

// Camera Raw's whole-frame stages on OpenGL 3.3 (fragment passes over float textures), with the
// C++ reference in camera_raw_spatial.hpp as the oracle: PhotoCraft has these stages on the CPU
// only.  Port of PhotoCraft crates/algo/src/camera_raw.rs, camera_raw/local_tone.rs and
// camera_raw/whites_blacks.rs @ 885a6ddf27cc58c07faf1722e5c327cd38953657, MIT (elected from
// MIT OR Apache-2.0).  Copyright (c) 2026 ArtCraft Team and the PhotoCraft contributors.
// Licence text: third_party/photocraft-LICENSES/LICENSE-MIT.
//
// Every function needs the Gl's context current, returns false on any GL error (Gl::error()
// says which) and leaves its outputs unspecified then.

#include <pixcake/gl/gl.hpp>
#include <pixcake/gpu/camera_raw_spatial.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pixcake::gpu::camera_raw_gl {

class Gl;
struct Slot;

// One float texture (R32F, or Rgba32F for a frame) with its framebuffer, from the Gl's pool;
// it goes back to the pool when the last copy is dropped.  Planes must not outlive their Gl.
struct Plane {
  std::shared_ptr<Slot> slot;
  std::uint32_t w = 0, h = 0;
  bool valid() const { return slot != nullptr; }
};

class Gl {
 public:
  explicit Gl(const gl::Context& context);
  ~Gl();
  Gl(const Gl&) = delete;
  Gl& operator=(const Gl&) = delete;
  bool ok() const;
  const std::string& error() const;
  // Bytes of the free planes kept for reuse.
  std::size_t pooledBytes() const;
  // Frees the free planes (planes still held stay valid and are freed when dropped).
  void trim();

  struct Impl;
  Impl& impl() { return *impl_; }

 private:
  std::unique_ptr<Impl> impl_;
};

// A plane from the pool, and its texture and framebuffer (for the executor's own passes).
bool allocate(Gl&, std::uint32_t w, std::uint32_t h, gl::PixelFormat format, Plane& out);
gl::Framebuffer& framebufferOf(const Plane&);
gl::Texture2D& textureOf(const Plane&);

bool upload(Gl&, const std::vector<float>& v, std::uint32_t w, std::uint32_t h, Plane& out);
bool download(Gl&, const Plane& p, std::vector<float>& out);

// The filters of camera_raw_spatial.hpp, same arguments.
bool boxMean(Gl&, const Plane& in, std::size_t r, Plane& out);
bool gauss(Gl&, const Plane& in, float sigma, Plane& out);
bool guided(Gl&, const Plane& i, const Plane& p, std::size_t r, float eps, Plane& out);
bool guidedFast(Gl&, const Plane& i, const Plane& p, std::size_t r, float eps, Plane& out);
bool rankFilter(Gl&, const Plane& in, std::size_t r, bool take_max, Plane& out);

// A frame as one Rgba32F plane (unclipped).
bool uploadFrame(Gl&, const camera_raw::Frame& frame, Plane& rgba);
bool downloadFrame(Gl&, const Plane& rgba, camera_raw::Frame& out);

// The stages of camera_raw_spatial.hpp on an Rgba32F frame, in place.  Image statistics
// (dehaze's atmospheric light and means) are read back and computed on the CPU.
bool dehaze(Gl&, Plane& rgba, float amount);
bool toneDetail(Gl&, Plane& rgba, float contrast, float texture, float clarity, float pixel_scale);
// Highlights and Shadows: the pyramids on the GPU; ranges, quantiles and shifts on the CPU from
// read-back work planes (at most localToneConstants().work_side across).
bool localTone(Gl&, Plane& rgba, float shadows, float highlights);
// Whites and Blacks: the black and white points from a read-back <= 256 px copy, the curves as
// one interpolated row each (whitesRow / blacksRow).
bool whitesBlacks(Gl&, Plane& rgba, float whites, float blacks);

}  // namespace pixcake::gpu::camera_raw_gl
