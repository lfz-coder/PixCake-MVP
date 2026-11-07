#include "pixcake/gpu/render_executor.hpp"

#include "pixcake/capability/capability_registry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
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
struct Vec3 { float x, y, z; };
struct Mat3 { float m[3][3]; };
float clamp01(float value) { return std::clamp(value, 0.0F, 1.0F); }
// PP-EXP-001: exposure is an EV offset over the linear working buffer.
// ISO 2720 defines one EV step as a factor-of-two exposure change.  The
// conversion to and from an encoded file color space belongs to the I/O
// adapter, so this function deliberately operates only on linear values.
float apply_exposure(float value, float ev) { return value * std::exp2(ev); }
float get(const std::map<std::string, float>& values, const char* key) {
  const auto it = values.find(key);
  return it == values.end() ? 0.0F : it->second;
}
Vec3 mul(const Mat3& matrix, Vec3 value) {
  return {matrix.m[0][0] * value.x + matrix.m[0][1] * value.y + matrix.m[0][2] * value.z,
          matrix.m[1][0] * value.x + matrix.m[1][1] * value.y + matrix.m[1][2] * value.z,
          matrix.m[2][0] * value.x + matrix.m[2][1] * value.y + matrix.m[2][2] * value.z};
}
Mat3 multiply(const Mat3& left, const Mat3& right) {
  Mat3 result{};
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      for (int k = 0; k < 3; ++k) result.m[row][column] += left.m[row][k] * right.m[k][column];
    }
  }
  return result;
}
Mat3 identity_matrix() {
  return {{{1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}}};
}
Vec3 add(Vec3 left, Vec3 right) { return {left.x + right.x, left.y + right.y, left.z + right.z}; }
Vec3 scale(Vec3 value, float factor) { return {value.x * factor, value.y * factor, value.z * factor}; }
Vec3 white_from_xy(float x, float y) { return {x / y, 1.0F, (1.0F - x - y) / y}; }

// PP-WB-001: IEC/ICC sRGB D65 matrices and the linear Bradford CAT from ICC
// 1:2022 Annex E.  The matrices are kept here as data, not as UI constants.
constexpr Mat3 kSrgbToXyz{{{0.4124564F, 0.3575761F, 0.1804375F},
                            {0.2126729F, 0.7151522F, 0.0721750F},
                            {0.0193339F, 0.1191920F, 0.9503041F}}};
constexpr Mat3 kXyzToSrgb{{{3.2404542F, -1.5371385F, -0.4985314F},
                            {-0.9692660F, 1.8760108F, 0.0415560F},
                            {0.0556434F, -0.2040259F, 1.0572252F}}};
constexpr Mat3 kBradford{{{0.8951F, 0.2664F, -0.1614F},
                           {-0.7502F, 1.7135F, 0.0367F},
                           {0.0389F, -0.0685F, 1.0296F}}};
constexpr Mat3 kBradfordInverse{{{0.9869929F, -0.1470543F, 0.1599627F},
                                  {0.4323053F, 0.5183603F, 0.0492912F},
                                  {-0.0085287F, 0.0400428F, 0.9684867F}}};

Mat3 white_balance_matrix(float temperature, float tint) {
  if (std::abs(temperature) < 1e-6F && std::abs(tint) < 1e-6F) return identity_matrix();
  const Vec3 source_white = white_from_xy(0.3127F, 0.3290F);  // CIE D65.
  const Vec3 warm_white = white_from_xy(0.3457F, 0.3585F);    // CIE D50.
  const Vec3 cool_white = white_from_xy(0.2990F, 0.3149F);    // CIE D75.
  const float temperature_fraction = std::clamp(temperature / 100.0F, -1.0F, 1.0F);
  const Vec3 target_white = temperature_fraction >= 0.0F
                                ? add(source_white, scale(add(warm_white, scale(source_white, -1.0F)),
                                                         temperature_fraction))
                                : add(source_white, scale(add(cool_white, scale(source_white, -1.0F)),
                                                         -temperature_fraction));
  const Vec3 source_lms = mul(kBradford, source_white);
  Vec3 target_lms = mul(kBradford, target_white);
  // Project consensus PP-WB-001: tint is a symmetric half-stop adjustment
  // of the Bradford green-cone response.  The scale is documented here and
  // is not a hidden per-channel coefficient.
  target_lms.y *= std::exp2(std::clamp(tint / 100.0F, -1.0F, 1.0F) * 0.5F);
  const Mat3 cone_scale{{{target_lms.x / source_lms.x, 0.0F, 0.0F},
                         {0.0F, target_lms.y / source_lms.y, 0.0F},
                         {0.0F, 0.0F, target_lms.z / source_lms.z}}};
  // RGB -> XYZ -> Bradford LMS -> scaled LMS -> XYZ -> RGB.
  return multiply(kXyzToSrgb, multiply(kBradfordInverse, multiply(cone_scale,
                                                                   multiply(kBradford, kSrgbToXyz))));
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
// White balance, style tint and tone, read once per operation (the white
// balance matrix and the exposure gain included) instead of once per pixel.
// The float operations and their order are the same as the per-pixel form
// they replace, so frames are bit-identical.
struct ToneColor {
  bool white_balance = false;
  Mat3 white_balance_matrix{};
  float style = 0;
  float exposure_gain = 1, brightness = 0, contrast_amount = 1;
  float highlights = 0, shadows = 0, whites = 0, blacks = 0, local = 0, saturation = 0, vibrance = 0;
};
ToneColor tone_color_of(const std::map<std::string, float>& v) {
  ToneColor t;
  const float temperature = get(v, "temperature");
  const float tint = get(v, "tint");
  t.white_balance = !(std::abs(temperature) < 1e-6F && std::abs(tint) < 1e-6F);
  if (t.white_balance) t.white_balance_matrix = white_balance_matrix(temperature, tint);
  // 程度 100 是默认强度：AI 色彩风格 0..100，AI 追色 0..200（200 为两倍强度），与参数模式一致。
  t.style = std::clamp(get(v, "amount") / 100.0F, 0.0F, 2.0F);
  // PP-EXP-001: the EV gain, computed once (apply_exposure(1, ev) is exactly exp2(ev)).
  t.exposure_gain = apply_exposure(1.0F, get(v, "exposure"));
  t.brightness = get(v, "brightness") / 100.0F;
  // PP-CONTRAST-001: W3C Filter Effects contrast() is an affine transfer
  // around the 0.5 midpoint.  The project maps -100..100 to a 0..2 slope.
  t.contrast_amount = 1.0F + get(v, "contrast") / 100.0F;
  t.highlights = get(v, "highlights") / 100.0F;
  t.shadows = get(v, "shadows") / 100.0F;
  t.whites = get(v, "whites") / 100.0F;
  t.blacks = get(v, "blacks") / 100.0F;
  t.local = (get(v, "clarity") + get(v, "texture") + get(v, "dehaze")) / 300.0F * 0.35F;
  t.saturation = get(v, "saturation") / 100.0F;
  t.vibrance = get(v, "vibrance") / 100.0F;
  return t;
}
Pixel color(Pixel p, const ToneColor& t) {
  if (t.white_balance) {
    const Vec3 adapted = mul(t.white_balance_matrix, {p.r, p.g, p.b});
    p.r = adapted.x;
    p.g = adapted.y;
    p.b = adapted.z;
  }
  p.r += t.style * 0.04F; p.g += t.style * 0.01F; p.b -= t.style * 0.03F;
  return p;
}
// PP-EXP-001 and PP-CONTRAST-001 are source-backed reference effects here;
// brightness/highlights/shadows and the detail controls remain provisional.
Pixel tone(Pixel p, const ToneColor& t) {
  p.r *= t.exposure_gain; p.g *= t.exposure_gain; p.b *= t.exposure_gain;
  p.r += t.brightness; p.g += t.brightness; p.b += t.brightness;
  p.r = (p.r - 0.5F) * t.contrast_amount + 0.5F;
  p.g = (p.g - 0.5F) * t.contrast_amount + 0.5F;
  p.b = (p.b - 0.5F) * t.contrast_amount + 0.5F;
  const float l = luminance(p);
  const float hi = clamp01((l - 0.5F) * 2.0F), lo = clamp01((0.5F - l) * 2.0F);
  const float lift = t.highlights * hi * 0.45F + t.shadows * lo * 0.45F + t.whites * hi * 0.2F + t.blacks * lo * 0.2F;
  p.r += lift; p.g += lift; p.b += lift;
  p.r = (p.r - 0.5F) * (1.0F + t.local) + 0.5F;
  p.g = (p.g - 0.5F) * (1.0F + t.local) + 0.5F;
  p.b = (p.b - 0.5F) * (1.0F + t.local) + 0.5F;
  const float saturation = 1.0F + t.saturation + t.vibrance * (1.0F - std::max({p.r, p.g, p.b}));
  const float gray = luminance(p);
  p.r = gray + (p.r - gray) * saturation; p.g = gray + (p.g - gray) * saturation; p.b = gray + (p.b - gray) * saturation;
  return p;
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
float hue_channel(float p, float q, float t) {
  if (t < 0.0F) t += 1.0F;
  if (t > 1.0F) t -= 1.0F;
  if (t < 1.0F / 6.0F) return p + (q - p) * 6.0F * t;
  if (t < 0.5F) return q;
  if (t < 2.0F / 3.0F) return p + (q - p) * (2.0F / 3.0F - t) * 6.0F;
  return p;
}
Pixel from_hsl(Hsl c, float alpha) {
  if (c.s <= 0.0F) return {c.l, c.l, c.l, alpha};
  const float q = c.l < 0.5F ? c.l * (1.0F + c.s) : c.l + c.s - c.l * c.s;
  const float p = 2.0F * c.l - q;
  const float h = c.h / 360.0F;
  return {hue_channel(p, q, h + 1.0F / 3.0F), hue_channel(p, q, h), hue_channel(p, q, h - 1.0F / 3.0F), alpha};
}

// The eight hue bands of the HSL and black-and-white panels, centred on these
// hues.  A hue belongs to its two neighbouring bands with linear weights, so
// the weights always sum to 1 (no band edge shows as a seam).
constexpr float kBandHue[8] = {0.0F, 30.0F, 60.0F, 120.0F, 180.0F, 240.0F, 270.0F, 300.0F};
constexpr const char* kBandName[8] = {"red", "orange", "yellow", "green", "aqua", "blue", "purple", "magenta"};
void band_weights(float hue, float weights[8]) {
  for (int i = 0; i < 8; ++i) weights[i] = 0.0F;
  for (int i = 0; i < 8; ++i) {
    const float start = kBandHue[i], end = i == 7 ? 360.0F : kBandHue[i + 1];
    if (hue >= start && hue < end) {
      const float t = (hue - start) / (end - start);
      weights[i] = 1.0F - t;
      weights[(i + 1) % 8] = t;
      return;
    }
  }
  weights[0] = 1.0F;
}

struct ColorGroups {
  bool levels = false;
  float in_black = 0, in_white = 1, gamma = 1, out_black = 0, out_white = 1;
  bool hsl = false;
  float hue[8]{}, saturation[8]{}, lightness[8]{};
  bool monochrome = false;
  float mix[8]{};
  bool selective = false, absolute = false;
  float selective_ink[9][4]{};  // target x (cyan, magenta, yellow, black), -1..1
  bool grading = false;
  float zone_hue[4]{}, zone_saturation[4]{}, zone_lightness[4]{}, blending = 0.5F;
  bool editor_offsets = false;
};

ColorGroups color_groups(const Values& v) {
  const char* cap = "color.global";
  ColorGroups g;
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
  for (int i = 0; i < 8; ++i) {
    const std::string band = kBandName[i];
    g.hue[i] = param(v, cap, "hsl_hue_" + band) / 100.0F;
    g.saturation[i] = param(v, cap, "hsl_saturation_" + band) / 100.0F;
    g.lightness[i] = param(v, cap, "hsl_luminance_" + band) / 100.0F;
    g.mix[i] = param(v, cap, "bw_mix_" + band) / 100.0F;
    g.hsl = g.hsl || g.hue[i] != 0.0F || g.saturation[i] != 0.0F || g.lightness[i] != 0.0F;
  }
  g.monochrome = choice_is(v, cap, "profile", "monochrome");
  const char* targets[9] = {"red", "yellow", "green", "cyan", "blue", "magenta", "white", "neutral", "black"};
  const char* inks[4] = {"cyan", "magenta", "yellow", "black"};
  for (int t = 0; t < 9; ++t) {
    for (int k = 0; k < 4; ++k) {
      g.selective_ink[t][k] = param(v, cap, std::string("selective_") + targets[t] + "_" + inks[k]) / 100.0F;
      g.selective = g.selective || g.selective_ink[t][k] != 0.0F;
    }
  }
  g.absolute = choice_is(v, cap, "selective_mode", "absolute");
  const char* zones[4] = {"shadows", "midtones", "highlights", "global"};
  for (int z = 0; z < 4; ++z) {
    const std::string zone = zones[z];
    g.zone_hue[z] = param(v, cap, "grading_" + zone + "_hue");
    g.zone_saturation[z] = param(v, cap, "grading_" + zone + "_saturation") / 100.0F;
    g.zone_lightness[z] = param(v, cap, "grading_" + zone + "_luminance") / 100.0F;
    g.grading = g.grading || g.zone_saturation[z] != 0.0F || g.zone_lightness[z] != 0.0F;
  }
  g.blending = param(v, cap, "grading_blending") / 100.0F;
  for (const char* item : {"hue", "saturation", "luminance"}) {
    g.editor_offsets = g.editor_offsets || param(v, cap, std::string("color_editor_") + item + "_shift") != 0.0F;
  }
  return g;
}

Pixel apply_levels(Pixel p, const ColorGroups& g) {
  const float span = std::max(g.in_white - g.in_black, 1.0F / 255.0F);
  const auto map = [&](float c) {
    const float x = std::pow(clamp01((c - g.in_black) / span), 1.0F / g.gamma);
    return g.out_black + x * (g.out_white - g.out_black);
  };
  return {map(p.r), map(p.g), map(p.b), p.a};
}

// HSL: per band, hue +-100 turns the hue by +-30 degrees (half the distance
// between the primary bands), saturation scales chroma by 1 + amount (-100
// removes it), luminance moves lightness toward white or black by up to half
// the headroom, scaled by the pixel's saturation so greys never move.
Pixel apply_hsl(Pixel p, const ColorGroups& g) {
  Hsl c = to_hsl(p);
  if (c.s <= 0.0F) return p;
  float w[8];
  band_weights(c.h, w);
  float hue = 0, saturation = 0, lightness = 0;
  for (int i = 0; i < 8; ++i) {
    hue += w[i] * g.hue[i];
    saturation += w[i] * g.saturation[i];
    lightness += w[i] * g.lightness[i];
  }
  const float s0 = c.s;
  c.h = std::fmod(c.h + hue * 30.0F + 360.0F, 360.0F);
  c.s = clamp01(c.s * (1.0F + saturation));
  c.l = clamp01(c.l + lightness * 0.5F * s0 * (lightness > 0.0F ? 1.0F - c.l : c.l));
  return from_hsl(c, p.a);
}

// Black-and-white profile: Rec. 709 luminance (the same weights as
// luminance()), then each band's mix slider brightens or darkens pixels of
// that hue in proportion to their chroma, so neutral pixels keep their grey.
Pixel apply_monochrome(Pixel p, const ColorGroups& g) {
  float grey = luminance(p);
  const float chroma = std::max({p.r, p.g, p.b}) - std::min({p.r, p.g, p.b});
  if (chroma > 0.0F) {
    float w[8];
    band_weights(to_hsl(p).h, w);
    float mix_amount = 0;
    for (int i = 0; i < 8; ++i) mix_amount += w[i] * g.mix[i];
    grey = clamp01(grey + mix_amount * chroma * 0.5F);
  }
  return {grey, grey, grey, p.a};
}

// Selective color, in the shape of the CMYK "selective color" adjustment.
// How much a pixel belongs to each target: colour targets by how far their
// primary/secondary leads (max - mid for red/green/blue when that channel is
// the largest, mid - min for cyan/magenta/yellow when its complement is the
// smallest); white above mid grey, black below it, neutral by closeness of
// both extremes to mid grey.  Each channel's ink is 1 - value: cyan acts on
// red, magenta on green, yellow on blue.  Relative scales the ink already
// there, absolute adds to it; black adds (or removes) ink in all channels.
Pixel apply_selective(Pixel p, const ColorGroups& g) {
  const float r = p.r, gr = p.g, b = p.b;
  const float high = std::max({r, gr, b}), low = std::min({r, gr, b});
  const float mid = r + gr + b - high - low;
  float weight[9] = {
      r == high ? high - mid : 0.0F,   // red
      b == low ? mid - low : 0.0F,     // yellow
      gr == high ? high - mid : 0.0F,  // green
      r == low ? mid - low : 0.0F,     // cyan
      b == high ? high - mid : 0.0F,   // blue
      gr == low ? mid - low : 0.0F,    // magenta
      low > 0.5F ? (low - 0.5F) * 2.0F : 0.0F,
      std::max(0.0F, 1.0F - (std::abs(high - 0.5F) + std::abs(low - 0.5F))),
      high < 0.5F ? (0.5F - high) * 2.0F : 0.0F,
  };
  float channel[3] = {r, gr, b};
  float out[3];
  for (int c = 0; c < 3; ++c) {
    const float ink = 1.0F - channel[c];
    float change = 0;
    for (int t = 0; t < 9; ++t) {
      if (weight[t] <= 0.0F) continue;
      const float s = g.selective_ink[t][c], k = g.selective_ink[t][3];
      float next = clamp01(g.absolute ? ink + s : ink * (1.0F + s));
      next += k * (k > 0.0F ? 1.0F - next : next);
      change += weight[t] * (next - ink);
    }
    out[c] = clamp01(channel[c] - change);
  }
  return {out[0], out[1], out[2], p.a};
}

// Color grading: shadows, midtones and highlights are luminance zones centred
// on 0, 0.5 and 1 with triangular weights; blending widens them from radius
// 0.5 (blending 0, zones meet at their neighbours' centres) to 1 (blending
// 100).  Global applies everywhere.  Each zone adds a chroma tint in its hue
// (the hue's colour with its own luminance removed, so the tint does not
// brighten) scaled by saturation, and shifts lightness by up to a quarter.
Pixel apply_grading(Pixel p, const ColorGroups& g) {
  const float l = luminance(p);
  const float radius = 0.5F + 0.5F * g.blending;
  const float centre[3] = {0.0F, 0.5F, 1.0F};
  for (int z = 0; z < 4; ++z) {
    const float w = z == 3 ? 1.0F : std::max(0.0F, 1.0F - std::abs(l - centre[z]) / radius);
    if (w <= 0.0F || (g.zone_saturation[z] == 0.0F && g.zone_lightness[z] == 0.0F)) continue;
    const Pixel hue = from_hsl({g.zone_hue[z], 1.0F, 0.5F}, 1.0F);
    const float hue_l = luminance(hue);
    const float tint = w * g.zone_saturation[z] * 0.25F;
    const float lift = w * g.zone_lightness[z] * 0.25F;
    p.r += tint * (hue.r - hue_l) + lift;
    p.g += tint * (hue.g - hue_l) + lift;
    p.b += tint * (hue.b - hue_l) + lift;
  }
  return p;
}

Pixel apply_color_groups(Pixel p, const ColorGroups& g) {
  if (g.levels) p = apply_levels(p, g);
  if (g.monochrome) {
    p = apply_monochrome(p, g);
  } else if (g.hsl) {
    p = apply_hsl(p, g);
  }
  if (g.selective) p = apply_selective(p, g);
  if (g.grading) p = apply_grading(p, g);
  return p;
}

// Grain (detail).  Value noise on a lattice whose cell follows the image size
// (so a <= 4000 px preview and the full-resolution export look alike), mixed
// with per-pixel noise by roughness; colour mixes monochrome noise toward
// independent per-channel noise.  Shadows/midtones/highlights weight it by
// luminance zone (triangular, radius 0.5).  Deterministic per position: the
// same frame renders the same grain every time.
struct Grain {
  bool on = false;
  float amount = 0, shadows = 0, midtones = 0, highlights = 0, size = 0, roughness = 0, colour = 0;
  float cell = 1;
};
Grain grain_of(const Values& v, std::uint32_t width, std::uint32_t height) {
  const char* cap = "detail";
  Grain g;
  g.amount = param(v, cap, "grain_amount") / 100.0F;
  g.on = g.amount > 0.0F;
  g.shadows = param(v, cap, "grain_shadows") / 100.0F;
  g.midtones = param(v, cap, "grain_midtones") / 100.0F;
  g.highlights = param(v, cap, "grain_highlights") / 100.0F;
  g.size = param(v, cap, "grain_size") / 100.0F;
  g.roughness = param(v, cap, "grain_roughness") / 100.0F;
  g.colour = param(v, cap, "grain_color") / 100.0F;
  g.cell = std::max(1.0F, static_cast<float>(std::min(width, height)) * (0.0008F + 0.006F * g.size));
  return g;
}
float hash01(std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
  std::uint32_t h = x * 0x8da6b343U ^ y * 0xd8163841U ^ channel * 0xcb1ab31fU;
  h ^= h >> 16;
  h *= 0x7feb352dU;
  h ^= h >> 15;
  h *= 0x846ca68bU;
  h ^= h >> 16;
  return static_cast<float>(h & 0xFFFFFFU) / 16777216.0F;
}
float value_noise(float x, float y, std::uint32_t channel) {
  const float fx = std::floor(x), fy = std::floor(y);
  const auto ix = static_cast<std::uint32_t>(static_cast<std::int64_t>(fx));
  const auto iy = static_cast<std::uint32_t>(static_cast<std::int64_t>(fy));
  float tx = x - fx, ty = y - fy;
  tx = tx * tx * (3.0F - 2.0F * tx);
  ty = ty * ty * (3.0F - 2.0F * ty);
  const float top = hash01(ix, iy, channel) + (hash01(ix + 1, iy, channel) - hash01(ix, iy, channel)) * tx;
  const float bottom =
      hash01(ix, iy + 1, channel) + (hash01(ix + 1, iy + 1, channel) - hash01(ix, iy + 1, channel)) * tx;
  return top + (bottom - top) * ty;
}
Pixel apply_grain(Pixel p, std::uint32_t x, std::uint32_t y, const Grain& g) {
  const float l = luminance(p);
  const float zone = std::max(0.0F, 1.0F - l / 0.5F) * g.shadows +
                     std::max(0.0F, 1.0F - std::abs(l - 0.5F) / 0.5F) * g.midtones +
                     std::max(0.0F, 1.0F - (1.0F - l) / 0.5F) * g.highlights;
  const float strength = g.amount * zone * 0.5F;
  if (strength <= 0.0F) return p;
  const auto noise = [&](std::uint32_t channel) {
    const float smooth = value_noise(static_cast<float>(x) / g.cell, static_cast<float>(y) / g.cell, channel);
    return smooth + (hash01(x, y, channel + 8U) - smooth) * g.roughness - 0.5F;
  };
  const float mono = noise(0);
  p.r += strength * (mono + (noise(1) - mono) * g.colour);
  p.g += strength * (mono + (noise(2) - mono) * g.colour);
  p.b += strength * (mono + (noise(3) - mono) * g.colour);
  return p;
}

enum class Blend { Normal, Multiply, Screen };
Blend blend_of(const std::string& mode) {
  if (mode == "multiply") return Blend::Multiply;
  if (mode == "screen") return Blend::Screen;
  return Blend::Normal;
}
Pixel composite(Pixel base, Pixel top, float opacity, Blend mode) {
  opacity = clamp01(opacity) * top.a;
  Pixel mixed = top;
  if (mode == Blend::Multiply) mixed = {base.r * top.r, base.g * top.g, base.b * top.b, top.a};
  if (mode == Blend::Screen) mixed = {1.0F - (1.0F - base.r) * (1.0F - top.r), 1.0F - (1.0F - base.g) * (1.0F - top.g), 1.0F - (1.0F - base.b) * (1.0F - top.b), top.a};
  return mix(base, mixed, opacity);
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
Pixel lut_sample(const Lut3DView& lut, Pixel input) {
  const float coordinate[3] = {clamp01(input.r) * (lut.size - 1), clamp01(input.g) * (lut.size - 1), clamp01(input.b) * (lut.size - 1)};
  const std::uint32_t base[3] = {static_cast<std::uint32_t>(coordinate[0]), static_cast<std::uint32_t>(coordinate[1]), static_cast<std::uint32_t>(coordinate[2])};
  const float fraction[3] = {coordinate[0] - base[0], coordinate[1] - base[1], coordinate[2] - base[2]};
  auto sample = [&](std::uint32_t r, std::uint32_t g, std::uint32_t b) {
    const auto index = (static_cast<std::size_t>(b) * lut.size * lut.size + static_cast<std::size_t>(g) * lut.size + r) * 3;
    return Pixel{lut.rgb[index], lut.rgb[index + 1], lut.rgb[index + 2], input.a};
  };
  const std::uint32_t hi[3] = {std::min(base[0] + 1, lut.size - 1), std::min(base[1] + 1, lut.size - 1), std::min(base[2] + 1, lut.size - 1)};
  Pixel c000 = sample(base[0], base[1], base[2]);
  Pixel c100 = sample(hi[0], base[1], base[2]);
  Pixel c010 = sample(base[0], hi[1], base[2]);
  Pixel c110 = sample(hi[0], hi[1], base[2]);
  Pixel c001 = sample(base[0], base[1], hi[2]);
  Pixel c101 = sample(hi[0], base[1], hi[2]);
  Pixel c011 = sample(base[0], hi[1], hi[2]);
  Pixel c111 = sample(hi[0], hi[1], hi[2]);
  const Pixel x00 = mix(c000, c100, fraction[0]);
  const Pixel x10 = mix(c010, c110, fraction[0]);
  const Pixel x01 = mix(c001, c101, fraction[0]);
  const Pixel x11 = mix(c011, c111, fraction[0]);
  const Pixel y0 = mix(x00, x10, fraction[1]);
  const Pixel y1 = mix(x01, x11, fraction[1]);
  return mix(y0, y1, fraction[2]);
}

}  // namespace

namespace {

// Channels with a shader binding and a recorded GPU readback comparison.
bool gpu_formula_supported(const pixcake::render::RenderOperation& operation) {
  if (!operation.enabled || operation.capability_id != "color.global" ||
      operation.blend_mode != "normal" || !operation.mask_ids.empty()) return false;
  static const std::set<std::string_view> channels = {"temperature", "tint", "exposure", "contrast"};
  for (const auto& value : operation.values) {
    if (channels.find(value.channel) == channels.end() && std::abs(value.numeric_value) > 1e-9) return false;
  }
  return true;
}

}  // namespace

PassStatus PassRegistry::classify(const pixcake::render::RenderOperation& operation) {
  if (!operation.enabled) return PassStatus::Disabled;
  if (operation.program.find("filter") != std::string::npos || operation.program.find("profile") != std::string::npos) return PassStatus::MissingResource;
  if (operation.program == "pixcake.color.global") return gpuSupported(operation) ? PassStatus::Implemented : PassStatus::ReferenceOnly;
  if (operation.program == "pixcake.color.local" || operation.program == "pixcake.color.post" || operation.program == "pixcake.color.match" || operation.program == "pixcake.color.style") return PassStatus::ReferenceOnly;
  // detail only registers grain parameters so far (reference formula, no shader).
  if (operation.program == "pixcake.detail") return PassStatus::ReferenceOnly;
  return PassStatus::Unregistered;
}

bool PassRegistry::gpuSupported(const pixcake::render::RenderOperation& operation) {
  // 2026-11-05: temperature, tint, exposure and contrast all passed the
  // reference/readback comparison on native CGL and the Qt desktop platform
  // (test_gpu_backend_native, test_gpu_shader_path); dropping any one channel
  // from the shader makes both tests fail.  The formula gate is therefore also
  // the evidence gate.  A new channel must add its own readback evidence first.
  return gpu_formula_supported(operation);
}

bool RenderExecutor::renderReference(const pixcake::render::RenderDescription& description,
                                     const RgbaImage& input, const RenderInputs& inputs,
                                     RenderResult& result) const {
  result = {};
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
  result.image = input;
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
    Lut3DView lut;
    bool has_lut = false;
    if (status == PassStatus::MissingResource && inputs.resources != nullptr) {
      std::string lut_error;
      if (inputs.resources->resolveLut(operation.program, lut, lut_error) && valid_lut(lut) && lut.revision == description.document_revision) {
        has_lut = true;
        result.statuses.back().status = PassStatus::ReferenceOnly;
        result.statuses.back().reason = "结构化 3D LUT 已解析，由 reference 三线性采样";
        result.diagnostics.push_back({operation.capability_id, result.statuses.back().reason, false});
      } else {
        result.diagnostics.push_back({operation.capability_id, lut_error.empty() ? record.reason : lut_error, false});
        continue;
      }
    }
    if (status == PassStatus::Unregistered || (status == PassStatus::MissingResource && !has_lut)) {
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
    std::vector<const RenderMask*> masks;
    bool missing = false;
    for (const auto& id : operation.mask_ids) {
      const auto* mask = mask_for(inputs, id);
      if (mask == nullptr || mask->width != input.width || mask->height != input.height || mask->alpha.size() < static_cast<std::size_t>(input.width) * input.height) { missing = true; break; }
      masks.push_back(mask);
    }
    if (missing) { result.statuses.back().status = PassStatus::MissingResource; result.statuses.back().reason = "蒙版缺失或尺寸不匹配，跳过操作"; result.diagnostics.push_back({operation.capability_id, "蒙版缺失或尺寸不匹配，跳过操作", false}); continue; }
    std::map<std::string, float> values;
    for (const auto& value : operation.values) values[value.channel] = static_cast<float>(value.numeric_value);
    const bool is_color = operation.stage == "color" || operation.program.find("color") != std::string::npos;
    const bool is_tone = operation.stage == "tone" || operation.capability_id == "color.global" || operation.capability_id == "color.local" || operation.capability_id == "color.post";
    const bool is_detail = operation.capability_id == "detail";
    if (!is_color && !is_tone && !is_detail && !has_lut) continue;
    // Sub-groups of the global colour tool and grain, read once per operation.
    const bool has_groups = !has_lut && operation.capability_id == "color.global";
    const ColorGroups groups = has_groups ? color_groups(values) : ColorGroups{};
    if (groups.editor_offsets) {
      // The colour editor's target colour (eyedropper, range) is not registered,
      // so its offsets have nothing to act on yet.
      result.diagnostics.push_back({operation.capability_id, "色彩编辑器还没有目标颜色参数，偏移暂不绘制", false});
    }
    const Grain grain = is_detail ? grain_of(values, input.width, input.height) : Grain{};
    if (is_detail && !grain.on) continue;
    const ToneColor tone_color = tone_color_of(values);
    const float lut_amount = values.find("amount") == values.end() ? 1.0F : clamp01(get(values, "amount") / 100.0F);
    const Blend blend = blend_of(operation.blend_mode);
    RgbaImage& frame = result.image;
    for_rows(input.width, input.height, [&](std::uint32_t first, std::uint32_t end) {
      for (std::uint32_t y = first; y < end; ++y) for (std::uint32_t x = 0; x < input.width; ++x) {
        const Pixel before = read(frame, x, y);
        Pixel after = before;
        if (has_lut) after = mix(before, lut_sample(lut, before), lut_amount);
        if (!has_lut && is_color) after = color(after, tone_color);
        if (!has_lut && is_tone) after = tone(after, tone_color);
        if (has_groups) after = apply_color_groups(after, groups);
        if (is_detail) after = apply_grain(after, x, y, grain);
        float weight = 1.0F;
        for (const auto* mask : masks) weight *= mask->alpha[static_cast<std::size_t>(y) * input.width + x] / 255.0F;
        write(frame, x, y, composite(before, after, weight, blend));
      }
    });
  }
  auto layers = inputs.layers;
  std::sort(layers.begin(), layers.end(), [](const RenderLayer& a, const RenderLayer& b) { return a.order < b.order; });
  for (const auto& layer : layers) {
    const auto layer_stride = layer.image.row_stride == 0 ? layer.image.width * 4 : layer.image.row_stride;
    const std::string layer_id = "layer:" + std::to_string(layer.order);
    if (!layer.visible) { result.statuses.push_back({layer_id, "layer", PassStatus::Disabled, "图层已隐藏"}); continue; }
    if (layer.image.width != input.width || layer.image.height != input.height || layer_stride < input.width * 4 || layer.image.pixels.size() < static_cast<std::size_t>(layer_stride) * layer.image.height) {
      result.statuses.push_back({layer_id, "layer", PassStatus::MissingResource, "图层尺寸或行距无效，跳过图层"});
      result.diagnostics.push_back({layer_id, "图层尺寸或行距无效，跳过图层", false});
      continue;
    }
    if (layer.blend_mode != "normal" && layer.blend_mode != "multiply" && layer.blend_mode != "screen") {
      result.statuses.push_back({layer_id, "layer", PassStatus::InvalidInput, "未知图层混合模式，跳过图层"});
      result.diagnostics.push_back({layer_id, "未知图层混合模式，跳过图层", false});
      continue;
    }
    const auto* mask = layer.mask_id.empty() ? nullptr : mask_for(inputs, layer.mask_id);
    if (!layer.mask_id.empty() && (mask == nullptr || mask->width != input.width || mask->height != input.height || mask->alpha.size() < static_cast<std::size_t>(input.width) * input.height)) { result.statuses.push_back({layer_id, "layer", PassStatus::MissingResource, "图层蒙版缺失或尺寸不匹配，跳过图层"}); result.diagnostics.push_back({layer_id, "图层蒙版缺失或尺寸不匹配，跳过图层", false}); continue; }
    result.statuses.push_back({layer_id, "layer", PassStatus::Implemented, {}});
    const Blend layer_blend = blend_of(layer.blend_mode);
    RgbaImage& frame = result.image;
    for_rows(input.width, input.height, [&](std::uint32_t first, std::uint32_t end) {
      for (std::uint32_t y = first; y < end; ++y) for (std::uint32_t x = 0; x < input.width; ++x) {
        const float amount = layer.opacity * (mask == nullptr ? 1.0F : mask->alpha[static_cast<std::size_t>(y) * input.width + x] / 255.0F);
        write(frame, x, y, composite(read(frame, x, y), read(layer.image, x, y), amount, layer_blend));
      }
    });
  }
  result.success = true;
  return true;
}

bool RenderExecutor::execute(gl::Context& context, const pixcake::render::RenderDescription& description,
                             const RgbaImage& input, const RenderInputs& inputs, RenderResult& result,
                             std::string& error) const {
  if (!context.valid() || !context.makeCurrent()) { error = context.lastError(); return false; }
  bool gpu_supported = description.operations.size() == 1 && inputs.layers.empty() && inputs.masks.empty() && input.row_stride == input.width * 4;
  std::map<std::string, float> values;
  for (const auto& operation : description.operations) {
    if (!gpu_formula_supported(operation)) { gpu_supported = false; continue; }
    for (const auto& value : operation.values) {
      values[value.channel] = static_cast<float>(value.numeric_value);
    }
  }
  if (!gpu_supported) {
    const bool ok = renderReference(description, input, inputs, result);
    result.reference_fallback = true;
    const std::string reason = (!inputs.layers.empty() || !inputs.masks.empty())
                                   ? "图层或蒙版的 GPU 合成路径尚未接入，已回退 reference"
                                   : "当前绘制描述包含 GPU 最小路径未覆盖的能力，已回退 reference";
    result.diagnostics.push_back({"", reason, false});
    context.doneCurrent();
    if (!ok) error = "reference render failed";
    return ok;
  }

  bool ok = false;
  {
    auto shader = context.createShader();
    auto fragment = context.createShader();
    auto program = context.createProgram();
    auto source = context.createTexture2D();
    auto target = context.createTexture2D();
    auto framebuffer = context.createFramebuffer();
    const char* vertex_source = "#version 330 core\nout vec2 uv;\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2.0-1.0,0,1);}";
    // PP-WB-001, PP-EXP-001, and PP-CONTRAST-001: the shader receives the
    // same linear working buffer and the same CPU-derived Bradford matrix,
    // EV multiplier, and affine contrast amount.
    const char* fragment_source = "#version 330 core\nin vec2 uv;out vec4 color;uniform sampler2D source;uniform vec4 adjust;uniform vec4 wb0;uniform vec4 wb1;uniform vec4 wb2;\nvoid main(){vec4 p=texture(source,uv);vec3 balanced=vec3(dot(wb0.xyz,p.rgb),dot(wb1.xyz,p.rgb),dot(wb2.xyz,p.rgb));balanced*=exp2(adjust.z);balanced=(balanced-0.5)*(1.0+adjust.w/100.0)+0.5;color=clamp(vec4(balanced,p.a),0.0,1.0);} ";
    if (shader.valid() && fragment.valid() && program.valid() && source.valid() && target.valid() && framebuffer.valid()) {
      ok = shader.compile(gl::ShaderStage::Vertex, vertex_source, error) && fragment.compile(gl::ShaderStage::Fragment, fragment_source, error) && program.link(shader, fragment, error) && program.use(error);
      const gl::ImageView view{input.width, input.height, input.row_stride, gl::PixelFormat::Rgba8, reinterpret_cast<const std::byte*>(input.pixels.data()), input.pixels.size()};
      ok = ok && source.upload(view, error) && target.allocate(input.width, input.height, gl::PixelFormat::Rgba8, error) && framebuffer.attachColor(target, error) && framebuffer.complete(error) && framebuffer.bind(error) && context.setViewport(input.width, input.height, error) && source.bind(0, error);
      const Mat3 white_balance = white_balance_matrix(values["temperature"], values["tint"]);
      ok = ok && program.setTextureUnit("source", 0, error) &&
           program.setVec4("adjust", {0.0F, 0.0F, values["exposure"], values["contrast"]}, error) &&
           program.setVec4("wb0", {white_balance.m[0][0], white_balance.m[0][1], white_balance.m[0][2], 0.0F}, error) &&
           program.setVec4("wb1", {white_balance.m[1][0], white_balance.m[1][1], white_balance.m[1][2], 0.0F}, error) &&
           program.setVec4("wb2", {white_balance.m[2][0], white_balance.m[2][1], white_balance.m[2][2], 0.0F}, error) &&
           context.drawTriangles(3, error);
      std::vector<std::byte> pixels;
      ok = ok && context.readPixels(input.width, input.height, pixels, error);
      if (ok) {
        result = {};
        result.image = input; result.image.revision = description.document_revision; result.image.pixels.resize(pixels.size());
        std::transform(pixels.begin(), pixels.end(), result.image.pixels.begin(), [](std::byte value) { return static_cast<std::uint8_t>(value); });
        result.success = true;
      }
    } else error = "GPU resources unavailable";
  }
  context.doneCurrent();
  if (!ok && error.empty()) error = "GPU pass failed";
  return ok;
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
