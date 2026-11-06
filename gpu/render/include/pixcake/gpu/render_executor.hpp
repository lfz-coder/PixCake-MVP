#pragma once
#include <pixcake/render/render_description.hpp>
#include <pixcake/gl/gl.hpp>
#include <functional>
#include <string>
#include <vector>
namespace pixcake::gpu {
// The kernel RenderDescription is supplied by the separately developed
// contract. This executor deliberately accepts an adapter callback so it does
// not define a second business description.
class RenderExecutor {
 public:
  using Pass = std::function<bool(gl::Context&, std::string&)>;
  // Validate the backend-independent description before touching a GPU
  // context.  This is shared by preview and export processes.
  static ResultCode validate(const render::RenderDescription&, std::string&);
  bool execute(gl::Context&, const render::RenderDescription&, const std::vector<Pass>&,
               std::string&) const;
  bool execute(gl::Context&, const std::vector<Pass>&, std::string&) const;
};
}  // namespace pixcake::gpu
