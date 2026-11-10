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
  pixcake::gpu::RenderInputs masked;
  masked.masks.push_back({"semantic:person.skin", input.width, input.height,
                          std::vector<std::uint8_t>(static_cast<std::size_t>(input.width) * input.height, 180)});
  pixcake::gpu::RenderInputs layered = masked;
  layered.layers.push_back({input, "semantic:person.skin", 0.8F, "screen", true, 1});
  layered.layers.back().image.revision = 0;
  const auto local = [] {
    auto description = op("color.local", "color", "pixcake.color.local", {value("exposure", 0.5)});
    description.operations.back().mask_ids = {"semantic:person.skin"};
    return description;
  };
  struct Case {
    const char* name;
    RenderDescription description;
    const pixcake::gpu::RenderInputs* inputs = nullptr;
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
      {"local, one mask", local(), &masked},
      {"local + screen layer", local(), &layered},
  };
  auto context = pixcake::gl::native::create_context();
  const bool gpu = pixcake::gl::native::available() && context.valid() && context.makeCurrent();
  if (gpu) context.doneCurrent();
  pixcake::gpu::RenderExecutor executor;  // one per context, as renderd keeps it
  std::printf("%-20s %10s %10s\n", "", "reference", gpu ? "gpu" : "gpu (none)");
  for (const auto& item : cases) {
    pixcake::gpu::RenderResult result;
    const double reference = best_ms([&] {
      return pixcake::gpu::RenderExecutor{}.renderReference(item.description, input, item.inputs ? *item.inputs : pixcake::gpu::RenderInputs{}, result);
    });
    double on_gpu = -1;
    if (gpu) {
      on_gpu = best_ms([&] {
        std::string error;
        return executor.execute(context, item.description, input, item.inputs ? *item.inputs : pixcake::gpu::RenderInputs{}, result, error) &&
               !result.reference_fallback;
      });
    }
    std::printf("%-20s %7.1f ms %7.1f ms\n", item.name, reference, on_gpu);
  }
  // A 24 MP export (6000 x 4000) is drawn in 4096 px tiles on the GPU.
  pixcake::gpu::RgbaImage full;
  full.width = 6000;
  full.height = 4000;
  full.row_stride = full.width * 4;
  full.pixels.resize(static_cast<std::size_t>(full.row_stride) * full.height);
  for (std::size_t i = 0; i < full.pixels.size(); ++i) full.pixels[i] = static_cast<std::uint8_t>((i * 2654435761u) >> 24);
  const auto& all = cases[6].description;
  pixcake::gpu::RenderResult result;
  const double reference = best_ms([&] { return pixcake::gpu::RenderExecutor{}.renderReference(all, full, {}, result); });
  double on_gpu = -1;
  if (gpu) {
    on_gpu = best_ms([&] {
      std::string error;
      return executor.execute(context, all, full, {}, result, error) && !result.reference_fallback;
    });
  }
  std::printf("%-20s %7.1f ms %7.1f ms\n", "24 MP export, all", reference, on_gpu);

  // The shell uses this interactive edge while a slider is being dragged.
  // It keeps the full-quality max_edge for commit, but bounds the amount of
  // work and the shared-memory frame copied for each transient value.
  pixcake::gpu::RgbaImage interactive;
  interactive.width = 600;
  interactive.height = 450;
  interactive.row_stride = interactive.width * 4;
  interactive.pixels.resize(static_cast<std::size_t>(interactive.row_stride) * interactive.height);
  for (std::size_t i = 0; i < interactive.pixels.size(); ++i) interactive.pixels[i] = static_cast<std::uint8_t>((i * 2654435761u) >> 24);
  const double interactive_reference = best_ms([&] {
    return pixcake::gpu::RenderExecutor{}.renderReference(all, interactive, {}, result);
  });
  std::printf("%-20s %7.1f ms %7.1f ms\n", "interactive 600px, all", interactive_reference, -1.0);

  // Camera Raw's whole-frame stages (dehaze, Highlights/Shadows, Texture/Clarity, Whites/Blacks)
  // run on the reference only; the GPU falls back to it.  The 4000 px frame is timed once.
  const auto spatial = op("color.global", "tone", "pixcake.color.global",
                          {value("dehaze", 30), value("shadows", 50), value("highlights", -40), value("texture", 20),
                           value("clarity", 30), value("whites", 20), value("blacks", -20)});
  const double spatial_interactive = best_ms([&] { return pixcake::gpu::RenderExecutor{}.renderReference(spatial, interactive, {}, result); });
  std::printf("%-20s %7.1f ms %7.1f ms\n", "interactive 600px, spatial", spatial_interactive, -1.0);
  const auto start = std::chrono::steady_clock::now();
  const bool spatial_ok = pixcake::gpu::RenderExecutor{}.renderReference(spatial, input, {}, result);
  const double spatial_preview = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  std::printf("%-20s %7.1f ms %7.1f ms\n", "4000px, spatial (1 run)", spatial_ok ? spatial_preview : -1.0, -1.0);
  return 0;
}
