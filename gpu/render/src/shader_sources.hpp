#pragma once

// GLSL for the GPU path of RenderExecutor::execute.  Each fragment shader is a
// line-for-line port of the reference formula in render_executor.cpp (same
// order of operations, same constants); the readback tests compare the two.
// Pixels are read with texelFetch (no filtering) and written to RGBA8, which
// quantises between passes exactly as the reference writes 8-bit after every
// operation.  Image row y is gl_FragCoord.y - 0.5: upload and readback both
// keep row 0 first, so the grain hash sees the same coordinates.
//
// A fragment shader is kPrelude followed by one body.  The prelude holds the
// operation's composite (composite() in render_executor.cpp): up to four
// masks packed into the RGBA channels of one texture multiply into the
// weight, then normal, multiply or screen blending by the pixel's alpha.

namespace pixcake::gpu::shaders {

inline constexpr const char* kPrelude = R"(#version 330 core
out vec4 color;
uniform sampler2D source;
uniform sampler2D masks;
uniform vec4 blend0;  // masks used (0..4), blend mode (0 normal, 1 multiply, 2 screen), -, -

float clamp01(float v) { return clamp(v, 0.0, 1.0); }
float luminance(vec3 p) { return 0.2126 * p.r + 0.7152 * p.g + 0.0722 * p.b; }
vec4 composite(vec4 base, vec4 top, float opacity, float mode) {
  opacity = clamp01(opacity) * top.a;
  vec4 mixed = top;
  if (mode > 0.5 && mode < 1.5) mixed = vec4(base.rgb * top.rgb, top.a);
  if (mode > 1.5) mixed = vec4(1.0 - (1.0 - base.rgb) * (1.0 - top.rgb), top.a);
  return base + (mixed - base) * clamp01(opacity);
}
float mask_weight() {
  float weight = 1.0;
  if (blend0.x > 0.5) {
    vec4 m = texelFetch(masks, ivec2(gl_FragCoord.xy), 0);
    weight *= m.r;
    if (blend0.x > 1.5) weight *= m.g;
    if (blend0.x > 2.5) weight *= m.b;
    if (blend0.x > 3.5) weight *= m.a;
  }
  return weight;
}
vec4 finish(vec4 before, vec3 p) {
  return clamp(composite(before, vec4(p, before.a), mask_weight(), blend0.y), 0.0, 1.0);
}
)";

inline constexpr const char* kVertex = R"(#version 330 core
void main() {
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

// The colour and tone steps and the global tool's sub-groups, in the order of
// draw_reference (render_executor.cpp): Camera Raw white balance and exposure,
// AI style tint, Photoshop modern brightness, legacy lift and local contrast,
// Camera Raw Contrast and Presence; then levels, black-and-white or the Camera
// Raw Color Mixer, selective colour, Camera Raw Color Grading.  The Camera Raw
// and brightness functions are line-for-line ports of camera_raw.cpp and
// brightness.cpp (PhotoCraft 885a6ddf27cc58c07faf1722e5c327cd38953657, MIT;
// third_party/photocraft-LICENSES/LICENSE-MIT).  stage0 says which steps run.
inline constexpr const char* kColor = R"(
uniform vec4 stage0;   // colour step, tone step, -, -
uniform vec4 cr0;      // Camera Raw stage 1 gains rgb; w = 1 when it applies
uniform vec4 cr1;      // contrast strength (slider / 100 * 0.6), vibrance, saturation, presence on
uniform vec4 bright0;  // brightness curve: s, e, x0, alpha
uniform vec4 bright1;  // brightness on, raise, hermite, -
uniform vec4 tone0;    // style, -, -, -
uniform vec4 tone1;    // highlights, shadows, whites, blacks (legacy)
uniform vec4 tone2;    // local contrast (legacy), -, -, -
uniform vec4 groups0;  // levels, monochrome, hsl, selective (1 = on)
uniform vec4 groups1;  // grading, absolute selective mode, grading blend, grading balance
uniform vec4 levels0;  // input black, input white, gamma, output black
uniform vec4 levels1;  // output white, -, -, -
uniform vec4 hue_a;    // Color Mixer sliders per band (red, orange, yellow, green)
uniform vec4 hue_b;    //                              (aqua, blue, purple, magenta)
uniform vec4 sat_a;
uniform vec4 sat_b;
uniform vec4 light_a;
uniform vec4 light_b;
uniform vec4 mix_a;    // black-and-white mix per band
uniform vec4 mix_b;
uniform vec4 ink0;     // selective colour per target: cyan, magenta, yellow, black
uniform vec4 ink1;
uniform vec4 ink2;
uniform vec4 ink3;
uniform vec4 ink4;
uniform vec4 ink5;
uniform vec4 ink6;
uniform vec4 ink7;
uniform vec4 ink8;
uniform vec4 zone_light;  // grading wheels' luminance: shadows, midtones, highlights, global
uniform vec4 zone_tint0;  // rgb: each wheel's resolved tint (camera_raw::Grade)
uniform vec4 zone_tint1;
uniform vec4 zone_tint2;
uniform vec4 zone_tint3;

const float kTau = 6.28318530717958647692;
const float kKnee = 126.0 / 255.0;
const float kCrBands[8] = float[8](0.0, 30.0, 60.0, 120.0, 180.0, 240.0, 270.0, 300.0);

float max3(vec3 p) { return max(max(p.r, p.g), p.b); }
float min3(vec3 p) { return min(min(p.r, p.g), p.b); }

vec3 to_hsl(vec3 p) {
  float high = max3(p), low = min3(p);
  vec3 c = vec3(0.0, 0.0, (high + low) / 2.0);
  float d = high - low;
  if (d < 1e-6) return c;
  c.y = c.z > 0.5 ? d / (2.0 - high - low) : d / (high + low);
  if (high == p.r) {
    c.x = (p.g - p.b) / d + (p.g < p.b ? 6.0 : 0.0);
  } else if (high == p.g) {
    c.x = (p.b - p.r) / d + 2.0;
  } else {
    c.x = (p.r - p.g) / d + 4.0;
  }
  c.x *= 60.0;
  return c;
}
const float kBandHue[8] = float[8](0.0, 30.0, 60.0, 120.0, 180.0, 240.0, 270.0, 300.0);
void band_weights(float hue, out float w[8]) {
  for (int i = 0; i < 8; ++i) w[i] = 0.0;
  for (int i = 0; i < 8; ++i) {
    float start = kBandHue[i];
    float stop = i == 7 ? 360.0 : kBandHue[min(i + 1, 7)];
    if (hue >= start && hue < stop) {
      float t = (hue - start) / (stop - start);
      w[i] = 1.0 - t;
      w[(i + 1) % 8] = t;
      return;
    }
  }
  w[0] = 1.0;
}
float band_sum(float w[8], vec4 a, vec4 b) {
  return w[0] * a.x + w[1] * a.y + w[2] * a.z + w[3] * a.w + w[4] * b.x + w[5] * b.y + w[6] * b.z + w[7] * b.w;
}

// ---- PhotoCraft Camera Raw (camera_raw.cpp) and modern brightness (brightness.cpp)
float rem_euclid(float x, float m) { return x - m * floor(x / m); }
float srgb_to_linear(float v) { return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4); }
float linear_to_srgb(float v) { return v <= 0.0031308 ? v * 12.92 : 1.055 * pow(v, 1.0 / 2.4) - 0.055; }
vec3 rgb_to_hsv(vec3 c) {
  float mx = max3(c), mn = min3(c), d = mx - mn;
  float h = 0.0;
  if (d <= 1e-9) {
    h = 0.0;
  } else if (mx == c.r) {
    h = 60.0 * rem_euclid((c.g - c.b) / d, 6.0);
  } else if (mx == c.g) {
    h = 60.0 * ((c.b - c.r) / d + 2.0);
  } else {
    h = 60.0 * ((c.r - c.g) / d + 4.0);
  }
  return vec3(h, mx > 1e-9 ? d / mx : 0.0, mx);
}
vec3 hsv_to_rgb(float h, float s, float v) {
  float hh = rem_euclid(h, 360.0) / 60.0;
  float c = v * s;
  float x = c * (1.0 - abs(mod(hh, 2.0) - 1.0));
  int sector = clamp(int(floor(hh)), 0, 5);
  vec3 rgb = vec3(c, 0.0, x);
  if (sector == 0) rgb = vec3(c, x, 0.0);
  else if (sector == 1) rgb = vec3(x, c, 0.0);
  else if (sector == 2) rgb = vec3(0.0, c, x);
  else if (sector == 3) rgb = vec3(0.0, x, c);
  else if (sector == 4) rgb = vec3(x, 0.0, c);
  return rgb + (v - c);
}
// Stage 1: gains in linear light.
vec3 cr_balance(vec3 p) {
  return vec3(linear_to_srgb(max(srgb_to_linear(p.r) * cr0.r, 0.0)), linear_to_srgb(max(srgb_to_linear(p.g) * cr0.g, 0.0)),
              linear_to_srgb(max(srgb_to_linear(p.b) * cr0.b, 0.0)));
}
float bright_raised(float x) {
  float s = bright0.x, e = bright0.y, x0 = bright0.z, alpha = bright0.w;
  float y0 = kKnee;
  if (x <= x0) return min(y0, s * x);
  float h = max(1.0 - x0, 1e-6);
  float t = clamp((x - x0) / h, 0.0, 1.0);
  float y;
  if (bright1.z > 0.5) {
    float t2 = t * t, t3 = t * t * t;
    y = (2.0 * t3 - 3.0 * t2 + 1.0) * y0 + (t3 - 2.0 * t2 + t) * h * s + (-2.0 * t3 + 3.0 * t2) + (t3 - t2) * h * e;
  } else {
    y = 1.0 - (1.0 - y0) * pow(1.0 - t, alpha);
  }
  return clamp(y, 0.0, 1.0);
}
// The CPU samples modern_lut; here the curve itself: raised directly, lowered as the inverse
// of the raised curve by 32 bisection steps (test_brightness_port checks the two agree).
float modern_brightness(float v) {
  v = clamp(v, 0.0, 1.0);
  if (bright1.y > 0.5) return bright_raised(v);
  if (v <= 0.0 || v >= 1.0) return v;
  float lo = 0.0, hi = 1.0;
  for (int i = 0; i < 32; ++i) {
    float mid = 0.5 * (lo + hi);
    if (bright_raised(mid) < v) lo = mid; else hi = mid;
  }
  return 0.5 * (lo + hi);
}
// Stage 3, Contrast only: an S-curve on luminance, colour ratios kept.
vec3 cr_contrast(vec3 c) {
  float l0 = max(luminance(c), 0.0);
  float lc = min(l0, 1.0);
  float lv = lc - cr1.x * sin(kTau * clamp(lc, 0.0, 1.0)) / kTau;
  lv = max(lv, 0.0) + (l0 - lc);
  return l0 > 1e-5 ? c * (lv / l0) : c + (lv - l0);
}
// Stage 4, Presence.
vec3 cr_presence(vec3 c) {
  float l = luminance(c);
  float mx = max3(c), mn = min3(c);
  float sat = mx > 1e-6 ? (mx - mn) / mx : 0.0;
  float hue = rgb_to_hsv(c).x;
  float skin = clamp(1.0 - abs(hue - 25.0) / 25.0, 0.0, 1.0) * 0.5;
  float k = 1.0 + cr1.z / 100.0 + cr1.y / 100.0 * (1.0 - sat) * (1.0 - skin);
  return l + (c - l) * max(k, 0.0);
}

vec3 colour_step(vec3 p) {
  if (cr0.w > 0.5) p = cr_balance(p);
  p.r += tone0.x * 0.04;
  p.g += tone0.x * 0.01;
  p.b -= tone0.x * 0.03;
  return p;
}
vec3 tone(vec3 p) {
  if (bright1.x > 0.5) p = vec3(modern_brightness(p.r), modern_brightness(p.g), modern_brightness(p.b));
  if (tone1 != vec4(0.0)) {
    float l = luminance(p);
    float hi = clamp01((l - 0.5) * 2.0), lo = clamp01((0.5 - l) * 2.0);
    p += tone1.x * hi * 0.45 + tone1.y * lo * 0.45 + tone1.z * hi * 0.2 + tone1.w * lo * 0.2;
  }
  if (tone2.x != 0.0) p = (p - 0.5) * (1.0 + tone2.x) + 0.5;
  if (cr1.x != 0.0) p = cr_contrast(p);
  if (cr1.w > 0.5) p = cr_presence(p);
  return p;
}
vec3 levels(vec3 p) {
  float span = max(levels0.y - levels0.x, 1.0 / 255.0);
  vec3 x = pow(clamp((p - levels0.x) / span, 0.0, 1.0), vec3(1.0 / levels0.z));
  return levels0.w + x * (levels1.x - levels0.w);
}
// Stage 6, Color Mixer; greys come back unchanged.
float band_of(vec4 a, vec4 b, int k) { return k < 4 ? a[k] : b[k - 4]; }
vec3 hsl(vec3 c) {
  vec3 hsv = rgb_to_hsv(max(c, vec3(0.0)));
  if (hsv.y <= 1e-4) return c;
  float dh = 0.0, ds = 0.0, dl = 0.0;
  for (int k = 0; k < 8; ++k) {
    float centre = kCrBands[k], prev = kCrBands[(k + 7) % 8], next = kCrBands[(k + 1) % 8];
    float diff = mod(hsv.x - centre + 540.0, 360.0) - 180.0;
    float span = diff >= 0.0 ? rem_euclid(next - centre, 360.0) : rem_euclid(centre - prev, 360.0);
    float w = max(1.0 - abs(diff) / max(span, 1e-3), 0.0);
    dh += w * band_of(hue_a, hue_b, k);
    ds += w * band_of(sat_a, sat_b, k);
    dl += w * band_of(light_a, light_b, k);
  }
  float nh = hsv.x + dh / 100.0 * 30.0;
  float ns = clamp(hsv.y * (1.0 + ds / 100.0), 0.0, 1.0);
  vec3 out_ = hsv_to_rgb(nh, ns, hsv.z);
  float l0 = luminance(c), l1 = luminance(out_);
  if (l1 > 1e-6) out_ = out_ * l0 / l1;
  return out_ * pow(2.0, dl / 100.0 * 1.0 * hsv.y);
}
vec3 monochrome(vec3 p) {
  float grey = luminance(p);
  float chroma = max3(p) - min3(p);
  if (chroma > 0.0) {
    float w[8];
    band_weights(to_hsl(p).x, w);
    grey = clamp01(grey + band_sum(w, mix_a, mix_b) * chroma * 0.5);
  }
  return vec3(grey);
}
vec3 selective(vec3 p) {
  float r = p.r, g = p.g, b = p.b;
  float high = max3(p), low = min3(p);
  float mid = r + g + b - high - low;
  float weight[9] = float[9](r == high ? high - mid : 0.0, b == low ? mid - low : 0.0,
                             g == high ? high - mid : 0.0, r == low ? mid - low : 0.0,
                             b == high ? high - mid : 0.0, g == low ? mid - low : 0.0,
                             low > 0.5 ? (low - 0.5) * 2.0 : 0.0,
                             max(0.0, 1.0 - (abs(high - 0.5) + abs(low - 0.5))),
                             high < 0.5 ? (0.5 - high) * 2.0 : 0.0);
  vec4 inks[9] = vec4[9](ink0, ink1, ink2, ink3, ink4, ink5, ink6, ink7, ink8);
  vec3 out_;
  for (int c = 0; c < 3; ++c) {
    float ink = 1.0 - p[c];
    float change = 0.0;
    for (int t = 0; t < 9; ++t) {
      if (weight[t] <= 0.0) continue;
      float s = inks[t][c], k = inks[t].w;
      float next = clamp01(groups1.y > 0.5 ? ink + s : ink * (1.0 + s));
      next += k * (k > 0.0 ? 1.0 - next : next);
      change += weight[t] * (next - ink);
    }
    out_[c] = clamp01(p[c] - change);
  }
  return out_;
}
// Stage 7, Color Grading: shadows / midtones / highlights / global wheels.
float cr_smoothstep(float e0, float e1, float x) {
  float t = clamp((x - e0) / (e1 - e0), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}
vec3 grading(vec3 c) {
  float l = clamp(luminance(c), 0.0, 1.0);
  float sh = 1.0 - cr_smoothstep(0.0, 0.5 + groups1.w + groups1.z, l);
  float hi = cr_smoothstep(0.5 + groups1.w - groups1.z, 1.0, l);
  float mid = max(1.0 - sh - hi, 0.0);
  vec4 masks = vec4(sh, mid, hi, 1.0);
  vec3 tints[4] = vec3[4](zone_tint0.rgb, zone_tint1.rgb, zone_tint2.rgb, zone_tint3.rgb);
  for (int z = 0; z < 4; ++z) {
    c += masks[z] * tints[z];
    if (zone_light[z] != 0.0) c *= pow(2.0, masks[z] * zone_light[z] / 100.0 * 0.6);
  }
  return c;
}

void main() {
  vec4 before = texelFetch(source, ivec2(gl_FragCoord.xy), 0);
  vec3 p = before.rgb;
  if (stage0.x > 0.5) p = colour_step(p);
  if (stage0.y > 0.5) p = tone(p);
  if (groups0.x > 0.5) p = levels(p);
  if (groups0.y > 0.5) {
    p = monochrome(p);
  } else if (groups0.z > 0.5) {
    p = hsl(p);
  }
  if (groups0.w > 0.5) p = selective(p);
  if (groups1.x > 0.5) p = grading(p);
  color = finish(before, p);
}
)";

// Detail grain (Grain): lattice value noise mixed with per-pixel noise.
inline constexpr const char* kGrain = R"(
uniform vec4 grain0;  // amount, shadows, midtones, highlights
uniform vec4 grain1;  // 1 / cell, roughness, colour, -
uniform vec4 grain2;  // the tile's origin in the frame: x, y

float hash01(uint x, uint y, uint channel) {
  uint h = x * 0x8da6b343u ^ y * 0xd8163841u ^ channel * 0xcb1ab31fu;
  h ^= h >> 16;
  h *= 0x7feb352du;
  h ^= h >> 15;
  h *= 0x846ca68bu;
  h ^= h >> 16;
  return float(h & 0xFFFFFFu) * (1.0 / 16777216.0);
}
float value_noise(float x, float y, uint channel) {
  float fx = floor(x), fy = floor(y);
  uint ix = uint(int(fx)), iy = uint(int(fy));
  float tx = x - fx, ty = y - fy;
  tx = tx * tx * (3.0 - 2.0 * tx);
  ty = ty * ty * (3.0 - 2.0 * ty);
  float top = hash01(ix, iy, channel) + (hash01(ix + 1u, iy, channel) - hash01(ix, iy, channel)) * tx;
  float bottom = hash01(ix, iy + 1u, channel) + (hash01(ix + 1u, iy + 1u, channel) - hash01(ix, iy + 1u, channel)) * tx;
  return top + (bottom - top) * ty;
}
float noise(uint x, uint y, uint channel) {
  float smooth_ = value_noise(float(x) * grain1.x, float(y) * grain1.x, channel);
  return smooth_ + (hash01(x, y, channel + 8u) - smooth_) * grain1.y - 0.5;
}

void main() {
  uint x = uint(gl_FragCoord.x) + uint(grain2.x), y = uint(gl_FragCoord.y) + uint(grain2.y);
  vec4 before = texelFetch(source, ivec2(gl_FragCoord.xy), 0);
  vec3 p = before.rgb;
  float l = luminance(p);
  float zone = max(0.0, 1.0 - l / 0.5) * grain0.y + max(0.0, 1.0 - abs(l - 0.5) / 0.5) * grain0.z +
               max(0.0, 1.0 - (1.0 - l) / 0.5) * grain0.w;
  float strength = grain0.x * zone * 0.5;
  if (strength > 0.0) {
    float mono = noise(x, y, 0u);
    p.r += strength * (mono + (noise(x, y, 1u) - mono) * grain1.z);
    p.g += strength * (mono + (noise(x, y, 2u) - mono) * grain1.z);
    p.b += strength * (mono + (noise(x, y, 3u) - mono) * grain1.z);
  }
  color = finish(before, p);
}
)";

// A layer over the frame (composite_layers): opacity times its mask.
inline constexpr const char* kLayer = R"(
uniform sampler2D layer;
uniform vec4 layer0;  // opacity, mask present, blend mode, -

void main() {
  vec4 before = texelFetch(source, ivec2(gl_FragCoord.xy), 0);
  vec4 top = texelFetch(layer, ivec2(gl_FragCoord.xy), 0);
  float amount = layer0.x * (layer0.y > 0.5 ? texelFetch(masks, ivec2(gl_FragCoord.xy), 0).r : 1.0);
  color = clamp(composite(before, top, amount, layer0.z), 0.0, 1.0);
}
)";

}  // namespace pixcake::gpu::shaders
