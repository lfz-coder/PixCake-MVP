#include "pixcake/gpu/render_executor.hpp"

#include "pixcake/capability/capability_registry.hpp"
#include "pixcake/capability/curve_points.hpp"
#include "pixcake/gpu/adjust.hpp"
#include "pixcake/gpu/brightness.hpp"
#include "pixcake/gpu/camera_raw.hpp"
#include "pixcake/gpu/camera_raw_gl.hpp"
#include "pixcake/gpu/camera_raw_spatial.hpp"
#include "shader_sources.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

namespace pixcake::gpu {
namespace {

bool known_blend_mode(const std::string& blend_mode) {
  return blend_mode == "normal" || blend_mode == "multiply" || blend_mode == "screen" ||
         blend_mode == "lighten" || blend_mode == "darken";
}

struct Pixel { float r, g, b, a; };
float clamp01(float value) { return std::clamp(value, 0.0F, 1.0F); }
float get(const std::map<std::string, float>& values, const char* key) {
  const auto it = values.find(key);
  return it == values.end() ? 0.0F : it->second;
}
float luminance(Pixel p) { return 0.2126F * p.r + 0.7152F * p.g + 0.0722F * p.b; }
Pixel mix(Pixel a, Pixel b, float amount) {
  amount = clamp01(amount);
  return {a.r + (b.r - a.r) * amount, a.g + (b.g - a.g) * amount,
          a.b + (b.b - a.b) * amount, a.a + (b.a - a.a) * amount};
}
Pixel read(const RgbaImage& image, std::uint32_t x, std::uint32_t y) {
  const auto stride = image.row_stride == 0 ? image.width * 4 : image.row_stride;
  const auto i = static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 4;
  return {image.pixels[i] / 255.0F, image.pixels[i + 1] / 255.0F,
          image.pixels[i + 2] / 255.0F, image.pixels[i + 3] / 255.0F};
}
void write(RgbaImage& image, std::uint32_t x, std::uint32_t y, Pixel p) {
  const auto stride = image.row_stride == 0 ? image.width * 4 : image.row_stride;
  const auto i = static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 4;
  image.pixels[i] = static_cast<std::uint8_t>(std::lround(clamp01(p.r) * 255.0F));
  image.pixels[i + 1] = static_cast<std::uint8_t>(std::lround(clamp01(p.g) * 255.0F));
  image.pixels[i + 2] = static_cast<std::uint8_t>(std::lround(clamp01(p.b) * 255.0F));
  image.pixels[i + 3] = static_cast<std::uint8_t>(std::lround(clamp01(p.a) * 255.0F));
}
namespace cr = pixcake::gpu::camera_raw;
namespace br = pixcake::gpu::brightness;
namespace adj = pixcake::gpu::adjust;

cr::Rgb rgb_of(const Pixel& p) { return {p.r, p.g, p.b}; }
Pixel with_rgb(Pixel p, const cr::Rgb& c) {
  p.r = c[0];
  p.g = c[1];
  p.b = c[2];
  return p;
}

// White balance, exposure, style tint and tone, read once per operation.
//
// The frame's values are the decoder's sRGB-encoded bytes / 255, which is the
// display encoding PhotoCraft's Camera Raw develop takes (encoding_gamma 0); the
// executor does not linearise them.  Per pixel, in this order:
//   1. Camera Raw stage 1: temperature, tint, exposure (PhotoCraft port)
//   2. AI style tint (amount; PixCake's provisional offset)
//   3. Photoshop modern brightness (PhotoCraft port; placed after stage 1 by the
//      2026-11-09 user decision), continued past 0..1 as PhotoCraft's lut_extended
//   4. Camera Raw stage 3, Contrast; 5. stage 4, Vibrance / Saturation
// then the global tool's groups (apply_color_groups).  An operation with any of
// Camera Raw's whole-frame controls (dehaze, highlights, shadows, texture,
// clarity, whites, blacks) runs them between steps 3 and 5 in develop's order
// instead (draw_spatial; camera_raw_spatial.cpp), on the reference path only.
struct ToneColor {
  bool balance = false;
  cr::Rgb gains{1.0F, 1.0F, 1.0F};
  bool calibration = false;  // Camera Raw stage 1b, red/green/blue primaries
  std::array<float, 3> calibration_hue{}, calibration_sat{};
  float style = 0;
  bool brightness_on = false;
  std::vector<float> brightness_lut;
  std::array<float, 2> brightness_slopes{1.0F, 1.0F};  // past 0 and past 1 (lut_extended)
  float brightness = 0;  // slider -100..100 (the GPU evaluates the curve from it)
  float contrast = 0, vibrance = 0, saturation = 0;  // Camera Raw sliders, -100..100
  // Camera Raw's whole-frame sliders, -100..100.
  float dehaze = 0, highlights = 0, shadows = 0, texture = 0, clarity = 0, whites = 0, blacks = 0;
  bool spatial = false;
};
ToneColor tone_color_of(const std::map<std::string, float>& v) {
  ToneColor t;
  const float temperature = get(v, "temperature");
  const float tint = get(v, "tint");
  const float exposure = get(v, "exposure");
  t.balance = temperature != 0.0F || tint != 0.0F || exposure != 0.0F;
  if (t.balance) t.gains = cr::balanceGains(temperature, tint, exposure);
  const char* primaries[3] = {"red", "green", "blue"};
  for (int i = 0; i < 3; ++i) {
    t.calibration_hue[i] = get(v, (std::string("calibration_") + primaries[i] + "_hue").c_str());
    t.calibration_sat[i] = get(v, (std::string("calibration_") + primaries[i] + "_saturation").c_str());
  }
  t.calibration = !cr::calibrationNeutral(t.calibration_hue, t.calibration_sat);
  // 程度 100 是默认强度：AI 色彩风格 0..100，AI 追色 0..200（200 为两倍强度），与参数模式一致。
  t.style = std::clamp(get(v, "amount") / 100.0F, 0.0F, 2.0F);
  t.brightness = get(v, "brightness");
  t.brightness_on = t.brightness != 0.0F;
  if (t.brightness_on) {
    t.brightness_lut = br::modernLut(t.brightness);
    t.brightness_slopes = br::endSlopes(t.brightness_lut);
  }
  t.dehaze = get(v, "dehaze");
  t.highlights = get(v, "highlights");
  t.shadows = get(v, "shadows");
  t.texture = get(v, "texture");
  t.clarity = get(v, "clarity");
  t.whites = get(v, "whites");
  t.blacks = get(v, "blacks");
  t.spatial = t.dehaze != 0.0F || t.highlights != 0.0F || t.shadows != 0.0F || t.texture != 0.0F ||
              t.clarity != 0.0F || t.whites != 0.0F || t.blacks != 0.0F;
  t.contrast = get(v, "contrast");
  t.saturation = get(v, "saturation");
  t.vibrance = get(v, "vibrance");
  return t;
}
bool color_is_active(const ToneColor& t) {
  return t.balance || t.calibration || std::abs(t.style) > 1e-7F;
}
bool tone_is_active(const ToneColor& t) {
  return t.brightness_on || t.spatial || t.contrast != 0.0F || t.vibrance != 0.0F || t.saturation != 0.0F;
}
// Steps 1 and 2.
Pixel color(Pixel p, const ToneColor& t) {
  if (t.balance) p = with_rgb(p, cr::applyBalance(rgb_of(p), t.gains));
  if (t.calibration) p = with_rgb(p, cr::applyCalibration(rgb_of(p), t.calibration_hue, t.calibration_sat));
  p.r += t.style * 0.04F; p.g += t.style * 0.01F; p.b -= t.style * 0.03F;
  return p;
}
// Step 3, brightness.
Pixel tone_brightness(Pixel p, const ToneColor& t) {
  if (t.brightness_on) {
    p.r = br::sampleLutExtended(t.brightness_lut, p.r);
    p.g = br::sampleLutExtended(t.brightness_lut, p.g);
    p.b = br::sampleLutExtended(t.brightness_lut, p.b);
  }
  return p;
}
// Step 5, presence.
Pixel tone_presence(Pixel p, const ToneColor& t) {
  if (t.vibrance != 0.0F || t.saturation != 0.0F) p = with_rgb(p, cr::applyPresence(rgb_of(p), t.vibrance, t.saturation));
  return p;
}
// Steps 3 to 5 for an operation without whole-frame controls; a step whose controls are at
// rest leaves the pixel untouched.
Pixel tone(Pixel p, const ToneColor& t) {
  p = tone_brightness(p, t);
  if (t.contrast != 0.0F) p = with_rgb(p, cr::applyContrast(rgb_of(p), t.contrast));
  return tone_presence(p, t);
}
// ---- color.global sub-groups and detail grain (requirement 10.3) -----------
// 像素蛋糕's own formulas are not published.  These are the project's
// provisional reference formulas, each written down with its rule; they are
// the identity at the registered defaults, so documents that do not touch a
// group render exactly as before.  Absent values take the registered default
// (ParameterMode is the single source of defaults, render contract §2).
using Values = std::map<std::string, float>;

float param(const Values& values, const char* capability, const std::string& id) {
  const auto found = values.find(id);
  if (found != values.end()) return found->second;
  const auto* mode = pixcake::find_parameter_mode(capability, id);
  if (mode == nullptr) return 0.0F;
  if (mode->choices.empty()) return static_cast<float>(mode->default_value);
  const auto choice = std::find(mode->choices.begin(), mode->choices.end(), mode->default_choice);
  return static_cast<float>(choice - mode->choices.begin());
}
// Choice parameters arrive as their index in the parameter mode's choices.
bool choice_is(const Values& values, const char* capability, const std::string& id, std::string_view choice) {
  const auto* mode = pixcake::find_parameter_mode(capability, id);
  if (mode == nullptr) return false;
  const auto found = std::find(mode->choices.begin(), mode->choices.end(), choice);
  return found != mode->choices.end() &&
         std::lround(param(values, capability, id)) == static_cast<long>(found - mode->choices.begin());
}

struct Hsl { float h, s, l; };  // h in degrees [0, 360)
Hsl to_hsl(Pixel p) {
  const float high = std::max({p.r, p.g, p.b}), low = std::min({p.r, p.g, p.b});
  Hsl c{0.0F, 0.0F, (high + low) / 2.0F};
  const float d = high - low;
  if (d < 1e-6F) return c;
  c.s = c.l > 0.5F ? d / (2.0F - high - low) : d / (high + low);
  if (high == p.r) {
    c.h = (p.g - p.b) / d + (p.g < p.b ? 6.0F : 0.0F);
  } else if (high == p.g) {
    c.h = (p.b - p.r) / d + 2.0F;
  } else {
    c.h = (p.r - p.g) / d + 4.0F;
  }
  c.h *= 60.0F;
  return c;
}
// The eight hue bands of the black-and-white mix (PixCake legacy-provisional;
// HSL uses Camera Raw's Color Mixer weights in camera_raw.cpp), centred on these
// hues.  A hue belongs to its two neighbouring bands with linear weights, so
// the weights always sum to 1 (no band edge shows as a seam).
constexpr float kBandHue[8] = {0.0F, 30.0F, 60.0F, 120.0F, 180.0F, 240.0F, 270.0F, 300.0F};
constexpr const char* kBandName[8] = {"red", "orange", "yellow", "green", "aqua", "blue", "purple", "magenta"};
int band_index(float hue) {
  if (!(hue >= 0.0F && hue < 360.0F)) return -1;
  if (hue < 30.0F) return 0;
  if (hue < 60.0F) return 1;
  if (hue < 120.0F) return 2;
  if (hue < 180.0F) return 3;
  if (hue < 240.0F) return 4;
  if (hue < 270.0F) return 5;
  if (hue < 300.0F) return 6;
  return 7;
}
float band_value(const float values[8], float hue) {
  const int index = band_index(hue);
  if (index < 0) return values[0];
  const float start = kBandHue[index], end = index == 7 ? 360.0F : kBandHue[index + 1];
  const float t = (hue - start) / (end - start);
  return (1.0F - t) * values[index] + t * values[(index + 1) % 8];
}

struct ColorGroups {
  bool levels = false;
  std::vector<float> levels_lut;  // PhotoCraft Levels, master = PixCake's RGB composite, 8-bit
  bool curves = false;  // Camera Raw stage 5, the tone curves
  cr::Curves curve_tables;
  float in_black = 0, in_white = 1, gamma = 1, out_black = 0, out_white = 1;
  bool hsl = false;  // Camera Raw stage 6, Color Mixer (slider units)
  std::array<float, 8> hue{}, saturation{}, lightness{};
  bool monochrome = false;
  float mix[8]{};
  bool selective = false, absolute = false;
  std::array<std::array<float, 4>, 9> selective_percent{};  // PhotoCraft selective_color amounts
  float selective_ink[9][4]{};  // target x (cyan, magenta, yellow, black), -1..1
  std::array<std::uint8_t, 9> selective_targets{};
  std::size_t selective_target_count = 0;
  bool grading = false;  // Camera Raw stage 7, Color Grading
  cr::Grade grade;
  bool editor_offsets = false;
};

ColorGroups color_groups(const Values& v, const pixcake::render::RenderOperation& operation) {
  const char* cap = "color.global";
  ColorGroups g;
  // Camera Raw's tone curves: the parametric sliders, and point curves as their text (a text
  // that is not a valid curve leaves that curve linear; the capability layer disables the step).
  cr::CurveSettings curves;
  curves.highlights = param(v, cap, "curve_highlights");
  curves.lights = param(v, cap, "curve_lights");
  curves.darks = param(v, cap, "curve_darks");
  curves.shadows = param(v, cap, "curve_shadows");
  const char* point_channels[4] = {"curve_points_rgb", "curve_points_red", "curve_points_green", "curve_points_blue"};
  for (const auto& value : operation.values) {
    for (int k = 0; k < 4; ++k) {
      if (value.channel != point_channels[k]) continue;
      std::vector<std::array<float, 2>> points;
      if (pixcake::decode_curve_points(value.display_value, points)) curves.points[k] = std::move(points);
    }
  }
  g.curves = !cr::curvesNeutral(curves);
  if (g.curves) g.curve_tables = cr::resolveCurves(curves);
  // Levels (RGB composite).  Input black/white stretch, output black/white
  // compress, both in 0..255.  The grey box is -1..1 in 像素蛋糕; the project
  // maps it to a midtone gamma 10^grey (0.1..10, 0 = unchanged) with positive
  // values brightening the midtones -- provisional, the official mapping is
  // not published.
  g.in_black = param(v, cap, "levels_rgb_input_black") / 255.0F;
  g.in_white = param(v, cap, "levels_rgb_input_white") / 255.0F;
  g.gamma = std::pow(10.0F, param(v, cap, "levels_rgb_input_gray"));
  g.out_black = param(v, cap, "levels_rgb_output_black") / 255.0F;
  g.out_white = param(v, cap, "levels_rgb_output_white") / 255.0F;
  g.levels = g.in_black != 0.0F || g.in_white != 1.0F || g.gamma != 1.0F || g.out_black != 0.0F || g.out_white != 1.0F;
  if (g.levels) g.levels_lut = adj::levelsLut({g.in_black, g.in_white, g.gamma, g.out_black, g.out_white}, 255.0F);
  for (int i = 0; i < 8; ++i) {
    const std::string band = kBandName[i];
    g.hue[i] = param(v, cap, "hsl_hue_" + band);
    g.saturation[i] = param(v, cap, "hsl_saturation_" + band);
    g.lightness[i] = param(v, cap, "hsl_luminance_" + band);
    g.mix[i] = param(v, cap, "bw_mix_" + band) / 100.0F;
  }
  g.hsl = !cr::colorMixerNeutral(g.hue, g.saturation, g.lightness);
  g.monochrome = choice_is(v, cap, "profile", "monochrome");
  const char* targets[9] = {"red", "yellow", "green", "cyan", "blue", "magenta", "white", "neutral", "black"};
  const char* inks[4] = {"cyan", "magenta", "yellow", "black"};
  for (int t = 0; t < 9; ++t) {
    bool target_active = false;
    for (int k = 0; k < 4; ++k) {
      g.selective_percent[t][k] = param(v, cap, std::string("selective_") + targets[t] + "_" + inks[k]);
      g.selective_ink[t][k] = g.selective_percent[t][k] / 100.0F;
      g.selective = g.selective || g.selective_ink[t][k] != 0.0F;
      target_active = target_active || g.selective_ink[t][k] != 0.0F;
    }
    if (target_active) g.selective_targets[g.selective_target_count++] = static_cast<std::uint8_t>(t);
  }
  g.absolute = choice_is(v, cap, "selective_mode", "absolute");
  // Camera Raw units: hue 0..360, saturation 0..100, luminance -100..100, blending
  // 0..100.  Balance is not registered in PixCake's parameter mode, so it stays 0.
  const char* zones[4] = {"shadows", "midtones", "highlights", "global"};
  std::array<cr::Wheel, 4> wheels{};
  for (int z = 0; z < 4; ++z) {
    const std::string zone = zones[z];
    wheels[z] = {param(v, cap, "grading_" + zone + "_hue"), param(v, cap, "grading_" + zone + "_saturation"),
                 param(v, cap, "grading_" + zone + "_luminance")};
  }
  g.grading = !cr::gradeNeutral(wheels);
  if (g.grading) g.grade = cr::resolveGrade(wheels, param(v, cap, "grading_blending"), 0.0F);
  for (const char* item : {"hue", "saturation", "luminance"}) {
    g.editor_offsets = g.editor_offsets || param(v, cap, std::string("color_editor_") + item + "_shift") != 0.0F;
  }
  return g;
}

// Levels: PhotoCraft's Photoshop Levels table (adjust.cpp), sampled like PhotoCraft's lut().
Pixel apply_levels(Pixel p, const ColorGroups& g) {
  return {adj::sampleLut(g.levels_lut, p.r), adj::sampleLut(g.levels_lut, p.g), adj::sampleLut(g.levels_lut, p.b), p.a};
}

// HSL: PhotoCraft's Camera Raw Color Mixer (camera_raw.cpp).
Pixel apply_hsl(Pixel p, const ColorGroups& g) {
  return with_rgb(p, cr::applyColorMixer(rgb_of(p), g.hue, g.saturation, g.lightness));
}

// Black-and-white profile: Rec. 709 luminance (the same weights as
// luminance()), then each band's mix slider brightens or darkens pixels of
// that hue in proportion to their chroma, so neutral pixels keep their grey.
Pixel apply_monochrome(Pixel p, const ColorGroups& g) {
  float grey = luminance(p);
  const float chroma = std::max({p.r, p.g, p.b}) - std::min({p.r, p.g, p.b});
  if (chroma > 0.0F) {
    const float mix_amount = band_value(g.mix, to_hsl(p).h);
    grey = clamp01(grey + mix_amount * chroma * 0.5F);
  }
  return {grey, grey, grey, p.a};
}

// Selective color: PhotoCraft's Photoshop Selective Color (adjust.cpp).
Pixel apply_selective(Pixel p, const ColorGroups& g) {
  return with_rgb(p, adj::selectiveColor(rgb_of(p), !g.absolute, g.selective_percent));
}

// Color grading: PhotoCraft's Camera Raw Color Grading (camera_raw.cpp).
Pixel apply_grading(Pixel p, const ColorGroups& g) { return with_rgb(p, cr::applyColorGrading(rgb_of(p), g.grade)); }

Pixel apply_color_groups(Pixel p, const ColorGroups& g) {
  if (g.levels) p = apply_levels(p, g);
  // After Presence (tone) and levels, before the Color Mixer, as Camera Raw orders stages 4-6.
  if (g.curves) p = with_rgb(p, cr::applyCurves(rgb_of(p), g.curve_tables));
  if (g.monochrome) {
    p = apply_monochrome(p, g);
  } else if (g.hsl) {
    p = apply_hsl(p, g);
  }
  if (g.selective) p = apply_selective(p, g);
  if (g.grading) p = apply_grading(p, g);
  return p;
}

// Grain (detail): PhotoCraft's Camera Raw stage 9 (camera_raw.cpp) for amount, size and
// roughness, sampled in full-resolution coordinates (RenderInputs::pixel_scale).  PixCake's
// own sub-sliders on top (PixCake-specific): shadows / midtones / highlights scale the strength
// by their triangular zones relative to the defaults 30 / 50 / 15, and colour mixes in an
// independent noise per channel; at the default zones with colour 0 (its default is 10) the
// grain is PhotoCraft's exactly.
struct Grain {
  bool on = false;
  cr::Grain base;
  std::array<cr::Grain, 3> channel{};  // the same grain, seeds 1..3 (colour)
  float shadows = 0, midtones = 0, highlights = 0, colour = 0;
};
constexpr float kGrainZoneDefault[3] = {0.30F, 0.50F, 0.15F};
Grain grain_of(const Values& v, float pixel_scale) {
  const char* cap = "detail";
  Grain g;
  const float amount = param(v, cap, "grain_amount");
  g.on = amount > 0.0F;
  g.base = cr::grainOf(amount, param(v, cap, "grain_size"), param(v, cap, "grain_roughness"), 0, pixel_scale);
  for (std::uint64_t c = 0; c < 3; ++c) {
    g.channel[c] = g.base;
    g.channel[c].seed = c + 1;
  }
  g.shadows = param(v, cap, "grain_shadows") / 100.0F;
  g.midtones = param(v, cap, "grain_midtones") / 100.0F;
  g.highlights = param(v, cap, "grain_highlights") / 100.0F;
  g.colour = param(v, cap, "grain_color") / 100.0F;
  return g;
}
// Camera Raw stage 8, sharpening and noise reduction (Camera Raw units; defaults from the
// parameter modes).
cr::DetailSettings detail_of(const Values& v) {
  const char* cap = "detail";
  cr::DetailSettings s;
  s.sharpen_amount = param(v, cap, "sharpen_amount");
  s.sharpen_radius = param(v, cap, "sharpen_radius");
  s.sharpen_detail = param(v, cap, "sharpen_detail");
  s.sharpen_masking = 0.0F;  // not on the official panel, not registered
  s.noise_luminance = param(v, cap, "noise_luminance");
  s.noise_luminance_detail = param(v, cap, "noise_luminance_detail");
  s.noise_color = param(v, cap, "noise_color");
  s.noise_color_detail = param(v, cap, "noise_color_detail");
  return s;
}
float grain_zone(float l, float shadows, float midtones, float highlights) {
  return std::max(0.0F, 1.0F - l / 0.5F) * shadows + std::max(0.0F, 1.0F - std::abs(l - 0.5F) / 0.5F) * midtones +
         std::max(0.0F, 1.0F - (1.0F - l) / 0.5F) * highlights;
}
Pixel apply_grain(Pixel p, std::uint32_t x, std::uint32_t y, const Grain& g) {
  const cr::Rgb c = rgb_of(p);
  const float l = std::clamp(cr::luma(c), 0.0F, 1.0F);
  const float zone = grain_zone(l, g.shadows, g.midtones, g.highlights) /
                     grain_zone(l, kGrainZoneDefault[0], kGrainZoneDefault[1], kGrainZoneDefault[2]);
  if (!(zone > 0.0F)) return p;
  const float fx = static_cast<float>(x), fy = static_cast<float>(y);
  const float w = cr::grainWeight(c) * zone;
  const float mono = cr::grainNoise(g.base, fx, fy);
  float n[3] = {mono, mono, mono};
  if (g.colour > 0.0F) {
    for (int k = 0; k < 3; ++k) n[k] = mono + (cr::grainNoise(g.channel[k], fx, fy) - mono) * g.colour;
  }
  p.r += (n[0] - 0.5F) * 2.0F * g.base.amount * w;
  p.g += (n[1] - 0.5F) * 2.0F * g.base.amount * w;
  p.b += (n[2] - 0.5F) * 2.0F * g.base.amount * w;
  return p;
}

// The values are the shaders' blend0.y / layer0.z codes.
enum class Blend { Normal = 0, Multiply = 1, Screen = 2 };
Blend blend_of(const std::string& mode) {
  if (mode == "multiply") return Blend::Multiply;
  if (mode == "screen") return Blend::Screen;
  return Blend::Normal;
}
adj::BlendMode blend_mode_of(Blend mode) {
  return mode == Blend::Multiply ? adj::BlendMode::Multiply : mode == Blend::Screen ? adj::BlendMode::Screen : adj::BlendMode::Normal;
}
// An operation's result back into the pixel it was made from, by coverage (PhotoCraft blend_back).
Pixel blend_back(Pixel before, Pixel after, float coverage, Blend mode) {
  const auto out = adj::blendBack(blend_mode_of(mode), {before.r, before.g, before.b, before.a}, rgb_of(after), clamp01(coverage));
  return {out[0], out[1], out[2], out[3]};
}
// A layer over the frame (PhotoCraft psblend::composite, straight alpha).
Pixel composite_layer(Pixel base, Pixel top, float opacity, Blend mode) {
  const auto out = adj::composite(blend_mode_of(mode), {base.r, base.g, base.b, base.a}, {top.r, top.g, top.b, top.a}, clamp01(opacity));
  return {out[0], out[1], out[2], out[3]};
}

// Runs body(first_row, end_row) over the frame's rows, split across the
// machine's cores when the frame is large.  Every pixel is read and written
// only by the row that owns it, so the split does not change the result.
template <typename Body>
void for_rows(std::uint32_t width, std::uint32_t height, const Body& body) {
  const std::uint64_t pixels = static_cast<std::uint64_t>(width) * height;
  const unsigned cores = std::max(1U, std::thread::hardware_concurrency());
  const unsigned workers = pixels < 262144U ? 1U : std::min<unsigned>(cores, height);
  if (workers <= 1) {
    body(0U, height);
    return;
  }
  std::vector<std::thread> threads;
  threads.reserve(workers - 1);
  const std::uint32_t band = (height + workers - 1) / workers;
  for (unsigned w = 1; w < workers; ++w) {
    const std::uint32_t first = std::min(height, band * w);
    const std::uint32_t end = std::min(height, first + band);
    if (first < end) threads.emplace_back([&body, first, end] { body(first, end); });
  }
  body(0U, std::min(height, band));
  for (auto& thread : threads) thread.join();
}
const RenderMask* mask_for(const RenderInputs& inputs, const std::string& id) {
  for (const auto& mask : inputs.masks) if (mask.id == id) return &mask;
  return nullptr;
}
bool valid_lut(const Lut3DView& lut) {
  if (lut.id.empty() || lut.size < 2 || lut.rgb == nullptr || lut.revision == 0) return false;
  const auto required = static_cast<std::size_t>(lut.size) * lut.size * lut.size * 3;
  if (lut.sample_count < required) return false;
  for (std::size_t i = 0; i < required; ++i) if (!std::isfinite(lut.rgb[i])) return false;
  return true;
}
// PhotoCraft's lut3d_sample (adjust.cpp), trilinear.
Pixel lut_sample(const Lut3DView& lut, Pixel input) {
  return with_rgb(input, adj::lut3dSample(lut.rgb, lut.sample_count, lut.size, rgb_of(input), false));
}

}  // namespace

namespace {

bool has_prefix(std::string_view text, std::string_view prefix) {
  return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

// Channels the GPU shaders draw, each with a readback comparison against the
// reference (test_gpu_backend_C57_C60, test_gpu_shader_path).  A channel
// outside this list falls back to the reference.  The colour editor's offsets
// are drawn by neither path (no target colour yet), so they do not block it.
// Camera Raw's whole-frame controls (dehaze, Highlights/Shadows, Texture/Clarity,
// Whites/Blacks) are drawn by camera_raw_gl.cpp when the frame fits one GPU tile; a larger
// frame falls back to the reference, whole (execute()).
const std::set<std::string_view>& tone_channels() {
  static const std::set<std::string_view> channels = {
      "temperature", "tint",       "exposure", "contrast", "brightness", "vibrance", "saturation",
      "dehaze",      "highlights", "shadows",  "texture",  "clarity",    "whites",   "blacks"};
  return channels;
}
bool gpu_channel(std::string_view capability, std::string_view channel) {
  if (capability == "detail") return has_prefix(channel, "grain_") || has_prefix(channel, "sharpen_") || has_prefix(channel, "noise_");
  if (capability == "color.local" || capability == "color.post") return tone_channels().count(channel) != 0;
  if (capability == "color.style") return channel == "amount";
  if (capability == "color.match") return channel == "amount" || channel == "precise";
  static const std::set<std::string_view> global = {
      "amount", "profile", "selective_mode", "color_editor_hue_shift", "color_editor_saturation_shift",
      "color_editor_luminance_shift"};
  if (tone_channels().count(channel) != 0 || global.count(channel) != 0) return true;
  for (const char* prefix : {"calibration_", "levels_rgb_", "curve_", "hsl_", "bw_mix_", "selective_", "grading_"}) {
    if (has_prefix(channel, prefix)) return true;
  }
  return false;
}

// Masks of one operation fit the four channels of one mask texture.
constexpr std::size_t kGpuMasksPerOperation = 4;


bool gpu_formula_supported(const pixcake::render::RenderOperation& operation) {
  static const std::set<std::pair<std::string_view, std::string_view>> programs = {
      {"color.global", "pixcake.color.global"}, {"color.local", "pixcake.color.local"},
      {"color.post", "pixcake.color.post"},     {"color.match", "pixcake.color.match"},
      {"color.style", "pixcake.color.style"},   {"detail", "pixcake.detail"}};
  if (!operation.enabled || programs.count({operation.capability_id, operation.program}) == 0 ||
      operation.mask_ids.size() > kGpuMasksPerOperation) {
    return false;
  }
  for (const auto& value : operation.values) {
    // A point curve's value is its text (numeric 0), so it always counts.
    const bool set = std::abs(value.numeric_value) > 1e-9 || pixcake::is_curve_points_parameter(value.channel);
    if (!gpu_channel(operation.capability_id, value.channel) && set) return false;
  }
  return true;
}

// One operation that draws, with everything the per-pixel loop needs read
// once: the reference loop and the shaders both start from this.
struct PreparedOperation {
  const pixcake::render::RenderOperation* operation = nullptr;
  bool has_lut = false;
  Lut3DView lut;
  float lut_amount = 1.0F;
  std::vector<const RenderMask*> masks;
  bool is_color = false, is_tone = false, is_detail = false, has_groups = false;
  bool color_active = false, tone_active = false;
  ColorGroups groups;
  Grain grain;
  cr::DetailSettings detail;  // Camera Raw stage 8, before the grain
  bool detail_on = false;
  ToneColor tone_color;
  Blend blend = Blend::Normal;
  float pixel_scale = 1.0F;  // RenderInputs::pixel_scale (texture and grain)
};

// Checks the frame, records every operation's status and diagnostics in
// result (result.image becomes a copy of the input), and lists the
// operations that draw.  False when the frame itself is rejected.
bool prepare(const pixcake::render::RenderDescription& description, const RgbaImage& input,
             const RenderInputs& inputs, RenderResult& result, std::vector<PreparedOperation>& draws,
             bool copy_pixels = true) {
  result = {};
  draws.clear();
  const auto stride = input.row_stride == 0 ? input.width * 4 : input.row_stride;
  if (input.width == 0 || input.height == 0 || stride < input.width * 4 ||
      input.pixels.size() < static_cast<std::size_t>(stride) * input.height) {
    result.diagnostics.push_back({"", "输入 RGBA 帧为空或行距无效", true});
    return false;
  }
  if (input.revision != 0 && input.revision != description.document_revision) {
    result.diagnostics.push_back({"", "输入帧修订号与绘制描述不一致", true});
    result.statuses.push_back({"", "frame", PassStatus::InvalidInput, "输入帧修订号与绘制描述不一致"});
    return false;
  }
  if (copy_pixels) {
    result.image = input;
  } else {  // the GPU path reads its pixels back; skip copying a frame it overwrites
    result.image.width = input.width;
    result.image.height = input.height;
  }
  result.image.row_stride = stride;
  result.image.revision = description.document_revision;
  for (const auto& diagnostic : description.diagnostics) {
    result.diagnostics.push_back({diagnostic.capability_id, diagnostic.reason, diagnostic.blocking});
  }
  for (const auto& operation : description.operations) {
    const auto status = PassRegistry::classify(operation);
    PassStatusRecord record{operation.capability_id, operation.program, status, {}};
    if (status == PassStatus::Disabled) record.reason = operation.disabled_reason.empty() ? "操作已禁用" : operation.disabled_reason;
    if (status == PassStatus::MissingResource) record.reason = "滤镜或配置文件资源未解析，跳过操作";
    if (status == PassStatus::Unregistered) record.reason = "渲染程序未注册，跳过操作";
    if (status == PassStatus::ReferenceOnly) record.reason = "仅有 reference 实现";
    result.statuses.push_back(record);
    PreparedOperation draw;
    draw.operation = &operation;
    if (status == PassStatus::MissingResource && inputs.resources != nullptr) {
      std::string lut_error;
      if (inputs.resources->resolveLut(operation.program, draw.lut, lut_error) && valid_lut(draw.lut) && draw.lut.revision == description.document_revision) {
        draw.has_lut = true;
        result.statuses.back().status = PassStatus::ReferenceOnly;
        result.statuses.back().reason = "结构化 3D LUT 已解析，由 reference 三线性采样";
        result.diagnostics.push_back({operation.capability_id, result.statuses.back().reason, false});
      } else {
        result.diagnostics.push_back({operation.capability_id, lut_error.empty() ? record.reason : lut_error, false});
        continue;
      }
    }
    if (status == PassStatus::Unregistered || (status == PassStatus::MissingResource && !draw.has_lut)) {
      result.diagnostics.push_back({operation.capability_id, record.reason, false});
      continue;
    }
    if (!operation.enabled) {
      result.diagnostics.push_back({operation.capability_id, operation.disabled_reason.empty() ? "操作已禁用" : operation.disabled_reason, false});
      continue;
    }
    if (operation.blend_mode != "normal" && operation.blend_mode != "multiply" && operation.blend_mode != "screen") {
      result.statuses.back().status = PassStatus::InvalidInput;
      result.statuses.back().reason = "未知混合模式，跳过操作";
      result.diagnostics.push_back({operation.capability_id, "未知混合模式，跳过操作", false});
      continue;
    }
    bool missing = false;
    for (const auto& id : operation.mask_ids) {
      const auto* mask = mask_for(inputs, id);
      if (mask == nullptr || mask->width != input.width || mask->height != input.height || mask->alpha.size() < static_cast<std::size_t>(input.width) * input.height) { missing = true; break; }
      draw.masks.push_back(mask);
    }
    if (missing) { result.statuses.back().status = PassStatus::MissingResource; result.statuses.back().reason = "蒙版缺失或尺寸不匹配，跳过操作"; result.diagnostics.push_back({operation.capability_id, "蒙版缺失或尺寸不匹配，跳过操作", false}); continue; }
    std::map<std::string, float> values;
    for (const auto& value : operation.values) values[value.channel] = static_cast<float>(value.numeric_value);
    draw.is_color = operation.stage == "color" || operation.program.find("color") != std::string::npos;
    draw.is_tone = operation.stage == "tone" || operation.capability_id == "color.global" || operation.capability_id == "color.local" || operation.capability_id == "color.post";
    draw.is_detail = operation.capability_id == "detail";
    if (!draw.is_color && !draw.is_tone && !draw.is_detail && !draw.has_lut) continue;
    // Sub-groups of the global colour tool and grain, read once per operation.
    draw.has_groups = !draw.has_lut && operation.capability_id == "color.global";
    if (draw.has_groups) draw.groups = color_groups(values, operation);
    if (draw.groups.editor_offsets) {
      // The colour editor's target colour (eyedropper, range) is not registered,
      // so its offsets have nothing to act on yet.
      result.diagnostics.push_back({operation.capability_id, "色彩编辑器还没有目标颜色参数，偏移暂不绘制", false});
    }
    if (draw.is_detail) {
      draw.grain = grain_of(values, inputs.pixel_scale);
      draw.detail = detail_of(values);
      draw.detail_on = !cr::detailNeutral(draw.detail);
    }
    if (draw.is_detail && !draw.grain.on && !draw.detail_on) continue;
    draw.tone_color = tone_color_of(values);
    draw.color_active = color_is_active(draw.tone_color);
    draw.tone_active = tone_is_active(draw.tone_color);
    draw.pixel_scale = inputs.pixel_scale;
    draw.lut_amount = values.find("amount") == values.end() ? 1.0F : clamp01(get(values, "amount") / 100.0F);
    draw.blend = blend_of(operation.blend_mode);
    draws.push_back(std::move(draw));
  }
  return true;
}

float coverage(const PreparedOperation& draw, std::size_t pixel) {
  float weight = 1.0F;
  if (draw.masks.size() == 1) return draw.masks.front()->alpha[pixel] / 255.0F;
  for (const auto* mask : draw.masks) weight *= mask->alpha[pixel] / 255.0F;
  return weight;
}

// An operation with Camera Raw's whole-frame controls: the per-pixel steps before them, then
// PhotoCraft develop's stages 2, 3 and 3b on the whole frame (unclipped floats), then the
// per-pixel steps after them, blended back by coverage.  Masks choose where the result shows;
// the whole-frame stages always see the whole frame.
void draw_spatial(const PreparedOperation& draw, RgbaImage& frame) {
  const ToneColor& t = draw.tone_color;
  cr::Frame f;
  f.width = frame.width;
  f.height = frame.height;
  f.px.resize(static_cast<std::size_t>(frame.width) * frame.height);
  std::vector<Pixel> before(f.px.size());
  for_rows(frame.width, frame.height, [&](std::uint32_t first, std::uint32_t end) {
    for (std::uint32_t y = first; y < end; ++y) for (std::uint32_t x = 0; x < frame.width; ++x) {
      const std::size_t i = static_cast<std::size_t>(y) * frame.width + x;
      before[i] = read(frame, x, y);
      Pixel p = before[i];
      if (draw.color_active) p = color(p, t);
      p = tone_brightness(p, t);
      f.px[i] = {p.r, p.g, p.b, p.a};
    }
  });
  if (t.dehaze != 0.0F) cr::dehaze(f, t.dehaze / 100.0F);
  if (t.shadows != 0.0F || t.highlights != 0.0F) cr::localTone(f, t.shadows, t.highlights);
  if (t.contrast != 0.0F || t.texture != 0.0F || t.clarity != 0.0F) cr::toneDetail(f, t.contrast, t.texture, t.clarity, draw.pixel_scale);
  if (t.whites != 0.0F || t.blacks != 0.0F) cr::whitesBlacks(f, t.whites, t.blacks);
  for_rows(frame.width, frame.height, [&](std::uint32_t first, std::uint32_t end) {
    for (std::uint32_t y = first; y < end; ++y) for (std::uint32_t x = 0; x < frame.width; ++x) {
      const std::size_t i = static_cast<std::size_t>(y) * frame.width + x;
      Pixel after{f.px[i][0], f.px[i][1], f.px[i][2], before[i].a};
      after = tone_presence(after, t);
      if (draw.has_groups) after = apply_color_groups(after, draw.groups);
      if (draw.masks.empty() && draw.blend == Blend::Normal && before[i].a >= 1.0F) {
        write(frame, x, y, after);
      } else {
        write(frame, x, y, blend_back(before[i], after, coverage(draw, i), draw.blend));
      }
    }
  });
}

// A detail operation with sharpening or noise reduction: stage 8 on the whole frame, then the
// grain (stage 9) on its float result, blended back by coverage.
void draw_detail(const PreparedOperation& draw, RgbaImage& frame) {
  cr::Frame f;
  f.width = frame.width;
  f.height = frame.height;
  f.px.resize(static_cast<std::size_t>(frame.width) * frame.height);
  std::vector<Pixel> before(f.px.size());
  for_rows(frame.width, frame.height, [&](std::uint32_t first, std::uint32_t end) {
    for (std::uint32_t y = first; y < end; ++y) for (std::uint32_t x = 0; x < frame.width; ++x) {
      const std::size_t i = static_cast<std::size_t>(y) * frame.width + x;
      before[i] = read(frame, x, y);
      f.px[i] = {before[i].r, before[i].g, before[i].b, before[i].a};
    }
  });
  cr::detail(f, draw.detail, draw.pixel_scale);
  for_rows(frame.width, frame.height, [&](std::uint32_t first, std::uint32_t end) {
    for (std::uint32_t y = first; y < end; ++y) for (std::uint32_t x = 0; x < frame.width; ++x) {
      const std::size_t i = static_cast<std::size_t>(y) * frame.width + x;
      Pixel after{f.px[i][0], f.px[i][1], f.px[i][2], before[i].a};
      if (draw.grain.on) after = apply_grain(after, x, y, draw.grain);
      if (draw.masks.empty() && draw.blend == Blend::Normal && before[i].a >= 1.0F) {
        write(frame, x, y, after);
      } else {
        write(frame, x, y, blend_back(before[i], after, coverage(draw, i), draw.blend));
      }
    }
  });
}

void draw_reference(const PreparedOperation& draw, RgbaImage& frame) {
  if (draw.is_detail && draw.detail_on) {
    draw_detail(draw, frame);
    return;
  }
  if (!draw.has_lut && draw.tone_active && draw.tone_color.spatial) {
    draw_spatial(draw, frame);
    return;
  }
  const RenderMask* single_mask = draw.masks.size() == 1 ? draw.masks.front() : nullptr;
  const bool direct_normal = draw.masks.empty() && draw.blend == Blend::Normal;
  for_rows(frame.width, frame.height, [&](std::uint32_t first, std::uint32_t end) {
    for (std::uint32_t y = first; y < end; ++y) for (std::uint32_t x = 0; x < frame.width; ++x) {
      const Pixel before = read(frame, x, y);
      Pixel after = before;
      if (draw.has_lut) after = mix(before, lut_sample(draw.lut, before), draw.lut_amount);
      if (!draw.has_lut && draw.color_active) after = color(after, draw.tone_color);
      if (!draw.has_lut && draw.tone_active) after = tone(after, draw.tone_color);
      if (draw.has_groups) after = apply_color_groups(after, draw.groups);
      if (draw.is_detail) after = apply_grain(after, x, y, draw.grain);
      const std::size_t pixel = static_cast<std::size_t>(y) * frame.width + x;
      if (direct_normal && before.a >= 1.0F) {
        write(frame, x, y, after);
      } else {
        float weight = 1.0F;
        if (single_mask != nullptr) {
          weight = single_mask->alpha[pixel] / 255.0F;
        } else {
          for (const auto* mask : draw.masks) weight *= mask->alpha[pixel] / 255.0F;
        }
        write(frame, x, y, blend_back(before, after, weight, draw.blend));
      }
    }
  });
}

// A drag on the common channel-independent tone controls does not need the
// general per-pixel colour pipeline.  Their exact byte result can be
// precomputed once and applied as a 256-entry lookup table.  Keeping this as a
// narrow fast path preserves the reference formula and its rounding/clamping
// semantics while avoiding exp2/pow and the general colour-group dispatch for
// every pixel.
bool scalar_tone_operation(const PreparedOperation& draw) {
  if (draw.operation == nullptr || draw.has_lut || draw.is_detail || !draw.is_color || !draw.is_tone ||
      !draw.masks.empty() || draw.blend != Blend::Normal || std::abs(draw.tone_color.style) > 1e-7F ||
      draw.groups.hsl || draw.groups.monochrome || draw.groups.selective || draw.groups.grading ||
      draw.groups.editor_offsets) {
    return false;
  }
  for (const auto& value : draw.operation->values) {
    // Each of these acts on every channel on its own (Camera Raw's exposure gain, the
    // brightness curve, RGB levels).  Contrast works on luminance, so it is not here.
    const bool scalar = value.channel == "exposure" || value.channel == "brightness";
    const bool levels = draw.groups.levels && has_prefix(value.channel, "levels_rgb_");
    const bool curves = has_prefix(value.channel, "curve_");  // each table acts on one channel
    if (!scalar && !levels && !curves) {
      return false;
    }
  }
  return true;
}

void draw_scalar_tone(const PreparedOperation& draw, RgbaImage& frame) {
  // `after` keeps the unquantised result: partially transparent pixels are
  // composited from it exactly as draw_reference does.
  std::array<std::array<std::uint8_t, 256>, 3> lut{};
  std::array<std::array<float, 256>, 3> after{};
  for (std::size_t channel = 0; channel < lut.size(); ++channel) {
    for (std::size_t i = 0; i < lut[channel].size(); ++i) {
      // The same functions draw_reference calls, on a grey of this level.
      const float level = static_cast<float>(i) / 255.0F;
      Pixel p{level, level, level, 1.0F};
      if (draw.color_active) p = color(p, draw.tone_color);
      if (draw.tone_active) p = tone(p, draw.tone_color);
      if (draw.groups.levels) p = apply_levels(p, draw.groups);
      if (draw.groups.curves) p = with_rgb(p, cr::applyCurves(rgb_of(p), draw.groups.curve_tables));
      const float value = channel == 0 ? p.r : channel == 1 ? p.g : p.b;
      after[channel][i] = value;
      lut[channel][i] = static_cast<std::uint8_t>(std::lround(clamp01(value) * 255.0F));
    }
  }
  const std::size_t stride = frame.row_stride == 0 ? static_cast<std::size_t>(frame.width) * 4 : frame.row_stride;
  for_rows(frame.width, frame.height, [&](std::uint32_t first, std::uint32_t end) {
    for (std::uint32_t y = first; y < end; ++y) {
      auto* row = frame.pixels.data() + static_cast<std::size_t>(y) * stride;
      for (std::uint32_t x = 0; x < frame.width; ++x) {
        auto* pixel = row + static_cast<std::size_t>(x) * 4;
        if (pixel[3] != 255) {
          const Pixel before = read(frame, x, y);
          const Pixel top{after[0][pixel[0]], after[1][pixel[1]], after[2][pixel[2]], before.a};
          write(frame, x, y, blend_back(before, top, 1.0F, Blend::Normal));
          continue;
        }
        pixel[0] = lut[0][pixel[0]];
        pixel[1] = lut[1][pixel[1]];
        pixel[2] = lut[2][pixel[2]];
      }
    }
  });
}

// A visible, valid layer to composite over the frame, in order.
struct PreparedLayer {
  const RenderLayer* layer = nullptr;
  const RenderMask* mask = nullptr;
  Blend blend = Blend::Normal;
};

// Records every layer's status and diagnostics and lists those that draw.
std::vector<PreparedLayer> prepare_layers(const RenderInputs& inputs, std::uint32_t width, std::uint32_t height,
                                          RenderResult& result) {
  std::vector<const RenderLayer*> layers;
  for (const auto& layer : inputs.layers) layers.push_back(&layer);
  std::stable_sort(layers.begin(), layers.end(), [](const RenderLayer* a, const RenderLayer* b) { return a->order < b->order; });
  std::vector<PreparedLayer> drawn;
  for (const auto* layer : layers) {
    const auto layer_stride = layer->image.row_stride == 0 ? layer->image.width * 4 : layer->image.row_stride;
    const std::string layer_id = "layer:" + std::to_string(layer->order);
    if (!layer->visible) { result.statuses.push_back({layer_id, "layer", PassStatus::Disabled, "图层已隐藏"}); continue; }
    if (layer->image.width != width || layer->image.height != height || layer_stride < width * 4 || layer->image.pixels.size() < static_cast<std::size_t>(layer_stride) * layer->image.height) {
      result.statuses.push_back({layer_id, "layer", PassStatus::MissingResource, "图层尺寸或行距无效，跳过图层"});
      result.diagnostics.push_back({layer_id, "图层尺寸或行距无效，跳过图层", false});
      continue;
    }
    if (layer->blend_mode != "normal" && layer->blend_mode != "multiply" && layer->blend_mode != "screen") {
      result.statuses.push_back({layer_id, "layer", PassStatus::InvalidInput, "未知图层混合模式，跳过图层"});
      result.diagnostics.push_back({layer_id, "未知图层混合模式，跳过图层", false});
      continue;
    }
    const auto* mask = layer->mask_id.empty() ? nullptr : mask_for(inputs, layer->mask_id);
    if (!layer->mask_id.empty() && (mask == nullptr || mask->width != width || mask->height != height || mask->alpha.size() < static_cast<std::size_t>(width) * height)) { result.statuses.push_back({layer_id, "layer", PassStatus::MissingResource, "图层蒙版缺失或尺寸不匹配，跳过图层"}); result.diagnostics.push_back({layer_id, "图层蒙版缺失或尺寸不匹配，跳过图层", false}); continue; }
    result.statuses.push_back({layer_id, "layer", PassStatus::Implemented, {}});
    drawn.push_back({layer, mask, blend_of(layer->blend_mode)});
  }
  return drawn;
}

void draw_layer_reference(const PreparedLayer& prepared, RgbaImage& frame) {
  const RenderLayer& layer = *prepared.layer;
  const RenderMask* mask = prepared.mask;
  for_rows(frame.width, frame.height, [&](std::uint32_t first, std::uint32_t end) {
    for (std::uint32_t y = first; y < end; ++y) for (std::uint32_t x = 0; x < frame.width; ++x) {
      const float amount = layer.opacity * (mask == nullptr ? 1.0F : mask->alpha[static_cast<std::size_t>(y) * frame.width + x] / 255.0F);
      write(frame, x, y, composite_layer(read(frame, x, y), read(layer.image, x, y), amount, prepared.blend));
    }
  });
}

// Up to four masks in the R, G, B, A channels of one RGBA8 frame; unused
// channels are 255 (weight 1).
// A rectangle of the frame drawn in one GPU pass sequence.
struct Tile {
  std::uint32_t x = 0, y = 0, width = 0, height = 0;
};

// Up to four masks, cut to the tile, in the R, G, B, A channels of one RGBA8
// buffer; unused channels are 255 (weight 1).
void pack_masks(const std::vector<const RenderMask*>& masks, std::uint32_t frame_width, const Tile& tile,
                std::vector<std::uint8_t>& packed) {
  packed.resize(static_cast<std::size_t>(tile.width) * tile.height * 4);
  const std::size_t used = std::min(masks.size(), kGpuMasksPerOperation);
  for_rows(tile.width, tile.height, [&](std::uint32_t first, std::uint32_t end) {
    for (std::uint32_t row = first; row < end; ++row) {
      const std::size_t source = static_cast<std::size_t>(tile.y + row) * frame_width + tile.x;
      std::uint8_t* out = packed.data() + static_cast<std::size_t>(row) * tile.width * 4;
      for (std::uint32_t x = 0; x < tile.width; ++x, out += 4) {
        for (std::size_t m = 0; m < 4; ++m) out[m] = m < used ? masks[m]->alpha[source + x] : 255;
      }
    }
  });
}

// Uniforms of one GPU pass, set from the prepared operation.
bool set_color_uniforms(gl::Program& program, const PreparedOperation& draw, std::string& error);

// An operation with whole-frame controls, around camera_raw_gl.cpp as draw_spatial does it.
// before: the colour step and brightness only, written unclipped (stage0.w).  After: Presence
// and the groups on the whole-frame result (stage0.z, unit 3), blended back.  Contrast is
// toneDetail's, so it is off in both.
bool set_spatial_part(gl::Program& program, const PreparedOperation& draw, bool before, std::string& error) {
  const ToneColor& t = draw.tone_color;
  const br::Curve curve = br::curve(t.brightness);
  const float presence = before ? 0.0F : (t.vibrance != 0.0F || t.saturation != 0.0F ? 1.0F : 0.0F);
  return program.setVec4("stage0", {before && draw.is_color ? 1.0F : 0.0F, 1.0F, before ? 0.0F : 1.0F, before ? 1.0F : 0.0F}, error) &&
         program.setVec4("cr1", {0.0F, t.vibrance, t.saturation, presence}, error) &&
         program.setVec4("bright1", {before && t.brightness_on ? 1.0F : 0.0F, curve.raise ? 1.0F : 0.0F, curve.hermite ? 1.0F : 0.0F, 0.0F}, error) &&
         (before || program.setTextureUnit("spatial", 3, error));
}

// Dehaze, Highlights/Shadows, Contrast + Texture + Clarity and Whites/Blacks on the float frame.
bool run_spatial_stages(pixcake::gpu::camera_raw_gl::Gl& g, pixcake::gpu::camera_raw_gl::Plane& frame, const PreparedOperation& draw) {
  namespace crgl = pixcake::gpu::camera_raw_gl;
  const ToneColor& t = draw.tone_color;
  return (t.dehaze == 0.0F || crgl::dehaze(g, frame, t.dehaze / 100.0F)) &&
         ((t.shadows == 0.0F && t.highlights == 0.0F) || crgl::localTone(g, frame, t.shadows, t.highlights)) &&
         ((t.contrast == 0.0F && t.texture == 0.0F && t.clarity == 0.0F) ||
          crgl::toneDetail(g, frame, t.contrast, t.texture, t.clarity, draw.pixel_scale)) &&
         ((t.whites == 0.0F && t.blacks == 0.0F) || crgl::whitesBlacks(g, frame, t.whites, t.blacks));
}

bool set_color_uniforms(gl::Program& program, const PreparedOperation& draw, std::string& error) {
  const ToneColor& t = draw.tone_color;
  const ColorGroups& g = draw.groups;
  const auto flag = [](bool value) { return value ? 1.0F : 0.0F; };
  // Sub-groups only belong to the global tool (has_groups); elsewhere they stay off.
  const bool colour_step = !draw.has_lut && draw.is_color, tone_step = !draw.has_lut && draw.is_tone;
  const br::Curve curve = br::curve(t.brightness);
  bool ok = program.setVec4("stage0", {flag(colour_step), flag(tone_step), 0.0F, 0.0F}, error) &&
            program.setVec4("cr0", {t.gains[0], t.gains[1], t.gains[2], flag(t.balance)}, error) &&
            program.setVec4("cal0", {t.calibration_hue[0], t.calibration_hue[1], t.calibration_hue[2], flag(t.calibration)}, error) &&
            program.setVec4("cal1", {t.calibration_sat[0], t.calibration_sat[1], t.calibration_sat[2], 0.0F}, error) &&
            program.setVec4("cr1", {t.contrast / 100.0F * 0.6F, t.vibrance, t.saturation,
                                    flag(t.vibrance != 0.0F || t.saturation != 0.0F)}, error) &&
            program.setVec4("bright0", {curve.s, curve.e, curve.x0, curve.alpha}, error) &&
            program.setVec4("bright1", {flag(t.brightness_on), flag(curve.raise), flag(curve.hermite), 0.0F}, error) &&
            program.setVec4("bright2", {t.brightness_slopes[0], t.brightness_slopes[1], 0.0F, 0.0F}, error) &&
            program.setVec4("tone0", {t.style, 0.0F, 0.0F, 0.0F}, error) &&
            program.setVec4("groups0", {flag(draw.has_groups && g.levels), flag(draw.has_groups && g.monochrome),
                                        flag(draw.has_groups && g.hsl), flag(draw.has_groups && g.selective)}, error) &&
            program.setVec4("groups1", {flag(draw.has_groups && g.grading), flag(g.absolute), g.grade.blend,
                                        g.grade.balance}, error) &&
            program.setVec4("levels0", {g.in_black, g.in_white, g.gamma, g.out_black}, error) &&
            program.setVec4("levels1", {g.out_white, 0.0F, 0.0F, 0.0F}, error) &&
            program.setVec4("curves0", {flag(draw.has_groups && g.curves && !g.curve_tables.parametric.empty()),
                                        flag(draw.has_groups && g.curves && !g.curve_tables.point[0].empty()),
                                        flag(draw.has_groups && g.curves && !g.curve_tables.point[1].empty()),
                                        flag(draw.has_groups && g.curves && !g.curve_tables.point[2].empty())}, error) &&
            program.setVec4("curves1", {flag(draw.has_groups && g.curves && !g.curve_tables.point[3].empty()), 0.0F, 0.0F, 0.0F}, error) &&
            program.setTextureUnit("curves", 4, error);
  const auto bands = [&](const char* a, const char* b, const auto& v) {
    return program.setVec4(a, {v[0], v[1], v[2], v[3]}, error) && program.setVec4(b, {v[4], v[5], v[6], v[7]}, error);
  };
  ok = ok && bands("hue_a", "hue_b", g.hue) && bands("sat_a", "sat_b", g.saturation) &&
       bands("light_a", "light_b", g.lightness) && bands("mix_a", "mix_b", g.mix);
  for (int target = 0; ok && target < 9; ++target) {
    const auto& ink = g.selective_ink[target];
    ok = program.setVec4("ink" + std::to_string(target), {ink[0], ink[1], ink[2], ink[3]}, error);
  }
  ok = ok && program.setVec4("zone_light", {g.grade.lums[0], g.grade.lums[1], g.grade.lums[2], g.grade.lums[3]}, error);
  for (int zone = 0; ok && zone < 4; ++zone) {
    const auto& tint = g.grade.tints[zone];
    ok = program.setVec4("zone_tint" + std::to_string(zone), {tint[0], tint[1], tint[2], 0.0F}, error);
  }
  return ok;
}
bool set_grain_uniforms(gl::Program& program, const PreparedOperation& draw, std::string& error) {
  const Grain& g = draw.grain;
  return program.setVec4("grain0", {g.base.cell, g.base.roughness, g.base.amount, g.base.scale}, error) &&
         program.setVec4("grain1", {g.shadows, g.midtones, g.highlights, g.colour}, error);
}

}  // namespace

PassStatus PassRegistry::classify(const pixcake::render::RenderOperation& operation) {
  if (!operation.enabled) return PassStatus::Disabled;
  if (operation.program.find("filter") != std::string::npos || operation.program.find("profile") != std::string::npos) return PassStatus::MissingResource;
  if (operation.program == "pixcake.color.global" || operation.program == "pixcake.detail" ||
      operation.program == "pixcake.color.local" || operation.program == "pixcake.color.post" ||
      operation.program == "pixcake.color.match" || operation.program == "pixcake.color.style") {
    return gpuSupported(operation) ? PassStatus::Implemented : PassStatus::ReferenceOnly;
  }
  return PassStatus::Unregistered;
}

bool PassRegistry::gpuSupported(const pixcake::render::RenderOperation& operation) {
  // 2026-11-05: temperature, tint, exposure and contrast passed the
  // reference/readback comparison on native CGL and the Qt desktop platform.
  // 2026-11-06: the rest of the global colour tool (tone, levels, HSL,
  // black-and-white, selective colour, grading), detail grain, local, post
  // and AI colour, masks (up to four per operation), the multiply and screen
  // blends and layers joined, each with its own readback comparison in
  // test_gpu_backend_C57_C60.  The formula gate is the evidence gate: a new
  // channel adds its readback first.
  return gpu_formula_supported(operation);
}

struct GpuCache {
  const void* context = nullptr;
  std::unique_ptr<pixcake::gpu::camera_raw_gl::Gl> spatial;  // float planes and programs
  gl::Shader vertex{nullptr};
  gl::Program color_program{nullptr};
  gl::Program grain_program{nullptr};
  gl::Program layer_program{nullptr};
  gl::Texture2D textures[2] = {gl::Texture2D{nullptr}, gl::Texture2D{nullptr}};
  gl::Framebuffer framebuffers[2] = {gl::Framebuffer{nullptr}, gl::Framebuffer{nullptr}};
  gl::Texture2D masks{nullptr};
  gl::Texture2D layer{nullptr};
  gl::Texture2D curves{nullptr};  // Camera Raw's curve tables, R32F, kCurveLut x 5 rows
  std::vector<float> curve_rows;
  std::uint32_t width = 0, height = 0;
  std::vector<std::uint8_t> packed;  // mask upload buffer
};

namespace {

// Compiles kPrelude + body and links it with the cached vertex shader, once.
bool ready_program(gl::Context& context, GpuCache& cache, gl::Program& program, const char* body, std::string& error) {
  if (program.valid()) return true;
  gl::Shader fragment = context.createShader();
  gl::Program linked = context.createProgram();
  const std::string source = std::string(shaders::kPrelude) + body;
  if (!fragment.valid() || !linked.valid() || !fragment.compile(gl::ShaderStage::Fragment, source, error) ||
      !linked.link(cache.vertex, fragment, error)) {
    return false;
  }
  program = linked;
  return true;
}

bool upload_masks(GpuCache& cache, gl::Context& context, const std::vector<const RenderMask*>& masks,
                  std::uint32_t frame_width, const Tile& tile, std::string& error) {
  if (!cache.masks.valid()) cache.masks = context.createTexture2D();
  pack_masks(masks, frame_width, tile, cache.packed);
  const gl::ImageView view{tile.width, tile.height, tile.width * 4, gl::PixelFormat::Rgba8,
                           reinterpret_cast<const std::byte*>(cache.packed.data()), cache.packed.size()};
  return cache.masks.valid() && cache.masks.upload(view, error);
}

// Camera Raw's curve tables as five R32F rows (parametric, master, red, green, blue); a row that
// is off stays 0 and is not read (curves0/curves1).
bool upload_curves(GpuCache& cache, gl::Context& context, const cr::Curves& curves, std::string& error) {
  if (!cache.curves.valid()) cache.curves = context.createTexture2D();
  constexpr std::size_t n = cr::kCurveLut;
  cache.curve_rows.assign(n * 5, 0.0F);
  const std::vector<float>* rows[5] = {&curves.parametric, &curves.point[0], &curves.point[1], &curves.point[2], &curves.point[3]};
  for (std::size_t r = 0; r < 5; ++r) {
    if (rows[r]->size() == n) std::copy(rows[r]->begin(), rows[r]->end(), cache.curve_rows.begin() + static_cast<std::ptrdiff_t>(r * n));
  }
  const gl::ImageView view{static_cast<std::uint32_t>(n), 5, static_cast<std::uint32_t>(n * 4), gl::PixelFormat::R32F,
                           reinterpret_cast<const std::byte*>(cache.curve_rows.data()), cache.curve_rows.size() * sizeof(float)};
  return cache.curves.valid() && cache.curves.upload(view, error);
}

// The tile of an RGBA image as an upload view: rows keep the image's stride,
// so nothing is copied.
gl::ImageView tile_view(const RgbaImage& image, const Tile& tile) {
  const std::size_t stride = image.row_stride == 0 ? static_cast<std::size_t>(image.width) * 4 : image.row_stride;
  const std::size_t offset = static_cast<std::size_t>(tile.y) * stride + static_cast<std::size_t>(tile.x) * 4;
  return {tile.width, tile.height, static_cast<std::uint32_t>(stride), gl::PixelFormat::Rgba8,
          reinterpret_cast<const std::byte*>(image.pixels.data() + offset), image.pixels.size() - offset};
}

}  // namespace

bool RenderExecutor::renderReference(const pixcake::render::RenderDescription& description,
                                     const RgbaImage& input, const RenderInputs& inputs,
                                     RenderResult& result) const {
  std::vector<PreparedOperation> draws;
  if (!prepare(description, input, inputs, result, draws)) return false;
  if (draws.size() == 1 && inputs.layers.empty() && scalar_tone_operation(draws.front())) {
    draw_scalar_tone(draws.front(), result.image);
    result.success = true;
    return true;
  }
  for (const auto& draw : draws) draw_reference(draw, result.image);
  for (const auto& layer : prepare_layers(inputs, input.width, input.height, result)) {
    draw_layer_reference(layer, result.image);
  }
  result.success = true;
  return true;
}

bool RenderExecutor::execute(gl::Context& context, const pixcake::render::RenderDescription& description,
                             const RgbaImage& input, const RenderInputs& inputs, RenderResult& result,
                             std::string& error) const {
  if (!context.valid() || !context.makeCurrent()) { error = context.lastError(); return false; }
  std::vector<PreparedOperation> draws;
  RenderResult prepared;
  const bool frame_ok = prepare(description, input, inputs, prepared, draws, false);
  bool gpu_supported = frame_ok;
  for (const auto& draw : draws) {
    gpu_supported = gpu_supported && !draw.has_lut && draw.masks.size() <= kGpuMasksPerOperation &&
                    gpu_formula_supported(*draw.operation);
  }
  // Camera Raw's whole-frame stages need the whole frame in one GPU tile.
  bool spatial = false;
  for (const auto& draw : draws) spatial = spatial || (draw.tone_active && draw.tone_color.spatial) || (draw.is_detail && draw.detail_on);
  const std::uint32_t spatial_edge = std::max<std::uint32_t>(1, gpu_tile_);
  const bool spatial_too_large = gpu_supported && spatial &&
                                 (input.width > spatial_edge || input.height > spatial_edge ||
                                  static_cast<std::uint64_t>(input.width) * input.height < spatial_gpu_min_pixels_);
  if (!gpu_supported || spatial_too_large) {
    const bool ok = renderReference(description, input, inputs, result);
    result.reference_fallback = true;
    result.diagnostics.push_back({"", spatial_too_large ? "当前绘制描述含 Camera Raw 整帧参数，且帧大于 GPU 分块或小于 GPU 更快的尺寸，已回退 reference"
                                                        : "当前绘制描述包含 GPU 路径未覆盖的内容（3D LUT、超过四个蒙版或未登记的参数），已回退 reference",
                                  false});
    context.doneCurrent();
    if (!ok) error = "reference render failed";
    return ok;
  }
  result = std::move(prepared);
  const std::vector<PreparedLayer> layers = prepare_layers(inputs, input.width, input.height, result);
  if (draws.empty() && layers.empty()) {
    result.image.pixels = input.pixels;
    context.doneCurrent();
    result.success = true;
    return true;
  }

  // Programs compile once per context.  A frame larger than the tile size
  // (full-resolution export) draws tile by tile; the two frame textures are
  // reallocated only when the tile size changes.  Grain takes the tile's
  // origin, so tiles join without seams.
  if (!gpu_cache_ || gpu_cache_->context != context.identity()) {
    gpu_cache_ = std::make_shared<GpuCache>();
    gpu_cache_->context = context.identity();
  }
  GpuCache& cache = *gpu_cache_;
  bool ok = cache.vertex.valid() || ((cache.vertex = context.createShader()).valid() &&
                                     cache.vertex.compile(gl::ShaderStage::Vertex, shaders::kVertex, error));
  result.image.row_stride = input.width * 4;
  result.image.pixels.resize(static_cast<std::size_t>(result.image.row_stride) * input.height);
  const std::uint32_t edge = std::max<std::uint32_t>(1, gpu_tile_);
  std::vector<std::byte> pixels;
  for (std::uint32_t tile_y = 0; ok && tile_y < input.height; tile_y += edge) {
    for (std::uint32_t tile_x = 0; ok && tile_x < input.width; tile_x += edge) {
      const Tile tile{tile_x, tile_y, std::min(edge, input.width - tile_x), std::min(edge, input.height - tile_y)};
      if (cache.width != tile.width || cache.height != tile.height) {
        for (int i = 0; ok && i < 2; ++i) {
          cache.textures[i] = context.createTexture2D();
          cache.framebuffers[i] = context.createFramebuffer();
          ok = cache.textures[i].valid() && cache.framebuffers[i].valid() &&
               cache.textures[i].allocate(tile.width, tile.height, gl::PixelFormat::Rgba8, error) &&
               cache.framebuffers[i].attachColor(cache.textures[i], error) && cache.framebuffers[i].complete(error);
        }
        cache.width = ok ? tile.width : 0;
        cache.height = ok ? tile.height : 0;
      }
      // Two RGBA8 textures ping-pong: each pass reads one and draws into the
      // other, so every pass is quantised to 8 bits like the reference.
      ok = ok && cache.textures[0].upload(tile_view(input, tile), error);
      int current = 0;
      // A pass without masks still gives the mask sampler a complete texture
      // (the frame it reads); the driver warns about an empty unit otherwise.
      const auto pass = [&](gl::Program& program, bool masked, const auto& set_uniforms) {
        const int next = 1 - current;
        ok = ok && program.use(error) && cache.framebuffers[next].bind(error) &&
             context.setViewport(tile.width, tile.height, error) && cache.textures[current].bind(0, error) &&
             (masked || cache.textures[current].bind(1, error)) && program.setTextureUnit("source", 0, error) &&
             set_uniforms() && context.drawTriangles(3, error);
        current = next;
      };
      for (const auto& draw : draws) {
        if (!ok) break;
        const bool grain = draw.is_detail;
        gl::Program& program = grain ? cache.grain_program : cache.color_program;
        ok = ready_program(context, cache, program, grain ? shaders::kGrain : shaders::kColor, error);
        // Whole-frame stages (the frame is one tile here): the first part into a float frame,
        // the stages on it, then the second part below reads it back from unit 3.
        const bool spatial_draw = !grain && draw.tone_active && draw.tone_color.spatial;
        pixcake::gpu::camera_raw_gl::Plane spatial_frame;
        if (ok && spatial_draw) {
          namespace crgl = pixcake::gpu::camera_raw_gl;
          if (!cache.spatial) cache.spatial = std::make_unique<crgl::Gl>(context);
          auto& sg = *cache.spatial;
          ok = crgl::allocate(sg, tile.width, tile.height, gl::PixelFormat::Rgba32F, spatial_frame) && program.use(error) &&
               crgl::framebufferOf(spatial_frame).bind(error) && context.setViewport(tile.width, tile.height, error) &&
               cache.textures[current].bind(0, error) && cache.textures[current].bind(1, error) &&
               program.setTextureUnit("source", 0, error) && program.setTextureUnit("masks", 1, error) &&
               program.setVec4("blend0", {0.0F, 0.0F, 0.0F, 0.0F}, error) && set_color_uniforms(program, draw, error) &&
               set_spatial_part(program, draw, true, error) && context.drawTriangles(3, error) && run_spatial_stages(sg, spatial_frame, draw);
          if (!ok && !sg.ok()) error = sg.error();
        }
        // Stage 8 (the frame is one tile here): the frame as it is into a float plane (kColor with
        // every step off, stage0.w), sharpening and noise reduction on it, then the grain pass
        // below reads it from unit 3.
        const bool detail_draw = grain && draw.detail_on;
        if (ok && detail_draw) {
          namespace crgl = pixcake::gpu::camera_raw_gl;
          if (!cache.spatial) cache.spatial = std::make_unique<crgl::Gl>(context);
          auto& sg = *cache.spatial;
          gl::Program& copy = cache.color_program;
          ok = ready_program(context, cache, copy, shaders::kColor, error) &&
               crgl::allocate(sg, tile.width, tile.height, gl::PixelFormat::Rgba32F, spatial_frame) && copy.use(error) &&
               crgl::framebufferOf(spatial_frame).bind(error) && context.setViewport(tile.width, tile.height, error) &&
               cache.textures[current].bind(0, error) && cache.textures[current].bind(1, error) &&
               cache.textures[current].bind(4, error) && copy.setTextureUnit("source", 0, error) &&
               copy.setTextureUnit("masks", 1, error) && copy.setVec4("blend0", {0.0F, 0.0F, 0.0F, 0.0F}, error) &&
               set_color_uniforms(copy, draw, error) && copy.setVec4("stage0", {0.0F, 0.0F, 0.0F, 1.0F}, error) &&
               context.drawTriangles(3, error) && crgl::detail(sg, spatial_frame, draw.detail, draw.pixel_scale);
          if (!ok && !sg.ok()) error = sg.error();
        }
        const auto masks = static_cast<float>(draw.masks.size());
        // Upload first, bind after: an upload binds its texture to the active unit.
        const bool curves = !grain && draw.has_groups && draw.groups.curves;
        ok = ok && (!curves || upload_curves(cache, context, draw.groups.curve_tables, error)) &&
             (draw.masks.empty() || upload_masks(cache, context, draw.masks, input.width, tile, error)) &&
             (draw.masks.empty() || cache.masks.bind(1, error)) &&
             // Without curves the sampler still needs a complete texture (the driver warns about an
             // unloadable unit otherwise); the frame read on unit 0 serves, never sampled there.
             (grain || (curves ? cache.curves.bind(4, error) : cache.textures[current].bind(4, error)));
        pass(program, !draw.masks.empty(), [&] {
          return program.setTextureUnit("masks", 1, error) &&
                 program.setVec4("blend0", {masks, static_cast<float>(draw.blend), 0.0F, 0.0F}, error) &&
                 (grain ? set_grain_uniforms(program, draw, error) &&
                              program.setVec4("grain2", {static_cast<float>(tile.x), static_cast<float>(tile.y), 0.0F, 0.0F}, error) &&
                              // Bound here, after the mask upload (an upload binds to the active unit).
                              (detail_draw ? pixcake::gpu::camera_raw_gl::textureOf(spatial_frame).bind(3, error) &&
                                                 program.setTextureUnit("spatial", 3, error)
                                           : program.setTextureUnit("spatial", 0, error)) &&
                              program.setVec4("grain3", {detail_draw ? 1.0F : 0.0F, draw.grain.on ? 1.0F : 0.0F, 0.0F, 0.0F}, error)
                        : set_color_uniforms(program, draw, error) &&
                              // Bound here, after the mask upload (an upload binds to the active unit).
                              (!spatial_draw || (pixcake::gpu::camera_raw_gl::textureOf(spatial_frame).bind(3, error) &&
                                                 set_spatial_part(program, draw, false, error))));
        });
      }
      for (const auto& layer : layers) {
        if (!ok) break;
        ok = ready_program(context, cache, cache.layer_program, shaders::kLayer, error);
        if (ok && !cache.layer.valid()) cache.layer = context.createTexture2D();
        // Upload first, bind after: an upload binds its texture to the active unit.
        ok = ok && cache.layer.valid() && cache.layer.upload(tile_view(layer.layer->image, tile), error) &&
             (layer.mask == nullptr || upload_masks(cache, context, {layer.mask}, input.width, tile, error)) &&
             cache.layer.bind(2, error) && (layer.mask == nullptr || cache.masks.bind(1, error));
        pass(cache.layer_program, layer.mask != nullptr, [&] {
          return cache.layer_program.setTextureUnit("layer", 2, error) &&
                 cache.layer_program.setTextureUnit("masks", 1, error) &&
                 cache.layer_program.setVec4("layer0", {layer.layer->opacity, layer.mask != nullptr ? 1.0F : 0.0F,
                                                        static_cast<float>(layer.blend), 0.0F}, error);
        });
      }
      ok = ok && context.readPixels(tile.width, tile.height, pixels, error);
      if (ok) {
        const std::size_t row_bytes = static_cast<std::size_t>(tile.width) * 4;
        for (std::uint32_t row = 0; row < tile.height; ++row) {
          std::memcpy(result.image.pixels.data() + static_cast<std::size_t>(tile.y + row) * result.image.row_stride +
                          static_cast<std::size_t>(tile.x) * 4,
                      pixels.data() + row * row_bytes, row_bytes);
        }
      }
    }
  }
  // The whole-frame stages' float planes are reused within a render only: a long-lived renderd
  // or exportd sees many frame sizes, and a pool kept per size would only grow.
  if (cache.spatial) cache.spatial->trim();
  if (ok) {
    result.success = true;
  } else {
    gpu_cache_.reset();  // rebuild from scratch next time
  }
  context.doneCurrent();
  if (!ok && error.empty()) error = "GPU pass failed";
  return ok;
}

void RenderExecutor::setGpuTileSize(std::uint32_t edge) { gpu_tile_ = edge; }
void RenderExecutor::setSpatialGpuMinPixels(std::uint64_t pixels) { spatial_gpu_min_pixels_ = pixels; }
std::size_t RenderExecutor::gpuPooledBytes() const {
  return gpu_cache_ != nullptr && gpu_cache_->spatial != nullptr ? gpu_cache_->spatial->pooledBytes() : 0;
}

ResultCode RenderExecutor::validate(const render::RenderDescription& description,
                                    std::string& error) {
  if (description.schema_version != render::kRenderDescriptionVersion) {
    error = "render description version is unsupported";
    return ResultCode::CapabilityTooNew;
  }
  std::uint32_t previous_order = 0;
  bool first_operation = true;
  for (const auto& operation : description.operations) {
    if (operation.capability_id.empty() || operation.stage.empty() || operation.program.empty()) {
      error = "render operation identity is empty";
      return ResultCode::StateConflict;
    }
    if (!first_operation && operation.order < previous_order) {
      error = "render operations are out of pipeline order";
      return ResultCode::StateConflict;
    }
    previous_order = operation.order;
    first_operation = false;
    if (!operation.enabled) {
      if (operation.disabled_reason.empty()) {
        error = "disabled render operation has no reason";
        return ResultCode::StateConflict;
      }
      continue;
    }
    for (const auto& value : operation.values) {
      if (value.parameter_id.empty() || value.channel.empty() || value.display_value.empty() ||
          !std::isfinite(value.numeric_value)) {
        error = "render value is malformed";
        return ResultCode::StateConflict;
      }
    }
  }

  std::unordered_set<std::string> layer_ids;
  for (const auto& layer : description.layers) {
    if (layer.id.empty() || !layer_ids.insert(layer.id).second) {
      error = "render layer identity is empty or duplicated";
      return ResultCode::StateConflict;
    }
    if (layer.opacity > 100 || layer.mask_density > 100 || layer.mask_feather > 100) {
      error = "render layer range is invalid";
      return ResultCode::StateConflict;
    }
    if (!known_blend_mode(layer.blend_mode)) {
      error = "render layer blend mode is unknown";
      return ResultCode::StateConflict;
    }
    if (!layer.parent_id.empty() && layer.parent_id == layer.id) {
      error = "render layer cannot parent itself";
      return ResultCode::StateConflict;
    }
  }
  for (const auto& layer : description.layers) {
    if (!layer.parent_id.empty() && layer_ids.find(layer.parent_id) == layer_ids.end()) {
      error = "render layer parent is missing";
      return ResultCode::StateConflict;
    }
  }
  for (const auto& layer : description.layers) {
    std::unordered_set<std::string> chain;
    std::string current = layer.id;
    while (!current.empty()) {
      if (!chain.insert(current).second) {
        error = "render layer parent relation contains a cycle";
        return ResultCode::StateConflict;
      }
      const auto found = std::find_if(description.layers.begin(), description.layers.end(),
                                      [&](const render::RenderLayer& candidate) {
                                        return candidate.id == current;
                                      });
      if (found == description.layers.end()) break;
      current = found->parent_id;
    }
  }
  if (description.selection.feather > 1000) {
    error = "render selection feather is invalid";
    return ResultCode::StateConflict;
  }
  for (const auto& diagnostic : description.diagnostics) {
    if (diagnostic.blocking) {
      error = diagnostic.reason.empty() ? "render description has a blocking diagnostic"
                                        : diagnostic.reason;
      return ResultCode::StateConflict;
    }
  }
  error.clear();
  return ResultCode::Ok;
}

bool RenderExecutor::execute(gl::Context& context, const render::RenderDescription& description,
                             const std::vector<Pass>& passes, std::string& error) const {
  const ResultCode valid = validate(description, error);
  if (valid != ResultCode::Ok) return false;
  if (passes.size() != description.operations.size()) {
    error = "render pass count does not match render description";
    return false;
  }
  return execute(context, passes, error);
}

bool RenderExecutor::execute(gl::Context& context, const std::vector<Pass>& passes,
                             std::string& error) const {
  if (!context.valid()) { error = "render context is empty"; return false; }
  for (const auto& pass : passes) {
    if (!pass) { error = "render pass is empty"; return false; }
    if (!pass(context, error)) return false;
  }
  return true;
}

}  // namespace pixcake::gpu
