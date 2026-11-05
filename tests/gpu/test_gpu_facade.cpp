#include <pixcake/gl/gl.hpp>
#include <pixcake/gpu/render_executor.hpp>
#include <cassert>
#include <string>
int main() {
  auto context = pixcake::gl::Context::create();
  assert(context.valid());
  assert(!context.makeCurrent());
  assert(context.lastError() == "no GPU backend is registered");
  pixcake::gpu::RenderExecutor executor;
  std::string error;
  assert(executor.execute(context, {}, error));
  return 0;
}
