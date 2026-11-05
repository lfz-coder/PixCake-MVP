#pragma once
#include <pixcake/gl/gl.hpp>
#include <pixcake/render/render_description.hpp>

#include <cstdint>
#include <string>
#include <vector>
namespace pixcake::gpu {
struct RgbaImage { std::uint32_t width = 0; std::uint32_t height = 0; std::uint32_t row_stride = 0; std::uint64_t revision = 0; std::vector<std::uint8_t> pixels; };
struct RenderMask { std::string id; std::uint32_t width = 0; std::uint32_t height = 0; std::vector<std::uint8_t> alpha; };
struct RenderLayer { RgbaImage image; std::string mask_id; float opacity = 1.0F; std::string blend_mode = "normal"; bool visible = true; std::uint32_t order = 0; };
struct RenderInputs { std::vector<RenderMask> masks; std::vector<RenderLayer> layers; std::uint32_t preview_max_dimension = 4000; bool preview = true; };
struct ExecutionDiagnostic { std::string capability_id; std::string reason; bool blocking = false; };
struct RenderResult { RgbaImage image; std::vector<ExecutionDiagnostic> diagnostics; bool success = false; };

class RenderExecutor {
 public:
  bool renderReference(const pixcake::render::RenderDescription&, const RgbaImage&,
                       const RenderInputs&, RenderResult&) const;
  bool execute(gl::Context&, const pixcake::render::RenderDescription&, const RgbaImage&,
               const RenderInputs&, RenderResult&, std::string&) const;
};
}  // namespace pixcake::gpu
