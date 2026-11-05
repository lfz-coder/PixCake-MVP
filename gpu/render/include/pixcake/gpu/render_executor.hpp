#pragma once
#include <pixcake/gl/gl.hpp>
#include <pixcake/render/render_description.hpp>

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
namespace pixcake::gpu {
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
class ResourceProvider {
 public:
  virtual ~ResourceProvider() = default;
  // The provider owns parsing/validation. The renderer only receives an
  // already-resolved handle and never interprets CUBE/XMP bytes.
  virtual bool resolve(std::string_view program, ResolvedResource& resource,
                       std::string& error) const = 0;
};
struct RenderInputs { std::vector<RenderMask> masks; std::vector<RenderLayer> layers; std::uint32_t preview_max_dimension = 4000; bool preview = true; const ResourceProvider* resources = nullptr; };
struct ExecutionDiagnostic { std::string capability_id; std::string reason; bool blocking = false; };
enum class PassStatus { Implemented, ReferenceOnly, MissingResource, Unregistered, Disabled, InvalidInput };
struct PassStatusRecord { std::string capability_id; std::string program; PassStatus status = PassStatus::Unregistered; std::string reason; };
struct RenderResult { RgbaImage image; std::vector<ExecutionDiagnostic> diagnostics; std::vector<PassStatusRecord> statuses; bool success = false; };

class PassRegistry {
 public:
  static PassStatus classify(const pixcake::render::RenderOperation& operation);
  static bool gpuSupported(const pixcake::render::RenderOperation& operation);
};

class RenderExecutor {
 public:
  bool renderReference(const pixcake::render::RenderDescription&, const RgbaImage&,
                       const RenderInputs&, RenderResult&) const;
  bool execute(gl::Context&, const pixcake::render::RenderDescription&, const RgbaImage&,
               const RenderInputs&, RenderResult&, std::string&) const;
};
}  // namespace pixcake::gpu
