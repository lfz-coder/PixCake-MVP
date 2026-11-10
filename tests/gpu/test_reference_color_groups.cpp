// Reference formulas for the colour sub-groups of color.global (levels, HSL,
// black-and-white mix, selective color, color grading) and detail grain.
// The formulas are the project's provisional ones (render_executor.cpp); these
// vectors pin them, check identity at the registered defaults, and check that
// each group acts only on the pixels it targets.
#include <pixcake/gpu/brightness.hpp>
#include <pixcake/gpu/camera_raw.hpp>
#include <pixcake/gpu/render_executor.hpp>
#include <pixcake/editdoc/edit_document.hpp>
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

namespace cr = pixcake::gpu::camera_raw;
namespace br = pixcake::gpu::brightness;
cr::Rgb unit(int r, int g, int b) { return {r / 255.0F, g / 255.0F, b / 255.0F}; }
Rgb quantize(const cr::Rgb& c) {
  const auto q = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255.0F)); };
  return {q(c[0]), q(c[1]), q(c[2])};
}

// The global tool runs PhotoCraft's Camera Raw stages (white balance and exposure,
// Contrast, Presence, Color Mixer, Color Grading) in Camera Raw's order.
void test_global_uses_camera_raw() {
  cr::Settings s;
  s.temperature = 60;
  s.tint = -20;
  s.exposure = 0.7F;
  s.contrast = 40;
  s.vibrance = 30;
  s.saturation = -10;
  s.hsl_hue[1] = 60;
  s.hsl_sat[1] = -50;
  s.hsl_lum[1] = 40;
  s.grade[0] = {220, 60, -30};
  s.grade[1] = {40, 50, 20};
  s.grade[2] = {50, 70, 30};
  s.grade_blending = 70;
  const auto description = global({value("temperature", 60), value("tint", -20), value("exposure", 0.7),
                                   value("contrast", 40), value("vibrance", 30), value("saturation", -10),
                                   value("hsl_hue_orange", 60), value("hsl_saturation_orange", -50),
                                   value("hsl_luminance_orange", 40), value("grading_shadows_hue", 220),
                                   value("grading_shadows_saturation", 60), value("grading_shadows_luminance", -30),
                                   value("grading_midtones_hue", 40), value("grading_midtones_saturation", 50),
                                   value("grading_midtones_luminance", 20), value("grading_highlights_hue", 50),
                                   value("grading_highlights_saturation", 70),
                                   value("grading_highlights_luminance", 30), value("grading_blending", 70)});
  for (const auto& px : {std::array<int, 3>{200, 120, 40}, std::array<int, 3>{30, 60, 90},
                         std::array<int, 3>{240, 230, 220}}) {
    const auto want = quantize(cr::developPixel(unit(px[0], px[1], px[2]), s));
    assert(near(render(description, image(static_cast<std::uint8_t>(px[0]), static_cast<std::uint8_t>(px[1]),
                                          static_cast<std::uint8_t>(px[2]))),
                want, 0));
  }
}

// Photoshop's modern brightness follows Camera Raw's white balance and exposure.
void test_brightness_after_balance() {
  const auto lut = br::modernLut(-40);
  const cr::Rgb balanced = cr::applyBalance(unit(90, 140, 210), cr::balanceGains(0, 0, 0.5F));
  const cr::Rgb want{br::sampleLut(lut, balanced[0]), br::sampleLut(lut, balanced[1]), br::sampleLut(lut, balanced[2])};
  assert(near(render(global({value("exposure", 0.5), value("brightness", -40)}), image(90, 140, 210)), quantize(want), 0));
  // The same through the general per-pixel path (a neutral HSL value opts out of the table).
  assert(near(render(global({value("exposure", 0.5), value("brightness", -40), value("hsl_hue_red", 0)}),
                     image(90, 140, 210)),
              quantize(want), 0));
}

// In the black-and-white profile the Color Mixer does not run.
void test_monochrome_skips_color_mixer() {
  const auto mono = render(global({choice("profile", "monochrome", 2)}), image(200, 60, 40));
  assert(near(render(global({choice("profile", "monochrome", 2), value("hsl_hue_red", 100)}), image(200, 60, 40)),
              mono, 0));
}

// Local colour runs the same Camera Raw stages.
void test_local_uses_camera_raw() {
  const auto description = op("color.local", "color", "pixcake.color.local", {value("exposure", 1), value("contrast", 50)});
  const cr::Rgb want = cr::applyContrast(cr::applyBalance(unit(90, 140, 210), cr::balanceGains(0, 0, 1)), 50);
  assert(near(render(description, image(90, 140, 210)), quantize(want), 0));
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

void test_scalar_tone_fast_path_matches_general_reference() {
  const auto input = image(17, 93, 221, 8);
  const auto fast = global({value("exposure", 0.65), value("brightness", -12), value("contrast", 37)});
  // A neutral HSL value keeps the pixels unchanged but deliberately takes the
  // general path, giving us an in-process equivalence oracle.
  const auto general = global({value("exposure", 0.65), value("brightness", -12), value("contrast", 37),
                               value("hsl_hue_red", 0)});
  RenderResult fast_result;
  RenderResult general_result;
  assert(RenderExecutor{}.renderReference(fast, input, RenderInputs{}, fast_result));
  assert(RenderExecutor{}.renderReference(general, input, RenderInputs{}, general_result));
  assert(fast_result.image.pixels == general_result.image.pixels);

  const auto fast_levels = global({value("levels_rgb_input_black", 16), value("levels_rgb_input_white", 240),
                                   value("levels_rgb_input_gray", 0.25), value("levels_rgb_output_black", 8),
                                   value("levels_rgb_output_white", 248)});
  const auto general_levels = global({value("levels_rgb_input_black", 16), value("levels_rgb_input_white", 240),
                                      value("levels_rgb_input_gray", 0.25), value("levels_rgb_output_black", 8),
                                      value("levels_rgb_output_white", 248), value("hsl_hue_red", 0)});
  assert(RenderExecutor{}.renderReference(fast_levels, input, RenderInputs{}, fast_result));
  assert(RenderExecutor{}.renderReference(general_levels, input, RenderInputs{}, general_result));
  assert(fast_result.image.pixels == general_result.image.pixels);

  // White balance keeps the general path so the fast path remains limited to
  // channel-independent tone and RGB levels; the neutral HSL value still
  // gives us a byte-for-byte generic-path oracle.
  const auto fast_warm = global({value("temperature", 68), value("exposure", 0.2)});
  const auto general_warm = global({value("temperature", 68), value("exposure", 0.2),
                                    value("hsl_hue_red", 0)});
  assert(RenderExecutor{}.renderReference(fast_warm, input, RenderInputs{}, fast_result));
  assert(RenderExecutor{}.renderReference(general_warm, input, RenderInputs{}, general_result));
  assert(fast_result.image.pixels == general_result.image.pixels);

  // Partially transparent pixels: the general path composites them with their
  // alpha, so the fast path must give the same bytes there too.
  auto translucent = input;
  for (std::size_t i = 3; i < translucent.pixels.size(); i += 4) {
    translucent.pixels[i] = static_cast<std::uint8_t>(32 + (i / 4) * 3 % 224);
  }
  assert(RenderExecutor{}.renderReference(fast, translucent, RenderInputs{}, fast_result));
  assert(RenderExecutor{}.renderReference(general, translucent, RenderInputs{}, general_result));
  assert(fast_result.image.pixels == general_result.image.pixels);
  assert(RenderExecutor{}.renderReference(fast_levels, translucent, RenderInputs{}, fast_result));
  assert(RenderExecutor{}.renderReference(general_levels, translucent, RenderInputs{}, general_result));
  assert(fast_result.image.pixels == general_result.image.pixels);

  // Brightness alone, and exposure + brightness + levels, opaque and translucent.
  const auto fast_bright = global({value("brightness", 35)});
  const auto general_bright = global({value("brightness", 35), value("hsl_hue_red", 0)});
  const auto fast_all = global({value("exposure", -0.4), value("brightness", -55), value("levels_rgb_input_black", 12),
                                value("levels_rgb_input_gray", -0.2), value("levels_rgb_output_white", 230)});
  const auto general_all = global({value("exposure", -0.4), value("brightness", -55),
                                   value("levels_rgb_input_black", 12), value("levels_rgb_input_gray", -0.2),
                                   value("levels_rgb_output_white", 230), value("hsl_hue_red", 0)});
  for (const RgbaImage* frame : {&input, static_cast<const RgbaImage*>(&translucent)}) {
    assert(RenderExecutor{}.renderReference(fast_bright, *frame, RenderInputs{}, fast_result));
    assert(RenderExecutor{}.renderReference(general_bright, *frame, RenderInputs{}, general_result));
    assert(fast_result.image.pixels == general_result.image.pixels);
    assert(RenderExecutor{}.renderReference(fast_all, *frame, RenderInputs{}, fast_result));
    assert(RenderExecutor{}.renderReference(general_all, *frame, RenderInputs{}, general_result));
    assert(fast_result.image.pixels == general_result.image.pixels);
  }
}

// Camera Raw's Color Mixer: hue turns up to 30 degrees, saturation scales HSV saturation,
// and both keep the pixel's luminance; luminance is a gain weighted by saturation.
Rgb developed(const cr::Settings& s, int r, int g, int b) { return quantize(cr::developPixel(unit(r, g, b), s)); }
void test_hsl() {
  cr::Settings turn;
  turn.hsl_hue[0] = 100;
  // Pure red turns towards orange at red's luminance (0.2126): darker, green rises, no blue.
  const Rgb orange = render(global({value("hsl_hue_red", 100)}), image(255, 0, 0));
  assert(near(orange, developed(turn, 255, 0, 0), 0));
  assert(orange.g > 0 && orange.b == 0 && orange.r > orange.g);
  assert(near(render(global({value("hsl_hue_red", 100)}), image(0, 0, 255)), {0, 0, 255}, 0));
  // Red saturation -100 greys it at its own luminance.
  cr::Settings grey;
  grey.hsl_sat[0] = -100;
  const Rgb greyed = render(global({value("hsl_saturation_red", -100)}), image(200, 50, 50));
  assert(near(greyed, developed(grey, 200, 50, 50), 0));
  assert(greyed.r == greyed.g && greyed.g == greyed.b);
  // Greys have no hue: no band moves them.
  assert(near(render(global({value("hsl_luminance_red", 100), value("hsl_saturation_aqua", 100)}),
                     image(128, 128, 128)),
              {128, 128, 128}, 0));
  // Red luminance +100 brightens a red.
  cr::Settings bright;
  bright.hsl_lum[0] = 100;
  const Rgb brighter = render(global({value("hsl_luminance_red", 100)}), image(200, 50, 50));
  assert(near(brighter, developed(bright, 200, 50, 50), 0));
  assert(brighter.r > 200 && brighter.g > 50 && brighter.b > 50);
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

// Camera Raw's Color Grading: wheels add a tint of zero luminance, luminance is a gain.
void test_grading() {
  // A red global tint on grey: red up, green and blue down, luminance kept.
  cr::Settings red;
  red.grade[3] = {0, 100, 0};
  const Rgb tinted =
      render(global({value("grading_global_hue", 0), value("grading_global_saturation", 100)}), image(128, 128, 128));
  assert(near(tinted, developed(red, 128, 128, 128), 0));
  assert(tinted.r > 160 && tinted.g < 128 && tinted.b < 128);
  const double luma = 0.2126 * tinted.r + 0.7152 * tinted.g + 0.0722 * tinted.b;
  assert(luma > 126 && luma < 130);
  // Shadows luminance +100 brightens a dark grey, leaves white and black alone.
  cr::Settings lift;
  lift.grade[0] = {0, 0, 100};
  const Rgb dark = render(global({value("grading_shadows_luminance", 100)}), image(40, 40, 40));
  assert(near(dark, developed(lift, 40, 40, 40), 0));
  assert(dark.r > 50);  // 40 * 2^(0.6 * shadow mask ~0.9) ~ 58
  assert(near(render(global({value("grading_shadows_luminance", 100)}), image(255, 255, 255)), {255, 255, 255}, 0));
  assert(near(render(global({value("grading_shadows_luminance", 100)}), image(0, 0, 0)), {0, 0, 0}, 0));
  // Blending widens the zones: mid grey moves less at blending 0 than at 100.
  const Rgb narrow = render(global({value("grading_shadows_luminance", 100), value("grading_blending", 0)}),
                            image(128, 128, 128));
  const Rgb wide = render(global({value("grading_shadows_luminance", 100), value("grading_blending", 100)}),
                          image(128, 128, 128));
  assert(narrow.r > 128 && wide.r > narrow.r);
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
  test_global_uses_camera_raw();
  test_brightness_after_balance();
  test_monochrome_skips_color_mixer();
  test_local_uses_camera_raw();
  test_identity_at_defaults();
  test_levels();
  test_scalar_tone_fast_path_matches_general_reference();
  test_hsl();
  test_monochrome();
  test_selective_color();
  test_grading();
  test_color_editor_is_reported_not_drawn();
  test_grain();
  std::printf("reference colour groups ok\n");
  return 0;
}
