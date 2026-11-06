// Contract coverage for the native Core OpenGL backend (checklist C57-C60):
// facade failure paths without a context, then a runtime smoke through the
// same RenderExecutor used by renderd/exportd.
#include <pixcake/gl/native_backend.hpp>
#include <pixcake/gpu/render_executor.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const char* expression, int line) {
  if (!condition) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, expression);
    ++failures;
  }
}

#define EXPECT(condition) expect(static_cast<bool>(condition), #condition, __LINE__)

}  // namespace

int main() {
  namespace gl = pixcake::gl;
  std::string error;

  // Native resources created without a current context must fail with a
  // diagnostic instead of touching GL.
  auto shader = gl::native::create_shader();
  EXPECT(shader.valid());
  EXPECT(!shader.compile(gl::ShaderStage::Vertex, "void main(){}", error));
  EXPECT(!error.empty());
  error.clear();
  auto program = gl::native::create_program();
  EXPECT(!program.use(error));
  EXPECT(!error.empty());
  error.clear();
  auto buffer = gl::native::create_buffer();
  EXPECT(!buffer.write(0, 1, nullptr, error));
  auto vertex_array = gl::native::create_vertex_array();
  EXPECT(!vertex_array.bind(error));
  auto texture = gl::native::create_texture2d();
  EXPECT(!texture.allocate(1, 1, gl::PixelFormat::Rgba8, error));
  EXPECT(!texture.bind(0, error));
  auto framebuffer = gl::native::create_framebuffer();
  EXPECT(!framebuffer.attachColor(texture, error));
  EXPECT(!framebuffer.complete(error));
  auto fence = gl::native::create_fence();
  EXPECT(!fence.wait(0, error));
  EXPECT(!error.empty());

  // A Qt resource must never be accepted by a native one (and vice versa);
  // the facade null objects stand in for "another backend" here.
  error.clear();
  EXPECT(!program.link(gl::Shader(), gl::Shader(), error));

  auto context = gl::native::create_context();
  EXPECT(context.valid());
  if (!gl::native::available() || !context.makeCurrent()) {
    // CI may not expose a hardware or software GL provider. The API/error
    // paths above still validate the contract in that environment.
    std::fprintf(stderr, "native GPU context unavailable; runtime smoke skipped: %s\n",
                 context.lastError().c_str());
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
  }

  {
    const std::string vertex_source =
        "#version 330 core\nlayout(location=0) in vec2 position;\n"
        "void main(){gl_Position=vec4(position,0,1);}";
    const std::string fragment_source =
        "#version 330 core\nout vec4 color;\nuniform float gain;\n"
        "void main(){color=vec4(gain,0,0,1);}";
    auto vertex = context.createShader();
    auto fragment = context.createShader();
    auto linked = context.createProgram();
    EXPECT(vertex.compile(gl::ShaderStage::Vertex, vertex_source, error));
    EXPECT(fragment.compile(gl::ShaderStage::Fragment, fragment_source, error));
    EXPECT(linked.link(vertex, fragment, error));
    EXPECT(linked.use(error));
    EXPECT(linked.setFloat("gain", 1.0F, error));
    EXPECT(!linked.setFloat("missing_uniform", 1.0F, error));

    std::array<std::uint8_t, 16> pixels = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    auto target = context.createTexture2D();
    const gl::ImageView view{2, 2, 8, gl::PixelFormat::Rgba8,
                             reinterpret_cast<const std::byte*>(pixels.data()), pixels.size()};
    EXPECT(target.upload(view, error));
    EXPECT(target.bind(0, error));
    auto attached = context.createFramebuffer();
    EXPECT(attached.attachColor(target, error));
    EXPECT(attached.complete(error));
    EXPECT(attached.bind(error));

    auto vertices = gl::native::create_buffer();
    auto layout = gl::native::create_vertex_array();
    EXPECT(layout.bind(error));
    EXPECT(vertices.allocate(16, pixels.data(), error));
    EXPECT(vertices.write(0, 16, pixels.data(), error));
    EXPECT(!vertices.write(8, 16, pixels.data(), error));
    EXPECT(layout.enableAttribute(0, 2, 8, 0, error));

    auto done = gl::native::create_fence();
    EXPECT(done.insert(error));
    EXPECT(done.wait(1000000000U, error));
  }
  context.doneCurrent();

  // The shared executor must produce the same pixels on the native backend as
  // the deterministic reference path.
  pixcake::gpu::RgbaImage input;
  input.width = 1;
  input.height = 1;
  input.row_stride = 4;
  input.pixels = {96, 128, 160, 255};
  pixcake::render::RenderDescription description;
  description.document_revision = 19;
  pixcake::render::RenderOperation operation;
  operation.capability_id = "color.global";
  operation.stage = "color";
  operation.program = "pixcake.color.global";
  operation.values = {{"temperature", "temperature", "40", 40.0},
                      {"tint", "tint", "-20", -20.0},
                      {"exposure", "exposure", "0.5", 0.5},
                      {"contrast", "contrast", "20", 20.0}};
  description.operations.push_back(operation);

  pixcake::gpu::RenderExecutor executor;
  pixcake::gpu::RenderInputs inputs;
  pixcake::gpu::RenderResult gpu_result;
  pixcake::gpu::RenderResult reference;
  EXPECT(executor.execute(context, description, input, inputs, gpu_result, error));
  EXPECT(!gpu_result.reference_fallback);
  EXPECT(executor.renderReference(description, input, inputs, reference));
  if (gpu_result.success && reference.success) {
    for (std::size_t i = 0; i < 3; ++i) {
      EXPECT(std::abs(static_cast<int>(gpu_result.image.pixels[i]) - static_cast<int>(reference.image.pixels[i])) <= 3);
    }
  }
  std::printf("native GPU backend active\n");
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
