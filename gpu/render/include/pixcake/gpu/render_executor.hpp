#pragma once
#include <pixcake/render/render_description.hpp>
#include <pixcake/gl/gl.hpp>
#include <pixcake/render/render_description.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
namespace pixcake::gpu {
// Render pixels are normalized linear-light RGBA samples stored in an 8-bit
// transport buffer for the current contract.  File encoders/decoders own any
// sRGB or RAW transfer and must convert before handing a frame to this API.
struct RgbaImage { std::uint32_t width = 0; std::uint32_t height = 0; std::uint32_t row_stride = 0; std::uint64_t revision = 0; std::vector<std::uint8_t> pixels; };
struct RenderMask { std::string id; std::uint32_t width = 0; std::uint32_t height = 0; std::vector<std::uint8_t> alpha; };
struct RenderLayer { RgbaImage image; std::string mask_id; float opacity = 1.0F; std::string blend_mode = "normal"; bool visible = true; std::uint32_t order = 0; };
enum class ResourceKind { Lut, Profile };
struct ResolvedResource {
  std::string id;
  ResourceKind kind = ResourceKind::Lut;
  const std::byte* data = nullptr;
  std::size_t bytes = 0;
};
struct Lut3DView {
  std::string id;
  std::uint32_t size = 0;
  // RGB float triples, R varying fastest, then G, then B. The provider owns
  // the memory for the duration of resolveLut and the render call.
  const float* rgb = nullptr;
  std::size_t sample_count = 0;
  std::uint64_t revision = 0;
};
class ResourceProvider {
 public:
  virtual ~ResourceProvider() = default;
  // The provider owns parsing/validation. The renderer only receives an
  // already-resolved handle and never interprets CUBE/XMP bytes.
  virtual bool resolve(std::string_view program, ResolvedResource& resource,
                       std::string& error) const = 0;
  virtual bool resolveLut(std::string_view program, Lut3DView& lut,
                          std::string& error) const {
    (void)program;
    lut = {};
    error = "structured 3D LUT is unavailable";
    return false;
  }
};
struct RenderInputs {
  std::vector<RenderMask> masks;
  std::vector<RenderLayer> layers;
  std::uint32_t preview_max_dimension = 4000;
  bool preview = true;
  const ResourceProvider* resources = nullptr;
  // Frame pixels per full-resolution output pixel (long edge over long edge, 0.01..1): grain
  // samples its noise in full-resolution coordinates so a preview shows the export's grain.
  float pixel_scale = 1.0F;
};
struct ExecutionDiagnostic { std::string capability_id; std::string reason; bool blocking = false; };
enum class PassStatus { Implemented, ReferenceOnly, MissingResource, Unregistered, Disabled, InvalidInput };
struct PassStatusRecord { std::string capability_id; std::string program; PassStatus status = PassStatus::Unregistered; std::string reason; };
struct RenderResult {
  RgbaImage image;
  std::vector<ExecutionDiagnostic> diagnostics;
  std::vector<PassStatusRecord> statuses;
  // True when execute could not use the GPU path and deliberately ran the
  // deterministic reference compositor instead.
  bool reference_fallback = false;
  bool success = false;
};

class PassRegistry {
 public:
  static PassStatus classify(const pixcake::render::RenderOperation& operation);
  static bool gpuSupported(const pixcake::render::RenderOperation& operation);
};

// Compiled programs and frame textures of the GPU path, kept between frames
// for one context (render_executor.cpp).
struct GpuCache;

class RenderExecutor {
 public:
  using Pass = std::function<bool(gl::Context&, std::string&)>;
  // Validate the backend-independent description before touching a GPU
  // context.  This is shared by preview and export processes.
  static ResultCode validate(const render::RenderDescription&, std::string&);
  bool execute(gl::Context&, const render::RenderDescription&, const std::vector<Pass>&,
               std::string&) const;
  bool execute(gl::Context&, const std::vector<Pass>&, std::string&) const;
  bool renderReference(const pixcake::render::RenderDescription&, const RgbaImage&,
                       const RenderInputs&, RenderResult&) const;
  // Draws on the GPU when every operation that draws has a shader, else falls
  // back to renderReference (result.reference_fallback).  Keep one executor
  // per context to reuse its programs and textures between frames.
  bool execute(gl::Context&, const pixcake::render::RenderDescription&, const RgbaImage&,
               const RenderInputs&, RenderResult&, std::string&) const;
  // Largest tile edge of the GPU path; larger frames draw tile by tile.  The
  // default fits every preview (<= 4000) in one tile and stays well under
  // the texture limit of any Mac GPU.  Tests set a small edge.
  static constexpr std::uint32_t kDefaultGpuTile = 4096;
  void setGpuTileSize(std::uint32_t edge);
  // An operation with Camera Raw's whole-frame controls draws on the GPU only on frames of at
  // least this many pixels (and at most one GPU tile); below it the reference is faster
  // (bench_reference_render, 2026-11-10: 600 x 450 GPU 94 ms vs 89 ms, 800 x 600 125 vs 148).
  static constexpr std::uint64_t kDefaultSpatialGpuMinPixels = 320000;
  void setSpatialGpuMinPixels(std::uint64_t pixels);

 private:
  mutable std::shared_ptr<GpuCache> gpu_cache_;
  std::uint32_t gpu_tile_ = kDefaultGpuTile;
  std::uint64_t spatial_gpu_min_pixels_ = kDefaultSpatialGpuMinPixels;
};
}  // namespace pixcake::gpu
