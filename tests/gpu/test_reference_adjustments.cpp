// The reference executor runs PhotoCraft's adjustment-layer functions (adjust.cpp: Levels,
// Selective Color, blending) and Camera Raw grain (camera_raw.cpp) with PixCake's parameters.
#include <pixcake/gpu/adjust.hpp>
#include <pixcake/gpu/camera_raw.hpp>
#include <pixcake/gpu/render_executor.hpp>
#include <pixcake/render/render_description.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>

namespace {

namespace adj = pixcake::gpu::adjust;
namespace cr = pixcake::gpu::camera_raw;
using pixcake::gpu::RenderExecutor;
using pixcake::gpu::RenderInputs;
using pixcake::gpu::RenderResult;
using pixcake::gpu::RgbaImage;
using pixcake::render::RenderDescription;
using pixcake::render::RenderValue;

RenderValue value(const char* channel, double numeric) { return {channel, channel, std::to_string(numeric), numeric}; }
RenderValue choice(const char* channel, const char* id, double index) { return {channel, channel, id, index}; }
RenderDescription op(const char* capability, const char* stage, const char* program,
                     std::initializer_list<RenderValue> values, std::vector<std::string> masks = {}) {
  RenderDescription description;
  pixcake::render::RenderOperation item;
  item.capability_id = capability;
  item.stage = stage;
  item.program = program;
  item.values.assign(values.begin(), values.end());
  item.mask_ids = std::move(masks);
  description.operations.push_back(std::move(item));
  return description;
}
RenderDescription global(std::initializer_list<RenderValue> values) {
  return op("color.global", "color", "pixcake.color.global", values);
}
RenderDescription grain(std::initializer_list<RenderValue> values) {
  return op("detail", "detail.grain", "pixcake.detail", values);
}

// A width x height frame whose pixel i is pixel(i).
template <typename F>
RgbaImage frame(std::uint32_t width, std::uint32_t height, F pixel) {
  RgbaImage image;
  image.width = width;
  image.height = height;
  image.row_stride = width * 4;
  for (std::uint32_t i = 0; i < width * height; ++i) {
    const std::array<std::uint8_t, 4> p = pixel(i);
    image.pixels.insert(image.pixels.end(), p.begin(), p.end());
  }
  return image;
}
RenderResult run(const RenderDescription& d, const RgbaImage& input, const RenderInputs& inputs = {}) {
  RenderResult result;
  assert(RenderExecutor{}.renderReference(d, input, inputs, result));
  return result;
}
std::uint8_t q(float v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255.0F)); }
float u(std::uint8_t v) { return v / 255.0F; }
void expect(std::uint8_t got, std::uint8_t want, const char* what, std::size_t i) {
  if (got != want) {
    std::printf("%s [%zu]: got %d want %d\n", what, i, got, want);
    assert(false);
  }
}

// PixCake's composite RGB levels = Photoshop's master channel, 8-bit (quantum 255).
void test_levels_uses_photocraft() {
  const auto greys = frame(16, 16, [](std::uint32_t i) {
    const auto g = static_cast<std::uint8_t>(i);
    return std::array<std::uint8_t, 4>{g, g, g, 255};
  });
  const auto result = run(global({value("levels_rgb_input_black", 44), value("levels_rgb_input_white", 214),
                                  value("levels_rgb_input_gray", 0.3), value("levels_rgb_output_black", 48),
                                  value("levels_rgb_output_white", 240)}),
                          greys);
  const adj::LevelsChannel ch{44.0F / 255.0F, 214.0F / 255.0F, std::pow(10.0F, 0.3F), 48.0F / 255.0F, 240.0F / 255.0F};
  const auto lut = adj::levelsLut(ch, 255.0F);
  for (std::size_t i = 0; i < 256; ++i) expect(result.image.pixels[i * 4], q(adj::sampleLut(lut, u(static_cast<std::uint8_t>(i)))), "levels", i);
}

// Selective Color: PixCake's nine targets in PhotoCraft's order, relative and absolute.
void test_selective_uses_photocraft() {
  const auto colours = frame(8, 8, [](std::uint32_t i) {
    return std::array<std::uint8_t, 4>{static_cast<std::uint8_t>(30 + 30 * (i % 8)), static_cast<std::uint8_t>(240 - 28 * (i / 8)),
                                       static_cast<std::uint8_t>((i * 37) % 256), 255};
  });
  const char* targets[9] = {"red", "yellow", "green", "cyan", "blue", "magenta", "white", "neutral", "black"};
  const char* inks[4] = {"cyan", "magenta", "yellow", "black"};
  for (int absolute = 0; absolute < 2; ++absolute) {
    for (int t = 0; t < 9; ++t) {
      const std::string a = std::string("selective_") + targets[t] + "_" + inks[t % 4];
      const std::string b = std::string("selective_") + targets[t] + "_black";
      const float va = t % 2 == 0 ? 70.0F : -60.0F, vb = t % 3 == 0 ? -40.0F : 30.0F;
      RenderDescription d = global({choice("selective_mode", absolute ? "absolute" : "relative", absolute)});
      d.operations[0].values.push_back({a, a, std::to_string(va), va});
      if (t % 4 != 3) d.operations[0].values.push_back({b, b, std::to_string(vb), vb});
      std::array<std::array<float, 4>, 9> amounts{};
      amounts[t][t % 4] = va;
      if (t % 4 != 3) amounts[t][3] = vb;
      const auto result = run(d, colours);
      for (std::size_t i = 0; i < 64; ++i) {
        const auto* p = &colours.pixels[i * 4];
        const auto want = adj::selectiveColor({u(p[0]), u(p[1]), u(p[2])}, absolute == 0, amounts);
        for (int c = 0; c < 3; ++c) expect(result.image.pixels[i * 4 + c], q(want[c]), targets[t], i);
      }
    }
  }
}

// A masked operation blends its result back by coverage (opacity x mask), alpha kept
// (PhotoCraft composite.rs blend_back) -- the pixel's own alpha is not coverage.
void test_operation_blends_back_by_coverage() {
  const auto translucent = frame(1, 1, [](std::uint32_t) { return std::array<std::uint8_t, 4>{90, 140, 210, 128}; });
  RenderInputs inputs;
  inputs.masks.push_back({"m", 1, 1, {128}});
  const auto result = run(op("color.local", "color", "pixcake.color.local", {value("exposure", 1)}, {"m"}), translucent, inputs);
  const cr::Rgb before{u(90), u(140), u(210)};
  const cr::Rgb adjusted = cr::applyBalance(before, cr::balanceGains(0, 0, 1));
  const float k = 128.0F / 255.0F;
  for (int c = 0; c < 3; ++c) expect(result.image.pixels[c], q(before[c] * (1.0F - k) + adjusted[c] * k), "blend back", c);
  expect(result.image.pixels[3], 128, "alpha kept", 3);
}

// Layers composite with PhotoCraft's psblend::composite (W3C general formula, straight alpha).
void test_layers_use_photocraft_composite() {
  const auto base = frame(1, 1, [](std::uint32_t) { return std::array<std::uint8_t, 4>{200, 100, 50, 128}; });
  for (const char* mode : {"normal", "multiply", "screen"}) {
    RenderInputs inputs;
    pixcake::gpu::RenderLayer layer;
    layer.image = frame(1, 1, [](std::uint32_t) { return std::array<std::uint8_t, 4>{60, 180, 220, 200}; });
    layer.opacity = 0.6F;
    layer.blend_mode = mode;
    inputs.layers.push_back(layer);
    const auto result = run(RenderDescription{}, base, inputs);
    const adj::BlendMode m = std::string(mode) == "multiply" ? adj::BlendMode::Multiply
                             : std::string(mode) == "screen" ? adj::BlendMode::Screen
                                                             : adj::BlendMode::Normal;
    const auto want = adj::composite(m, {u(200), u(100), u(50), u(128)}, {u(60), u(180), u(220), u(200)}, 0.6F);
    for (int c = 0; c < 4; ++c) expect(result.image.pixels[c], q(want[c]), mode, c);
  }
}

RgbaImage gradient(std::uint32_t side) {
  return frame(side, side, [side](std::uint32_t i) {
    return std::array<std::uint8_t, 4>{static_cast<std::uint8_t>(20 + 200 * (i % side) / (side - 1)),
                                       static_cast<std::uint8_t>(240 - 200 * (i / side) / (side - 1)), 128, 255};
  });
}

// At PixCake's default zone sliders (30/50/15) and colour 0, grain is PhotoCraft's.
void test_grain_uses_photocraft() {
  const auto input = gradient(16);
  const auto result = run(grain({value("grain_amount", 60), value("grain_size", 25), value("grain_roughness", 50),
                                 value("grain_shadows", 30), value("grain_midtones", 50), value("grain_highlights", 15),
                                 value("grain_color", 0)}),
                          input);
  const cr::Grain g = cr::grainOf(60, 25, 50, 0, 1.0F);
  for (std::uint32_t i = 0; i < 256; ++i) {
    const auto* p = &input.pixels[i * 4];
    const auto want = cr::applyGrain({u(p[0]), u(p[1]), u(p[2])}, g, static_cast<float>(i % 16), static_cast<float>(i / 16));
    for (int c = 0; c < 3; ++c) expect(result.image.pixels[i * 4 + c], q(want[c]), "grain", i);
  }
}

// A preview frame at half resolution samples the noise where the full-resolution frame does.
void test_grain_full_resolution_coordinates() {
  const auto grey = [](std::uint32_t side) {
    return frame(side, side, [](std::uint32_t) { return std::array<std::uint8_t, 4>{128, 128, 128, 255}; });
  };
  const auto d = grain({value("grain_amount", 100), value("grain_size", 40), value("grain_color", 0)});
  const auto full = run(d, grey(16));
  RenderInputs half;
  half.pixel_scale = 0.5F;
  const auto preview = run(d, grey(8), half);
  for (std::uint32_t y = 0; y < 8; ++y)
    for (std::uint32_t x = 0; x < 8; ++x)
      expect(preview.image.pixels[(y * 8 + x) * 4], full.image.pixels[(2 * y * 16 + 2 * x) * 4], "half scale", y * 8 + x);
}

// The PixCake-specific sub-sliders still act: zones scale the strength, colour decorrelates channels.
void test_grain_extensions() {
  const auto input = gradient(16);
  const auto base = run(grain({value("grain_amount", 80), value("grain_color", 0)}), input);
  const auto no_shadows = run(grain({value("grain_amount", 80), value("grain_color", 0), value("grain_shadows", 0),
                                     value("grain_midtones", 0), value("grain_highlights", 0)}),
                              input);
  assert(no_shadows.image.pixels == input.pixels);
  assert(base.image.pixels != input.pixels);
  const auto colour = run(grain({value("grain_amount", 80), value("grain_color", 100)}), input);
  int differ = 0;
  for (std::size_t i = 0; i < colour.image.pixels.size(); i += 4)
    differ += colour.image.pixels[i] - input.pixels[i] != colour.image.pixels[i + 1] - input.pixels[i + 1];
  assert(differ > 64);
}

}  // namespace

int main() {
  test_levels_uses_photocraft();
  test_selective_uses_photocraft();
  test_operation_blends_back_by_coverage();
  test_layers_use_photocraft_composite();
  test_grain_uses_photocraft();
  test_grain_full_resolution_coordinates();
  test_grain_extensions();
  std::printf("reference adjustments ok\n");
  return 0;
}
