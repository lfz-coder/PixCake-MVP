#include "pixcake/gpu/render_executor.hpp"
namespace pixcake::gpu {
bool RenderExecutor::execute(gl::Context& context, const std::vector<Pass>& passes,
                             std::string& error) const {
  if (!context.valid()) { error = "render context is empty"; return false; }
  for (const auto& pass : passes) {
    if (!pass) { error = "render pass is empty"; return false; }
    if (!pass(context, error)) return false;
  }
  return true;
}
}  // namespace pixcake::gpu
