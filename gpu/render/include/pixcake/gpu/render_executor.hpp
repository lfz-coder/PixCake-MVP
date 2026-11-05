#pragma once
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
  bool execute(gl::Context&, const std::vector<Pass>&, std::string&) const;
};
}  // namespace pixcake::gpu
