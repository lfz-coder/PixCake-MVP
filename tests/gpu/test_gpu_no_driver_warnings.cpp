// The GPU path leaves no sampler on an unloadable texture: macOS's GL driver logs
// "... is unloadable and bound to sampler type ..." (once per process) when a draw samples, or
// merely declares, a unit with no complete texture.  The warning is logged once per process, so
// each kind of draw runs in a fresh process of its own (this program with the draw's index), with
// stderr captured; an earlier draw cannot leave a unit bound for a later one.
#include <pixcake/gl/native_backend.hpp>
#include <pixcake/gpu/render_executor.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

int main(int argc, char** argv) {
  auto context = pixcake::gl::native::create_context();
  if (!context.valid() || !context.makeCurrent()) {
    std::printf("no native GL context, skipped\n");
    return 0;
  }

  using pixcake::render::RenderValue;
  const auto v = [](const char* channel, double n) { return RenderValue{channel, channel, std::to_string(n), n}; };
  const auto op = [](const char* capability, std::vector<RenderValue> values, std::vector<std::string> masks = {}) {
    pixcake::render::RenderOperation o;
    o.capability_id = capability;
    o.stage = capability == std::string("detail") ? "detail" : "color";
    o.program = std::string("pixcake.") + capability;
    o.values = std::move(values);
    o.mask_ids = std::move(masks);
    return o;
  };
  const auto frame = [](std::uint32_t w, std::uint32_t h) {
    pixcake::gpu::RgbaImage f;
    f.width = w;
    f.height = h;
    f.row_stride = w * 4;
    f.revision = 3;
    for (std::uint32_t i = 0; i < w * h; ++i) f.pixels.insert(f.pixels.end(), {static_cast<std::uint8_t>(i * 7), static_cast<std::uint8_t>(i * 13), 90, 255});
    return f;
  };
  pixcake::gpu::RenderInputs masked;
  masked.masks.push_back({"m", 64, 48, std::vector<std::uint8_t>(64 * 48, 200)});
  pixcake::gpu::RenderInputs layered = masked;
  {
    pixcake::gpu::RgbaImage image = frame(64, 48);
    image.revision = 0;
    layered.layers.push_back(pixcake::gpu::RenderLayer{image, "", 0.5F, "multiply", true, 1});
    layered.layers.push_back(pixcake::gpu::RenderLayer{image, "m", 0.7F, "screen", true, 2});
  }
  const struct {
    const char* name;
    std::vector<pixcake::render::RenderOperation> ops;
    pixcake::gpu::RgbaImage input;
    const pixcake::gpu::RenderInputs* inputs;
  } draws[] = {
      {"1x1 colour", {op("color.global", {v("temperature", 40), v("exposure", 0.5), v("contrast", 20)})}, frame(1, 1), nullptr},
      {"colour + hsl", {op("color.global", {v("exposure", 0.5), v("hsl_hue_red", 10)})}, frame(64, 48), nullptr},
      {"masked local", {op("color.local", {v("exposure", 0.5)}, {"m"})}, frame(64, 48), &masked},
      {"spatial", {op("color.global", {v("shadows", 40), v("clarity", 20), v("vibrance", 10)})}, frame(64, 48), nullptr},
      {"grain", {op("detail", {v("grain_amount", 30)})}, frame(64, 48), nullptr},
      {"sharpen + grain", {op("detail", {v("sharpen_amount", 80), v("noise_luminance", 30), v("grain_amount", 20)})}, frame(64, 48), nullptr},
      {"layers", {op("color.global", {v("exposure", 0.2)})}, frame(64, 48), &layered},
      {"curves", {op("color.global", {RenderValue{"curve_points_rgb", "curve_points_rgb", "0,0;128,160;255,255", 0.0}})}, frame(64, 48), nullptr},
  };
  pixcake::gpu::RenderExecutor executor;
  executor.setSpatialGpuMinPixels(0);
  pixcake::gpu::RenderResult result;
  std::string error;
  const std::size_t count = sizeof draws / sizeof draws[0];
  if (argc < 2) {
    // The parent: one child per draw.
    bool all = true;
    for (std::size_t k = 0; k < count; ++k) {
      const std::string log = std::string(argv[0]) + "." + std::to_string(k) + ".stderr";
      const std::string command = "'" + std::string(argv[0]) + "' " + std::to_string(k) + " 2> '" + log + "'";
      const int status = std::system(command.c_str());
      std::ifstream in(log);
      std::stringstream text;
      text << in.rdbuf();
      const bool warned = text.str().find("unloadable") != std::string::npos;
      std::printf("%-16s %s%s\n", draws[k].name, status == 0 ? "drawn" : "FAILED", warned ? ", driver warning" : "");
      if (warned) std::printf("  %s", text.str().c_str());
      all = all && status == 0 && !warned;
    }
    return all ? 0 : 1;
  }
  bool ok = true;
  const std::size_t only = static_cast<std::size_t>(std::atoi(argv[1]));
  for (std::size_t k = 0; k < count; ++k) {
    if (k != only) continue;
    const auto& item = draws[k];
    pixcake::render::RenderDescription d;
    d.document_revision = 3;
    d.operations = item.ops;
    const bool drawn = executor.execute(context, d, item.input, item.inputs ? *item.inputs : pixcake::gpu::RenderInputs{}, result, error) &&
                       !result.reference_fallback;
    if (!drawn) std::fprintf(stderr, "draw %s failed: %s\n", item.name, error.c_str());
    ok = ok && drawn;
    std::fprintf(stderr, "after %s\n", item.name);
    std::fflush(stderr);
  }

  return ok ? 0 : 1;
}
