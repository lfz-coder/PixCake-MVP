// Reference formulas for the colour sub-groups of color.global (levels, HSL,
// black-and-white mix, selective color, color grading) and detail grain.
// The formulas are the project's provisional ones (render_executor.cpp); these
// vectors pin them, check identity at the registered defaults, and check that
// each group acts only on the pixels it targets.
#include <pixcake/gpu/render_executor.hpp>
#include <pixcake/editdoc/edit_document.hpp>
#include <pixcake/render/render_description.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>

namespace {

using pixcake::gpu::RenderExecutor;
using pixcake::gpu::RenderInputs;
using pixcake::gpu::RenderResult;
using pixcake::gpu::RgbaImage;
using pixcake::render::RenderDescription;
using pixcake::render::RenderValue;

RgbaImage image(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint32_t side = 1) {
  RgbaImage result;
  result.width = side;
  result.height = side;
  result.row_stride = side * 4;
  for (std::uint32_t i = 0; i < side * side; ++i) result.pixels.insert(result.pixels.end(), {r, g, b, 255});
  return result;
}

RenderValue value(const char* channel, double numeric) { return {channel, channel, std::to_string(numeric), numeric}; }
// Choices travel as their index in the parameter mode's choices, display value the stable id.
RenderValue choice(const char* channel, const char* id, double index) { return {channel, channel, id, index}; }

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
RenderDescription global(std::initializer_list<RenderValue> values) {
  return op("color.global", "color", "pixcake.color.global", values);
}

struct Rgb { int r, g, b; };
Rgb render(const RenderDescription& description, const RgbaImage& input, RenderResult* out = nullptr) {
  RenderResult result;
  const bool ok = RenderExecutor{}.renderReference(description, input, RenderInputs{}, result);
  assert(ok);
  if (out != nullptr) *out = result;
  return {result.image.pixels[0], result.image.pixels[1], result.image.pixels[2]};
}
bool near(Rgb got, Rgb want, int tolerance = 1) {
  const bool ok = std::abs(got.r - want.r) <= tolerance && std::abs(got.g - want.g) <= tolerance &&
                  std::abs(got.b - want.b) <= tolerance;
  if (!ok) std::fprintf(stderr, "got %d %d %d, want %d %d %d\n", got.r, got.g, got.b, want.r, want.g, want.b);
  return ok;
}

void test_identity_at_defaults() {
  // Every group written at its registered default leaves the pixel exactly as it was.
  const auto defaults = global({value("levels_rgb_input_black", 0), value("levels_rgb_input_white", 255),
                                value("levels_rgb_input_gray", 0), value("levels_rgb_output_black", 0),
                                value("levels_rgb_output_white", 255), value("hsl_hue_red", 0),
                                value("selective_red_cyan", 0), choice("selective_mode", "relative", 0),
                                value("grading_global_saturation", 0), value("grading_blending", 50),
                                choice("profile", "standard", 0)});
  assert(near(render(defaults, image(64, 128, 192)), {64, 128, 192}, 0));
  // Absent values take the registered defaults (input white 255, not 0).
  assert(near(render(global({value("levels_rgb_input_black", 0)}), image(64, 128, 192)), {64, 128, 192}, 0));
}

void test_levels() {
  assert(near(render(global({value("levels_rgb_input_black", 64)}), image(64, 64, 64)), {0, 0, 0}));
  assert(near(render(global({value("levels_rgb_input_white", 128)}), image(128, 128, 128)), {255, 255, 255}));
  assert(near(render(global({value("levels_rgb_output_black", 64)}), image(0, 0, 0)), {64, 64, 64}));
  assert(near(render(global({value("levels_rgb_output_white", 128)}), image(255, 255, 255)), {128, 128, 128}));
  // Grey +1: gamma 10, mid grey 0.502^(1/10) = 0.933; -1: gamma 0.1, nearly black.
  assert(near(render(global({value("levels_rgb_input_gray", 1)}), image(128, 128, 128)), {238, 238, 238}));
  assert(near(render(global({value("levels_rgb_input_gray", -1)}), image(128, 128, 128)), {0, 0, 0}));
}

void test_hsl() {
  // Red hue +100 turns pure red 30 degrees, to orange; blue is outside the red band.
  assert(near(render(global({value("hsl_hue_red", 100)}), image(255, 0, 0)), {255, 128, 0}));
  assert(near(render(global({value("hsl_hue_red", 100)}), image(0, 0, 255)), {0, 0, 255}, 0));
  // Red saturation -100 removes the chroma, keeping HSL lightness (200+50)/2.
  assert(near(render(global({value("hsl_saturation_red", -100)}), image(200, 50, 50)), {125, 125, 125}));
  // Greys have no hue: no band moves them.
  assert(near(render(global({value("hsl_luminance_red", 100), value("hsl_saturation_aqua", 100)}),
                     image(128, 128, 128)),
              {128, 128, 128}, 0));
  // Red luminance +100 brightens a red, never past white.
  const Rgb brighter = render(global({value("hsl_luminance_red", 100)}), image(200, 50, 50));
  assert(brighter.r >= 200 && brighter.g > 50 && brighter.b > 50);
}

void test_monochrome() {
  // Black-and-white profile: Rec. 709 luminance of pure red is 0.2126.
  assert(near(render(global({choice("profile", "monochrome", 2)}), image(255, 0, 0)), {54, 54, 54}));
  // Red mix +100 adds half the chroma: 0.2126 + 0.5.
  assert(near(render(global({choice("profile", "monochrome", 2), value("bw_mix_red", 100)}), image(255, 0, 0)),
              {182, 182, 182}));
  // A grey keeps its grey whatever the mix.
  assert(near(render(global({choice("profile", "monochrome", 2), value("bw_mix_red", 100)}), image(90, 90, 90)),
              {90, 90, 90}));
  // Outside the black-and-white profile the mix does nothing.
  assert(near(render(global({value("bw_mix_red", 100)}), image(255, 0, 0)), {255, 0, 0}, 0));
}

void test_selective_color() {
  // (200, 50, 50) belongs to red by (max - mid) = 150/255.  Cyan +100 acts on the red channel.
  assert(near(render(global({value("selective_red_cyan", 100)}), image(200, 50, 50)), {168, 50, 50}));
  assert(near(render(global({value("selective_red_cyan", 100), choice("selective_mode", "absolute", 1)}),
                     image(200, 50, 50)),
              {82, 50, 50}));
  // Relative mode scales ink that is there: pure red has no cyan ink to scale.
  assert(near(render(global({value("selective_red_cyan", 100)}), image(255, 0, 0)), {255, 0, 0}, 0));
  // A blue is not red.
  assert(near(render(global({value("selective_red_cyan", 100)}), image(50, 50, 200)), {50, 50, 200}, 0));
  // Neutral black +100 darkens mid grey to (nearly) black.
  const Rgb grey = render(global({value("selective_neutral_black", 100)}), image(128, 128, 128));
  assert(grey.r <= 2 && grey.g <= 2 && grey.b <= 2);
}

void test_grading() {
  // A red global tint on grey: red up, green and blue down, overall lightness kept.
  const Rgb tinted =
      render(global({value("grading_global_hue", 0), value("grading_global_saturation", 100)}), image(128, 128, 128));
  assert(tinted.r > 160 && tinted.g < 128 && tinted.b < 128);
  const double luma = 0.2126 * tinted.r + 0.7152 * tinted.g + 0.0722 * tinted.b;
  assert(luma > 124 && luma < 132);
  // Shadows lift reaches black fully and does not reach white.
  assert(near(render(global({value("grading_shadows_luminance", 100)}), image(0, 0, 0)), {64, 64, 64}));
  assert(near(render(global({value("grading_shadows_luminance", 100)}), image(255, 255, 255)), {255, 255, 255}, 0));
  // Blending 0 narrows the zones: mid grey is outside the shadows zone (radius 0.5, l = 0.502).
  assert(near(render(global({value("grading_shadows_luminance", 100), value("grading_blending", 0)}),
                     image(128, 128, 128)),
              {128, 128, 128}));
}

void test_color_editor_is_reported_not_drawn() {
  RenderResult result;
  assert(near(render(global({value("color_editor_hue_shift", 50)}), image(200, 50, 50), &result), {200, 50, 50}, 0));
  bool reported = false;
  for (const auto& diagnostic : result.diagnostics) reported = reported || diagnostic.reason.find("色彩编辑器") != std::string::npos;
  assert(reported);
}

void test_grain() {
  const auto detail = [](std::initializer_list<RenderValue> values) {
    return op("detail", "detail.grain", "pixcake.detail", values);
  };
  // Grain has a shader with a readback comparison (test_gpu_backend_C57_C60).
  assert(pixcake::gpu::PassRegistry::classify(detail({value("grain_amount", 50)}).operations[0]) ==
         pixcake::gpu::PassStatus::Implemented);
  // Amount 0 (the default) draws nothing, whatever the other values.
  RenderResult result;
  render(detail({value("grain_amount", 0), value("grain_size", 100)}), image(128, 128, 128, 64), &result);
  for (std::size_t i = 0; i < result.image.pixels.size(); i += 4) assert(result.image.pixels[i] == 128);
  // Amount 100 on mid grey: varies, centred near grey, monochrome at colour 0, repeatable.
  const auto strong = detail({value("grain_amount", 100), value("grain_color", 0)});
  render(strong, image(128, 128, 128, 64), &result);
  RenderResult again;
  render(strong, image(128, 128, 128, 64), &again);
  assert(result.image.pixels == again.image.pixels);
  long sum = 0;
  int low = 255, high = 0;
  for (std::size_t i = 0; i < result.image.pixels.size(); i += 4) {
    const int r = result.image.pixels[i];
    assert(r == result.image.pixels[i + 1] && r == result.image.pixels[i + 2]);
    sum += r;
    low = std::min(low, r);
    high = std::max(high, r);
  }
  const double mean = static_cast<double>(sum) / (64.0 * 64.0);
  assert(mean > 120 && mean < 136 && high - low > 10);
  // Colour 100: channels differ.
  render(detail({value("grain_amount", 100), value("grain_color", 100)}), image(128, 128, 128, 64), &result);
  bool coloured = false;
  for (std::size_t i = 0; i < result.image.pixels.size(); i += 4) {
    coloured = coloured || result.image.pixels[i] != result.image.pixels[i + 1];
  }
  assert(coloured);
}

// The document path: a value set in the edit document compiles into the
// operation the renderer reads (choices as their index), and draws.
void test_document_to_pixels() {
  auto document = *pixcake::EditDocument::create("photo-1");
  assert(document.set_preview("color.global", "profile", "monochrome") == pixcake::ResultCode::Ok);
  auto compiled = pixcake::render::compile(document);
  assert(compiled.code == pixcake::ResultCode::Ok);
  bool found = false;
  for (const auto& operation : compiled.description.operations) {
    for (const auto& item : operation.values) {
      if (item.channel == "profile") {
        found = item.display_value == "monochrome" && item.numeric_value == 2.0;
      }
    }
  }
  assert(found);
  RgbaImage input = image(255, 0, 0);
  input.revision = compiled.description.document_revision;
  assert(near(render(compiled.description, input), {54, 54, 54}));
}

}  // namespace

int main() {
  test_document_to_pixels();
  test_identity_at_defaults();
  test_levels();
  test_hsl();
  test_monochrome();
  test_selective_color();
  test_grading();
  test_color_editor_is_reported_not_drawn();
  test_grain();
  std::printf("reference colour groups ok\n");
  return 0;
}
