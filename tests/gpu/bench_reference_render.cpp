// Time of one frame at the preview's maximum size (4000 x 3000) for each
// colour group, on the reference renderer and on the GPU path renderd uses
// (native offscreen context), so slider drags can be judged against P-15.
// Not a test: run the Release build's binary by hand.  Milliseconds per frame.
#include <pixcake/gl/native_backend.hpp>
#include <pixcake/gpu/render_executor.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <utility>

namespace {

using pixcake::render::RenderDescription;
using pixcake::render::RenderValue;

RenderValue value(const char* channel, double numeric) { return {channel, channel, std::to_string(numeric), numeric}; }

RenderDescription op(const char* capability, const char* stage, const char* program,
                     std::initializer_list<RenderValue> values) {
  RenderDescription description;
  pixcake::render::RenderOperation item;
  item.capability_id = capability;
  item.stage = stage;
  item.program = program;
  item.values.assign(values.begin(), values.end());
  description.operations.push_back(std::move(item));
  return description;
}

template <typename Render>
double best_ms(const Render& render) {
  double best = 1e30;
  for (int run = 0; run < 3; ++run) {
    const auto start = std::chrono::steady_clock::now();
    if (!render()) return -1;
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    if (ms < best) best = ms;
  }
  return best;
}

}  // namespace

int main() {
  pixcake::gpu::RgbaImage input;
  input.width = 4000;
  input.height = 3000;
  input.row_stride = input.width * 4;
  input.pixels.resize(static_cast<std::size_t>(input.row_stride) * input.height);
  for (std::size_t i = 0; i < input.pixels.size(); ++i) input.pixels[i] = static_cast<std::uint8_t>((i * 2654435761u) >> 24);
  struct Case {
    const char* name;
    RenderDescription description;
  };
  const Case cases[] = {
      {"exposure (baseline)", op("color.global", "tone", "pixcake.color.global", {value("exposure", 0.5)})},
      {"levels", op("color.global", "color", "pixcake.color.global", {value("levels_rgb_input_black", 20)})},
      {"hsl", op("color.global", "color", "pixcake.color.global", {value("hsl_hue_red", 40)})},
      {"black-and-white", op("color.global", "color", "pixcake.color.global",
                             {{"profile", "profile", "monochrome", 2.0}, value("bw_mix_red", 30)})},
      {"selective color", op("color.global", "color", "pixcake.color.global", {value("selective_red_cyan", 40)})},
      {"color grading", op("color.global", "color", "pixcake.color.global", {value("grading_global_saturation", 40)})},
      {"all colour groups", op("color.global", "color", "pixcake.color.global",
                               {value("exposure", 0.5), value("levels_rgb_input_black", 20), value("hsl_hue_red", 40),
                                value("selective_red_cyan", 40), value("grading_global_saturation", 40)})},
      {"grain", op("detail", "detail.grain", "pixcake.detail", {value("grain_amount", 60)})},
  };
  auto context = pixcake::gl::native::create_context();
  const bool gpu = pixcake::gl::native::available() && context.valid() && context.makeCurrent();
  if (gpu) context.doneCurrent();
  pixcake::gpu::RenderExecutor executor;  // one per context, as renderd keeps it
  std::printf("%-20s %10s %10s\n", "", "reference", gpu ? "gpu" : "gpu (none)");
  for (const auto& item : cases) {
    pixcake::gpu::RenderResult result;
    const double reference = best_ms([&] {
      return pixcake::gpu::RenderExecutor{}.renderReference(item.description, input, pixcake::gpu::RenderInputs{}, result);
    });
    double on_gpu = -1;
    if (gpu) {
      on_gpu = best_ms([&] {
        std::string error;
        return executor.execute(context, item.description, input, pixcake::gpu::RenderInputs{}, result, error) &&
               !result.reference_fallback;
      });
    }
    std::printf("%-20s %7.1f ms %7.1f ms\n", item.name, reference, on_gpu);
  }
  return 0;
}
