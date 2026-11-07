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

// White balance and AI style tint (the colour step), tone, then the colour
// sub-groups of the global tool (ColorGroups): levels, black-and-white or
// HSL, selective colour, colour grading.  stage0 says which steps run, as
// PreparedOperation does for local, post and AI colour.
inline constexpr const char* kColor = R"(
uniform vec4 stage0;   // colour step, tone step, -, -
uniform vec4 wb0;      // white balance matrix rows; wb0.w = 1 when it applies
uniform vec4 wb1;
uniform vec4 wb2;
uniform vec4 tone0;    // style, exposure gain, brightness, contrast amount
uniform vec4 tone1;    // highlights, shadows, whites, blacks
uniform vec4 tone2;    // local contrast, saturation, vibrance, -
uniform vec4 groups0;  // levels, monochrome, hsl, selective (1 = on)
uniform vec4 groups1;  // grading, absolute selective mode, blending, -
uniform vec4 levels0;  // input black, input white, gamma, output black
uniform vec4 levels1;  // output white, -, -, -
uniform vec4 hue_a;    // HSL hue per band (red, orange, yellow, green)
uniform vec4 hue_b;    //                  (aqua, blue, purple, magenta)
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
uniform vec4 zone_sat;    // grading per zone: shadows, midtones, highlights, global
uniform vec4 zone_light;
uniform vec4 zone_tint0;  // rgb: the zone hue's colour minus its own luminance
uniform vec4 zone_tint1;
uniform vec4 zone_tint2;
uniform vec4 zone_tint3;

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
float hue_channel(float p, float q, float t) {
  if (t < 0.0) t += 1.0;
  if (t > 1.0) t -= 1.0;
  if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
  if (t < 0.5) return q;
  if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
  return p;
}
vec3 from_hsl(vec3 c) {
  if (c.y <= 0.0) return vec3(c.z);
  float q = c.z < 0.5 ? c.z * (1.0 + c.y) : c.z + c.y - c.z * c.y;
  float p = 2.0 * c.z - q;
  float h = c.x / 360.0;
  return vec3(hue_channel(p, q, h + 1.0 / 3.0), hue_channel(p, q, h), hue_channel(p, q, h - 1.0 / 3.0));
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

vec3 white_balance_style(vec3 p) {
  if (wb0.w > 0.5) p = vec3(dot(wb0.xyz, p), dot(wb1.xyz, p), dot(wb2.xyz, p));
  p.r += tone0.x * 0.04;
  p.g += tone0.x * 0.01;
  p.b -= tone0.x * 0.03;
  return p;
}
vec3 tone(vec3 p) {
  p *= tone0.y;
  p += tone0.z;
  p = (p - 0.5) * tone0.w + 0.5;
  float l = luminance(p);
  float hi = clamp01((l - 0.5) * 2.0), lo = clamp01((0.5 - l) * 2.0);
  float lift = tone1.x * hi * 0.45 + tone1.y * lo * 0.45 + tone1.z * hi * 0.2 + tone1.w * lo * 0.2;
  p += lift;
  p = (p - 0.5) * (1.0 + tone2.x) + 0.5;
  float saturation = 1.0 + tone2.y + tone2.z * (1.0 - max3(p));
  float gray = luminance(p);
  return gray + (p - gray) * saturation;
}
vec3 levels(vec3 p) {
  float span = max(levels0.y - levels0.x, 1.0 / 255.0);
  vec3 x = pow(clamp((p - levels0.x) / span, 0.0, 1.0), vec3(1.0 / levels0.z));
  return levels0.w + x * (levels1.x - levels0.w);
}
vec3 hsl(vec3 p) {
  vec3 c = to_hsl(p);
  if (c.y <= 0.0) return p;
  float w[8];
  band_weights(c.x, w);
  float hue = band_sum(w, hue_a, hue_b);
  float saturation = band_sum(w, sat_a, sat_b);
  float lightness = band_sum(w, light_a, light_b);
  float s0 = c.y;
  c.x = mod(c.x + hue * 30.0 + 360.0, 360.0);
  c.y = clamp01(c.y * (1.0 + saturation));
  c.z = clamp01(c.z + lightness * 0.5 * s0 * (lightness > 0.0 ? 1.0 - c.z : c.z));
  return from_hsl(c);
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
vec3 grading(vec3 p) {
  float l = luminance(p);
  float radius = 0.5 + 0.5 * groups1.z;
  vec3 tints[4] = vec3[4](zone_tint0.rgb, zone_tint1.rgb, zone_tint2.rgb, zone_tint3.rgb);
  for (int z = 0; z < 4; ++z) {
    float w = z == 3 ? 1.0 : max(0.0, 1.0 - abs(l - 0.5 * float(z)) / radius);
    if (w <= 0.0 || (zone_sat[z] == 0.0 && zone_light[z] == 0.0)) continue;
    float tint = w * zone_sat[z] * 0.25;
    float lift = w * zone_light[z] * 0.25;
    p += tint * tints[z] + lift;
  }
  return p;
}

void main() {
  vec4 before = texelFetch(source, ivec2(gl_FragCoord.xy), 0);
  vec3 p = before.rgb;
  if (stage0.x > 0.5) p = white_balance_style(p);
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
  uint x = uint(gl_FragCoord.x), y = uint(gl_FragCoord.y);
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
