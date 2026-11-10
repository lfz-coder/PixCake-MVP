// Contract coverage for the native Core OpenGL backend (checklist C57-C60):
// facade failure paths without a context, then a runtime smoke through the
// same RenderExecutor used by renderd/exportd.
#include <pixcake/gl/native_backend.hpp>
#include <pixcake/gpu/render_executor.hpp>

#include "spatial_frames.hpp"

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
  executor.setSpatialGpuMinPixels(0);  // the test frames are small; the default sends them to the reference
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
  const auto pts = [](const char* channel, const char* text) { return RenderValue{channel, channel, text, 0.0}; };
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
  const auto colour = [](const char* capability, const char* program, std::vector<RenderValue> values,
                          std::vector<std::string> masks = {}, const char* blend = "normal") {
    pixcake::render::RenderOperation op;
    op.capability_id = capability;
    op.stage = "color";
    op.program = program;
    op.order = 5;
    op.values = std::move(values);
    op.mask_ids = std::move(masks);
    op.blend_mode = blend;
    return op;
  };
  // Masks and layers with soft edges, zeros and full values.
  pixcake::gpu::RenderInputs extras;
  for (int m = 0; m < 5; ++m) {
    pixcake::gpu::RenderMask mask{"m" + std::to_string(m), frame.width, frame.height, {}};
    mask.alpha.resize(static_cast<std::size_t>(frame.width) * frame.height);
    for (std::uint32_t i = 0; i < mask.alpha.size(); ++i) {
      const std::uint32_t x = i % frame.width, y = i / frame.width;
      mask.alpha[i] = static_cast<std::uint8_t>(x < 8 ? 0 : x > 88 ? 255 : ((x * (13 + m * 7) + y * (5 + m)) % 256));
    }
    extras.masks.push_back(std::move(mask));
  }
  const auto layer = [&](std::uint32_t order, const char* mask, float opacity, const char* blend, bool visible) {
    pixcake::gpu::RgbaImage image = frame;
    image.revision = 0;
    for (std::size_t i = 0; i < image.pixels.size(); ++i) {
      image.pixels[i] = static_cast<std::uint8_t>(i % 4 == 3 ? (i / 4) % 3 == 0 ? 200 : 255 : (i * (7 + order)) % 256);
    }
    return pixcake::gpu::RenderLayer{image, mask, opacity, blend, visible, order};
  };
  pixcake::gpu::RenderInputs layered = extras;
  layered.layers.push_back(layer(3, "m1", 0.7F, "normal", true));
  layered.layers.push_back(layer(1, "", 0.5F, "multiply", true));
  layered.layers.push_back(layer(2, "m2", 1.0F, "screen", true));
  layered.layers.push_back(layer(4, "", 1.0F, "normal", false));
  struct Case {
    const char* name;
    std::vector<pixcake::render::RenderOperation> operations;
    const pixcake::gpu::RenderInputs* inputs = nullptr;
    bool on_gpu = true;
    // Camera Raw's whole-frame stages need the whole frame on one tile: a frame larger than
    // the GPU tile (the 32 px tiled run here) falls back to the reference, whole.
    bool tiles_on_gpu = true;
  };
  const Case cases[] = {
      {"tone", {global({v("temperature", -30), v("exposure", 0.4), v("brightness", 10), v("contrast", 25),
                        v("vibrance", 40), v("saturation", -20)})}},
      // Camera Raw's whole-frame stages have no shader yet: the operation runs on the reference,
      // over the whole frame even when the export is drawn in tiles.
      {"tone, whole-frame", {global({v("exposure", 0.4), v("contrast", 25), v("highlights", -40),
                                     v("shadows", 35), v("whites", 20), v("blacks", -15),
                                     v("clarity", 30), v("texture", 10), v("dehaze", 5)})},
       nullptr, true, false},
      {"exposure 2 + spatial", {global({v("exposure", 2.0), v("dehaze", 30), v("shadows", 50), v("highlights", -40),
                                         v("clarity", 20)})},
       nullptr, true, false},
      {"local spatial, 2 masks", {colour("color.local", "pixcake.color.local", {v("shadows", 60), v("whites", 30)},
                                         {"m1", "m2"}, "multiply")},
       &extras, true, false},
      {"spatial then grain", {global({v("texture", 40), v("blacks", -30), v("hsl_hue_red", 20)}), grain({v("grain_amount", 40)})},
       nullptr, true, false},
      // Camera Raw stage 8 (sharpening, noise reduction) on the whole frame, then grain on its result.
      {"sharpen + nr", {grain({v("sharpen_amount", 100), v("sharpen_radius", 1.5), v("noise_luminance", 40), v("noise_color", 50)})},
       nullptr, true, false},
      {"colour, detail + grain", {global({v("exposure", 0.3), v("hsl_hue_red", 30)}),
                                  grain({v("sharpen_amount", 80), v("sharpen_detail", 60), v("noise_color", 30), v("grain_amount", 30)})},
       nullptr, true, false},
      // Camera Raw stage 1b, the calibration primaries, in the colour step.
      {"calibration", {global({v("calibration_red_hue", 50), v("calibration_green_hue", -40), v("calibration_blue_hue", 70),
                               v("calibration_red_saturation", -30), v("calibration_green_saturation", 60),
                               v("calibration_blue_saturation", 100)})}},
      {"calibration after balance", {global({v("temperature", -40), v("exposure", 1.5), v("calibration_red_hue", -60),
                                             v("calibration_blue_saturation", -80), v("brightness", 20), v("hsl_hue_red", 30)})}},
      {"spatial + calibration", {global({v("dehaze", 30), v("shadows", 40), v("calibration_green_hue", 50),
                                         v("calibration_red_saturation", 40)})},
       nullptr, true, false},
      // Camera Raw stage 5: the curve tables on an R32F texture (unit 4).
      {"curves", {global({v("curve_highlights", -40), v("curve_lights", 30), v("curve_darks", -20), v("curve_shadows", 50),
                          pts("curve_points_rgb", "0,0;64,40;192,220;255,255"), pts("curve_points_red", "0,20;128,150;255,240"),
                          pts("curve_points_blue", "0,0;128,100;255,230")})}},
      {"exposure 2 + curves", {global({v("exposure", 2.0), v("curve_shadows", 40), pts("curve_points_green", "0,0;100,90;255,255")})}},
      {"curves, levels, hsl", {global({v("vibrance", 30), v("levels_rgb_input_black", 20), v("curve_lights", 40),
                                       pts("curve_points_rgb", "0,30;255,255"), v("hsl_hue_red", 40), v("grading_shadows_saturation", 40),
                                       v("grading_shadows_hue", 200)})}},
      {"spatial + curves", {global({v("shadows", 40), v("clarity", 20), pts("curve_points_rgb", "0,0;64,40;192,220;255,255"),
                                    v("curve_highlights", -30)})},
       nullptr, true, false},
      {"local curves (reference only)", {colour("color.local", "pixcake.color.local", {v("exposure", 0.3), pts("curve_points_rgb", "0,0;128,160;255,255")},
                                         {"m1"}, "normal")},
       &extras, false},
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
      // PhotoCraft Camera Raw stages and Photoshop modern brightness, one at a time.
      {"wb_warm", {global({v("temperature", 60), v("tint", -20)})}},
      {"exposure_brightness", {global({v("exposure", 0.5), v("brightness", 40)})}},
      {"brightness_down", {global({v("brightness", -60)})}},
      {"contrast_cr", {global({v("contrast", 70)})}},
      {"presence", {global({v("vibrance", 60), v("saturation", -30)})}},
      {"grading_cr", {global({v("grading_shadows_hue", 220), v("grading_shadows_saturation", 60),
                              v("grading_shadows_luminance", -30), v("grading_midtones_hue", 40),
                              v("grading_midtones_saturation", 50), v("grading_midtones_luminance", 20),
                              v("grading_highlights_hue", 50), v("grading_highlights_saturation", 70),
                              v("grading_highlights_luminance", 30), v("grading_blending", 20)})}},
      // PhotoCraft Photoshop adjustments, blending and Camera Raw grain.
      {"levels_pc", {global({v("levels_rgb_input_black", 44), v("levels_rgb_input_white", 214),
                             v("levels_rgb_input_gray", 0.3), v("levels_rgb_output_black", 48),
                             v("levels_rgb_output_white", 240)})}},
      {"selective_abs_pc", {global({{"selective_mode", "selective_mode", "absolute", 1.0}, v("selective_red_cyan", 80),
                                    v("selective_neutral_black", -40), v("selective_blue_yellow", 60),
                                    v("selective_white_magenta", -70)})}},
      {"grain_pc", {grain({v("grain_amount", 80), v("grain_size", 50), v("grain_roughness", 60),
                           v("grain_shadows", 30), v("grain_midtones", 50), v("grain_highlights", 15),
                           v("grain_color", 0)})}},
      {"grain_ext", {grain({v("grain_amount", 70), v("grain_size", 10), v("grain_roughness", 20),
                            v("grain_shadows", 80), v("grain_midtones", 20), v("grain_highlights", 60),
                            v("grain_color", 50)})}},
      {"grain", {grain({v("grain_amount", 80), v("grain_size", 30), v("grain_roughness", 60), v("grain_color", 50),
                        v("grain_shadows", 70), v("grain_midtones", 100), v("grain_highlights", 40)})}},
      {"colour then grain", {global({v("exposure", 0.3), v("hsl_hue_red", 30), v("grading_global_saturation", 30)}),
                             grain({v("grain_amount", 60)})}},
      {"local, one mask", {colour("color.local", "pixcake.color.local", {v("exposure", 0.6), v("saturation", 30),
                                                                          v("temperature", 40)}, {"m0"})}, &extras},
      {"local, 3 masks, mult", {colour("color.local", "pixcake.color.local", {v("contrast", 40), v("saturation", 50)},
                                       {"m1", "m2", "m3"}, "multiply")}, &extras},
      {"post, 4 masks, screen", {colour("color.post", "pixcake.color.post", {v("brightness", -20), v("vibrance", 50)},
                                        {"m0", "m1", "m2", "m3"}, "screen")}, &extras},
      {"AI match and style", {colour("color.match", "pixcake.color.match", {v("amount", 150)}),
                              colour("color.style", "pixcake.color.style", {v("amount", 80)}, {"m4"})}, &extras},
      {"global, masked, grain", {global({v("hsl_hue_red", 40), v("exposure", -0.3)}),
                                 colour("color.local", "pixcake.color.local", {v("exposure", 0.8)}, {"m2"}),
                                 grain({v("grain_amount", 40)})}, &extras},
      {"layers", {global({v("contrast", 20)})}, &layered},
      {"layers only", {}, &layered},
      {"five masks (reference)", {colour("color.local", "pixcake.color.local", {v("exposure", 0.5)},
                                         {"m0", "m1", "m2", "m3", "m4"})}, &extras, false},
  };
  // The same cases drawn in 32 px tiles (full-resolution export) from a frame
  // whose rows are padded: tiles must join without seams (grain included).
  pixcake::gpu::RenderExecutor tiled;
  tiled.setGpuTileSize(32);
  tiled.setSpatialGpuMinPixels(0);
  pixcake::gpu::RgbaImage padded = frame;
  padded.row_stride = frame.row_stride + 12;
  padded.pixels.assign(static_cast<std::size_t>(padded.row_stride) * frame.height, 0);
  for (std::uint32_t y = 0; y < frame.height; ++y) {
    std::copy_n(frame.pixels.data() + static_cast<std::size_t>(y) * frame.row_stride, frame.row_stride,
                padded.pixels.data() + static_cast<std::size_t>(y) * padded.row_stride);
  }
  const auto worst_of = [&](const pixcake::gpu::RgbaImage& a, const pixcake::gpu::RgbaImage& b, std::size_t& off) {
    int worst = 0;
    off = 0;
    if (a.width != b.width || a.height != b.height) return 999;
    const std::size_t sa = a.row_stride == 0 ? a.width * 4 : a.row_stride, sb = b.row_stride == 0 ? b.width * 4 : b.row_stride;
    if (a.pixels.size() < sa * a.height || b.pixels.size() < sb * b.height) return 999;
    for (std::uint32_t y = 0; y < a.height; ++y) {
      for (std::uint32_t x = 0; x < a.width * 4; ++x) {
        const int diff = std::abs(static_cast<int>(a.pixels[y * sa + x]) - static_cast<int>(b.pixels[y * sb + x]));
        worst = std::max(worst, diff);
        if (diff > 0) ++off;
      }
    }
    return worst;
  };
  for (const auto& item : cases) {
    pixcake::render::RenderDescription multi;
    multi.document_revision = 19;
    multi.operations = item.operations;
    const pixcake::gpu::RenderInputs& case_inputs = item.inputs != nullptr ? *item.inputs : inputs;
    pixcake::gpu::RenderResult on_gpu;
    pixcake::gpu::RenderResult on_cpu;
    const bool drawn = executor.execute(context, multi, frame, case_inputs, on_gpu, error);
    EXPECT(drawn);
    if (on_gpu.reference_fallback == item.on_gpu) {
      std::fprintf(stderr, "FAIL %s: expected %s\n", item.name, item.on_gpu ? "the GPU path" : "a reference fallback");
      ++failures;
    }
    EXPECT(executor.renderReference(multi, frame, case_inputs, on_cpu));
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
    if (item.on_gpu && item.tiles_on_gpu) {
      pixcake::gpu::RenderResult in_tiles;
      std::string tile_error;
      const bool tiles_drawn = tiled.execute(context, multi, padded, case_inputs, in_tiles, tile_error);
      std::size_t tile_off = 0;
      const int tile_worst = tiles_drawn && !in_tiles.reference_fallback ? worst_of(in_tiles.image, on_cpu.image, tile_off) : 999;
      std::printf("gpu tiles    %-20s max diff %d, %zu differ\n", item.name, tile_worst, tile_off);
      if (tile_worst > 1) {
        std::fprintf(stderr, "FAIL %s in tiles: differs from reference by %d (%s)\n", item.name, tile_worst, tile_error.c_str());
        ++failures;
      }
    } else {
      // A fallback renders the whole frame on the reference, so tiling cannot cut it up.
      pixcake::gpu::RenderResult in_tiles;
      std::string tile_error;
      const bool tiles_drawn = tiled.execute(context, multi, padded, case_inputs, in_tiles, tile_error);
      std::size_t tile_off = 0;
      const int tile_worst = tiles_drawn && in_tiles.reference_fallback ? worst_of(in_tiles.image, on_cpu.image, tile_off) : 999;
      std::printf("gpu tiles    %-20s max diff %d, %zu differ (reference)\n", item.name, tile_worst, tile_off);
      if (tile_worst != 0) {
        std::fprintf(stderr, "FAIL %s in tiles: fallback differs from reference by %d\n", item.name, tile_worst);
        ++failures;
      }
    }
    bool same_statuses = on_gpu.statuses.size() == on_cpu.statuses.size();
    for (std::size_t i = 0; same_statuses && i < on_cpu.statuses.size(); ++i) {
      same_statuses = on_gpu.statuses[i].status == on_cpu.statuses[i].status &&
                      on_gpu.statuses[i].capability_id == on_cpu.statuses[i].capability_id;
    }
    EXPECT(same_statuses);
  }
  // With the default minimum, a frame this small draws whole-frame operations on the reference.
  {
    pixcake::render::RenderDescription d;
    d.document_revision = 19;
    d.operations = {global({v("shadows", 50), v("dehaze", 20)})};
    pixcake::gpu::RenderExecutor defaults;
    pixcake::gpu::RenderResult on_gpu, on_cpu;
    const bool drawn = defaults.execute(context, d, frame, inputs, on_gpu, error);
    EXPECT(defaults.renderReference(d, frame, inputs, on_cpu));
    std::size_t off = 0;
    const int worst = drawn && on_gpu.reference_fallback ? worst_of(on_gpu.image, on_cpu.image, off) : 999;
    std::printf("gpu readback %-20s max diff %d (reference)\n", "spatial, small frame", worst);
    if (worst != 0) {
      std::fprintf(stderr, "FAIL spatial, small frame: expected the reference, diff %d\n", worst);
      ++failures;
    }
  }
  // A 1000 x 750 scene: dehaze's proxy (k = 2) and Clarity's subsampled guided filter (s = 2) in
  // the executor path.
  {
    pixcake::gpu::RgbaImage scene = spatial_frames::scene(1000, 750);
    scene.revision = 19;
    pixcake::render::RenderDescription d;
    d.document_revision = 19;
    d.operations = {global({v("exposure", 0.3), v("dehaze", 40), v("shadows", 50), v("highlights", -50), v("texture", 30),
                            v("clarity", 40), v("contrast", 20), v("whites", 25), v("blacks", -25), v("vibrance", 20)})};
    pixcake::gpu::RenderResult on_gpu, on_cpu;
    const bool drawn = executor.execute(context, d, scene, inputs, on_gpu, error);
    EXPECT(executor.renderReference(d, scene, inputs, on_cpu));
    std::size_t off = 0;
    const int worst = drawn && !on_gpu.reference_fallback ? worst_of(on_gpu.image, on_cpu.image, off) : 999;
    std::printf("gpu readback %-20s max diff %d, %zu differ\n", "scene 1000x750", worst, off);
    // The float planes go back to the GPU when the render ends: renderd and exportd live long
    // and see many frame sizes (crops, other photos), so a pool kept per size would only grow.
    const std::size_t pooled = executor.gpuPooledBytes();
    std::printf("gpu pooled after spatial render: %zu bytes\n", pooled);
    if (pooled != 0) {
      std::fprintf(stderr, "FAIL spatial pool: %zu bytes still held after the render\n", pooled);
      ++failures;
    }
    if (worst > 1) {
      std::fprintf(stderr, "FAIL scene 1000x750: GPU differs from reference by %d (%s)\n", worst, error.c_str());
      ++failures;
    }
  }
  // A 7 x 1 frame with every whole-frame control: the stages' early outs on the GPU.
  {
    pixcake::gpu::RgbaImage tiny;
    tiny.width = 7;
    tiny.height = 1;
    tiny.row_stride = 28;
    tiny.pixels.assign(frame.pixels.begin(), frame.pixels.begin() + 28);
    pixcake::render::RenderDescription d;
    d.document_revision = 19;
    d.operations = {global({v("dehaze", 40), v("shadows", 40), v("highlights", -40), v("texture", 40), v("clarity", 40),
                            v("whites", 40), v("blacks", -40), v("contrast", 20)})};
    tiny.revision = 19;
    pixcake::gpu::RenderResult on_gpu, on_cpu;
    const bool drawn = executor.execute(context, d, tiny, inputs, on_gpu, error);
    EXPECT(executor.renderReference(d, tiny, inputs, on_cpu));
    std::size_t off = 0;
    const int worst = drawn && !on_gpu.reference_fallback ? worst_of(on_gpu.image, on_cpu.image, off) : 999;
    std::printf("gpu readback %-20s max diff %d\n", "tiny 7x1", worst);
    if (worst > 1) {
      std::fprintf(stderr, "FAIL tiny 7x1: GPU differs from reference by %d (%s)\n", worst, error.c_str());
      ++failures;
    }
  }
  std::printf("native GPU backend active\n");
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
