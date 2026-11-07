// Contract coverage for the native Core OpenGL backend (checklist C57-C60):
// facade failure paths without a context, then a runtime smoke through the
// same RenderExecutor used by renderd/exportd.
#include <pixcake/gl/native_backend.hpp>
#include <pixcake/gpu/render_executor.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

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
  // Every group the shaders draw, on a frame that covers hues, greys, dark and
  // light values and partial alpha: the GPU readback must match the
  // reference within one step (float rounding), and the pass must not fall
  // back.  Dropping any group from the shader makes its case fail.
  pixcake::gpu::RgbaImage frame;
  frame.width = 97;
  frame.height = 61;
  frame.row_stride = frame.width * 4;
  frame.revision = 19;
  frame.pixels.resize(static_cast<std::size_t>(frame.row_stride) * frame.height);
  for (std::uint32_t y = 0; y < frame.height; ++y) {
    for (std::uint32_t x = 0; x < frame.width; ++x) {
      auto* px = frame.pixels.data() + static_cast<std::size_t>(y) * frame.row_stride + x * 4;
      px[0] = static_cast<std::uint8_t>((x * 255) / (frame.width - 1));
      px[1] = static_cast<std::uint8_t>((y * 255) / (frame.height - 1));
      px[2] = static_cast<std::uint8_t>(((x + y) * 37) % 256);
      px[3] = static_cast<std::uint8_t>(y % 7 == 0 ? 128 : 255);
      if (x % 11 == 0) px[1] = px[2] = px[0];  // greys
    }
  }
  using pixcake::render::RenderValue;
  const auto v = [](const char* channel, double number) {
    return RenderValue{channel, channel, std::to_string(number), number};
  };
  const auto global = [](std::vector<RenderValue> values) {
    pixcake::render::RenderOperation op;
    op.capability_id = "color.global";
    op.stage = "color";
    op.program = "pixcake.color.global";
    op.values = std::move(values);
    return op;
  };
  const auto grain = [](std::vector<RenderValue> values) {
    pixcake::render::RenderOperation op;
    op.capability_id = "detail";
    op.stage = "detail.grain";
    op.program = "pixcake.detail";
    op.order = 9;
    op.values = std::move(values);
    return op;
  };
  struct Case {
    const char* name;
    std::vector<pixcake::render::RenderOperation> operations;
  };
  const Case cases[] = {
      {"tone", {global({v("temperature", -30), v("exposure", 0.4), v("brightness", 10), v("contrast", 25),
                        v("highlights", -40), v("shadows", 35), v("whites", 20), v("blacks", -15), v("clarity", 30),
                        v("texture", 10), v("dehaze", 5), v("vibrance", 40), v("saturation", -20)})}},
      {"levels", {global({v("levels_rgb_input_black", 20), v("levels_rgb_input_white", 230),
                          v("levels_rgb_input_gray", 0.3), v("levels_rgb_output_black", 10),
                          v("levels_rgb_output_white", 240)})}},
      {"hsl", {global({v("hsl_hue_red", 60), v("hsl_saturation_green", -50), v("hsl_luminance_blue", 70),
                       v("hsl_hue_aqua", -40), v("hsl_luminance_orange", -60), v("hsl_saturation_magenta", 80)})}},
      {"black-and-white", {global({{"profile", "profile", "monochrome", 2.0}, v("bw_mix_red", 60),
                                   v("bw_mix_blue", -70), v("bw_mix_yellow", 40)})}},
      {"selective relative", {global({v("selective_red_cyan", 50), v("selective_neutral_black", 30),
                                      v("selective_blue_yellow", -60), v("selective_white_magenta", 40)})}},
      {"selective absolute", {global({{"selective_mode", "selective_mode", "absolute", 1.0},
                                      v("selective_green_magenta", 70), v("selective_black_black", -50)})}},
      {"grading", {global({v("grading_shadows_hue", 220), v("grading_shadows_saturation", 60),
                           v("grading_highlights_hue", 40), v("grading_highlights_saturation", 50),
                           v("grading_midtones_luminance", -30), v("grading_global_saturation", 20),
                           v("grading_global_hue", 300), v("grading_blending", 80)})}},
      {"grain", {grain({v("grain_amount", 80), v("grain_size", 30), v("grain_roughness", 60), v("grain_color", 50),
                        v("grain_shadows", 70), v("grain_midtones", 100), v("grain_highlights", 40)})}},
      {"colour then grain", {global({v("exposure", 0.3), v("hsl_hue_red", 30), v("grading_global_saturation", 30)}),
                             grain({v("grain_amount", 60)})}},
  };
  for (const auto& item : cases) {
    pixcake::render::RenderDescription multi;
    multi.document_revision = 19;
    multi.operations = item.operations;
    pixcake::gpu::RenderResult on_gpu;
    pixcake::gpu::RenderResult on_cpu;
    const bool drawn = executor.execute(context, multi, frame, inputs, on_gpu, error);
    EXPECT(drawn);
    EXPECT(!on_gpu.reference_fallback);
    EXPECT(executor.renderReference(multi, frame, inputs, on_cpu));
    int worst = 0;
    std::size_t off = 0;
    if (drawn && on_gpu.image.pixels.size() == on_cpu.image.pixels.size()) {
      for (std::size_t i = 0; i < on_cpu.image.pixels.size(); ++i) {
        const int diff = std::abs(static_cast<int>(on_gpu.image.pixels[i]) - static_cast<int>(on_cpu.image.pixels[i]));
        worst = std::max(worst, diff);
        if (diff > 0) ++off;
      }
    } else {
      worst = 999;
    }
    std::printf("gpu readback %-20s max diff %d, %zu of %zu channels differ\n", item.name, worst, off,
                on_cpu.image.pixels.size());
    if (worst > 1) std::fprintf(stderr, "FAIL %s: GPU differs from reference by %d (%s)\n", item.name, worst, error.c_str());
    if (worst > 1) ++failures;
    EXPECT(on_gpu.statuses.size() == on_cpu.statuses.size());
  }
  std::printf("native GPU backend active\n");
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
