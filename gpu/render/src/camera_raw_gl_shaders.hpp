#pragma once

// Fragment shaders of camera_raw_gl.cpp.  Each reads its inputs with texelFetch (t0, t1, ...)
// and writes `color`; an R32F target keeps color.r.  Same licence header as camera_raw_gl.hpp.

namespace pixcake::gpu::camera_raw_gl::shaders {

inline constexpr const char* kVertex = R"(#version 330 core
void main() {
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

inline constexpr const char* kHeader = R"(#version 330 core
uniform sampler2D t0;
uniform sampler2D t1;
uniform sampler2D t2;
uniform sampler2D t3;
out vec4 color;
ivec2 here() { return ivec2(gl_FragCoord.xy); }
ivec2 size0() { return textureSize(t0, 0); }
)";

// box_mean, one axis: the mean of 2r + 1 samples, edge-clamped (all four channels).
inline constexpr const char* kBox = R"(
uniform int radius;
uniform int axis;  // 0 rows, 1 columns
void main() {
  ivec2 p = here(), n = size0();
  vec4 sum = vec4(0.0);
  for (int k = -radius; k <= radius; ++k) {
    ivec2 q = axis == 0 ? ivec2(clamp(p.x + k, 0, n.x - 1), p.y) : ivec2(p.x, clamp(p.y + k, 0, n.y - 1));
    sum += texelFetch(t0, q, 0);
  }
  color = sum / float(2 * radius + 1);
}
)";

// The window minimum or maximum, one axis, cut to the frame.
inline constexpr const char* kRank = R"(
uniform int radius;
uniform int axis;
uniform int take_max;
void main() {
  ivec2 p = here(), n = size0();
  int c = axis == 0 ? p.x : p.y, last = (axis == 0 ? n.x : n.y) - 1;
  float acc = texelFetch(t0, p, 0).r;
  for (int k = max(c - radius, 0); k <= min(c + radius, last); ++k) {
    float v = texelFetch(t0, axis == 0 ? ivec2(k, p.y) : ivec2(p.x, k), 0).r;
    acc = take_max != 0 ? max(acc, v) : min(acc, v);
  }
  color = vec4(acc);
}
)";

// (I, p, I*p, I*I) for the guided filter's four means.
inline constexpr const char* kGuidedPack = R"(
void main() {
  float i = texelFetch(t0, here(), 0).r, p = texelFetch(t1, here(), 0).r;
  color = vec4(i, p, i * p, i * i);
}
)";

// a and b from the four means (t0).
inline constexpr const char* kGuidedAb = R"(
uniform float eps;
void main() {
  vec4 m = texelFetch(t0, here(), 0);  // mean I, mean p, mean Ip, mean II
  float a = (m.z - m.x * m.y) / (m.w - m.x * m.x + eps);
  color = vec4(a, m.y - a * m.x, 0.0, 0.0);
}
)";

// mean a * I + mean b; t0 = means of (a, b), t1 = I.  scale > 1: the means are on a copy
// `scale` times smaller, read with camera_raw::bilinear at ((x + 0.5) / scale - 0.5, ...).
inline constexpr const char* kGuidedOut = R"(
uniform float scale;
uniform vec4 factor_v;  // scale == 0: the means are read at ((x + 0.5) * factor_v.x - 0.5, ...)
float bilinear_at(int c, vec2 at) {
  ivec2 n = size0();
  float x = clamp(at.x, 0.0, float(n.x - 1)), y = clamp(at.y, 0.0, float(n.y - 1));
  int x0 = int(floor(x)), y0 = int(floor(y));
  int x1 = min(x0 + 1, n.x - 1), y1 = min(y0 + 1, n.y - 1);
  float tx = x - float(x0), ty = y - float(y0);
  float p00 = texelFetch(t0, ivec2(x0, y0), 0)[c], p10 = texelFetch(t0, ivec2(x1, y0), 0)[c];
  float p01 = texelFetch(t0, ivec2(x0, y1), 0)[c], p11 = texelFetch(t0, ivec2(x1, y1), 0)[c];
  float top = p00 + (p10 - p00) * tx, bot = p01 + (p11 - p01) * tx;
  return top + (bot - top) * ty;
}
void main() {
  ivec2 p = here();
  float i = texelFetch(t1, p, 0).r;
  if (scale == 0.0) {
    vec2 at = (vec2(p) + 0.5) * factor_v.xy - 0.5;
    color = vec4(bilinear_at(0, at) * i + bilinear_at(1, at));
  } else if (scale <= 1.0) {
    vec4 m = texelFetch(t0, p, 0);
    color = vec4(m.x * i + m.y);
  } else {
    vec2 at = (vec2(p) + 0.5) / scale - 0.5;
    color = vec4(bilinear_at(0, at) * i + bilinear_at(1, at));
  }
}
)";

// The mean of each `block` x `block` cell (cut to t0), rows then columns as the CPU adds them;
// clamp_in clamps each sample to 0..1 first (dehaze's proxy).
inline constexpr const char* kBlockMean = R"(
uniform int block;
uniform int clamp_in;
void main() {
  ivec2 p = here(), n = size0();
  vec4 sum = vec4(0.0);
  float count = 0.0;
  for (int y = p.y * block; y < min(p.y * block + block, n.y); ++y) {
    for (int x = p.x * block; x < min(p.x * block + block, n.x); ++x) {
      vec4 v = texelFetch(t0, ivec2(x, y), 0);
      sum += clamp_in != 0 ? clamp(v, 0.0, 1.0) : v;
      count += 1.0;
    }
  }
  color = sum / max(count, 1.0);
}
)";

inline constexpr const char* kLuma = R"(
float luma3(vec3 c) { return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; }
uniform int clipped;  // 1: min(max(luma, 0), 1) (stage 3's lc)
void main() {
  float l = luma3(texelFetch(t0, here(), 0).rgb);
  color = vec4(clipped != 0 ? min(max(l, 0.0), 1.0) : l);
}
)";

// ---- Dehaze (camera_raw.rs stage 2)
inline constexpr const char* kChannelMin = R"(
void main() {
  vec3 c = texelFetch(t0, here(), 0).rgb;
  color = vec4(min(min(c.r, c.g), c.b));
}
)";
inline constexpr const char* kNormalised = R"(
uniform vec4 air;
void main() {
  vec3 c = texelFetch(t0, here(), 0).rgb;
  color = vec4(min(min(c.r / air.r, c.g / air.g), c.b / air.b));
}
)";
inline constexpr const char* kTransmission = R"(
uniform float weight;
void main() { color = vec4(1.0 - weight * texelFetch(t0, here(), 0).r); }
)";
// t0 = frame, t1 = refined transmission on the proxy (k times smaller).
inline constexpr const char* kDehazeApply = R"(
uniform float k;
uniform float amount;
uniform vec4 air;
float bilinear_t(vec2 at) {
  ivec2 n = textureSize(t1, 0);
  float x = clamp(at.x, 0.0, float(n.x - 1)), y = clamp(at.y, 0.0, float(n.y - 1));
  int x0 = int(floor(x)), y0 = int(floor(y));
  int x1 = min(x0 + 1, n.x - 1), y1 = min(y0 + 1, n.y - 1);
  float tx = x - float(x0), ty = y - float(y0);
  float p00 = texelFetch(t1, ivec2(x0, y0), 0).r, p10 = texelFetch(t1, ivec2(x1, y0), 0).r;
  float p01 = texelFetch(t1, ivec2(x0, y1), 0).r, p11 = texelFetch(t1, ivec2(x1, y1), 0).r;
  float top = p00 + (p10 - p00) * tx, bot = p01 + (p11 - p01) * tx;
  return top + (bot - top) * ty;
}
void main() {
  ivec2 p = here();
  vec4 q = texelFetch(t0, p, 0);
  float tv = clamp(bilinear_t((vec2(p) + 0.5) / k - 0.5), 0.1, 1.0);
  for (int c = 0; c < 3; ++c) {
    float i = q[c];
    float j = (i - air[c]) / tv + air[c];
    q[c] = amount >= 0.0 ? i + (j - i) * amount : i + (air[c] - i) * (-amount) * 0.5 * (1.0 - tv * 0.5);
  }
  color = q;
}
)";

// ---- Stage 3: Contrast, Texture, Clarity and tone() (camera_raw.rs)
// t0 = frame, t1 = lc, t2 = guided lc (clarity), t3 = blurred lc (texture).
inline constexpr const char* kToneDetail = R"(
const float TAU = 6.28318530717958647692;
uniform float clarity;
uniform float texture_amount;
uniform float k_con;
uniform int has_clarity;
uniform int has_texture;
float luma3(vec3 c) { return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; }
void main() {
  ivec2 p = here();
  vec4 q = texelFetch(t0, p, 0);
  float l0 = max(luma3(q.rgb), 0.0);
  float lc = texelFetch(t1, p, 0).r;
  float lv = lc;
  if (has_clarity != 0) {
    float m = 2.0 * clamp(lv, 0.0, 1.0) - 1.0;
    float mw = 1.0 - m * m;
    lv += clarity / 100.0 * 0.8 * (lc - texelFetch(t2, p, 0).r) * mw;
  }
  if (has_texture != 0) lv += texture_amount / 100.0 * 0.9 * clamp(lc - texelFetch(t3, p, 0).r, -0.1, 0.1);
  if (k_con != 0.0) {
    float xx = clamp(lv, 0.0, 1.0);
    lv += -k_con * sin(TAU * xx) / TAU;
  }
  lv = max(lv, 0.0) + (l0 - lc);
  float ratio = l0 > 1e-5 ? lv / l0 : 1.0;
  q.rgb = l0 > 1e-5 ? q.rgb * ratio : q.rgb + (lv - l0);
  color = q;
}
)";

inline constexpr const char* kCopy = R"(
void main() { color = texelFetch(t0, here(), 0); }
)";

// ---- Highlights / Shadows (camera_raw/local_tone.rs)
// Mirror into 0..n (the edge sample is not repeated), the binomial taps, and one sample of the
// zero-stuffed upsampling of `c` to W x H, adding the terms in up_at's order.
inline constexpr const char* kPyramid = R"(
const float TAPS[5] = float[5](1.0 / 16.0, 4.0 / 16.0, 6.0 / 16.0, 4.0 / 16.0, 1.0 / 16.0);
int mirror(int i, int n) {
  if (n == 1) return 0;
  int p = 2 * (n - 1), m = i;
  while (m < 0) m += p;
  while (m >= p) m -= p;
  return m >= n ? p - m : m;
}
bool stuffed(int i, int n, int lim, out int at) {
  int m = mirror(i, n);
  at = m / 2;
  return (m & 1) == 0 && m / 2 < lim;
}
float up_at(sampler2D c, int x, int y, int W, int H) {
  ivec2 cn = textureSize(c, 0);
  int lh = min(cn.y, (H + 1) / 2), lw = min(cn.x, (W + 1) / 2);
  int rows[5];
  bool has[5];
  for (int k = 0; k < 5; ++k) has[k] = stuffed(y + k - 2, H, lh, rows[k]);
  float sum = 0.0;
  for (int k = 0; k < 5; ++k) {
    int j;
    if (stuffed(x + k - 2, W, lw, j)) {
      float col = 0.0;
      for (int t = 0; t < 5; ++t)
        if (has[t]) col += TAPS[t] * (texelFetch(c, ivec2(j, rows[t]), 0).r * 4.0);
      sum += TAPS[k] * col;
    }
  }
  return sum;
}
)";

// blur_step, columns at the kept rows: t0 is p, the target p.w x ceil(p.h / step).
inline constexpr const char* kBlurColumns = R"(
uniform int step_size;
void main() {
  ivec2 p = here(), n = size0();
  int y = p.y * step_size;
  float sum = 0.0;
  for (int k = 0; k < 5; ++k) sum += TAPS[k] * texelFetch(t0, ivec2(p.x, mirror(y + k - 2, n.y)), 0).r;
  color = vec4(sum);
}
)";
// blur_step, rows at the kept columns: t0 is the column pass, the target ceil(w / step) x its h.
inline constexpr const char* kBlurRows = R"(
uniform int step_size;
void main() {
  ivec2 p = here(), n = size0();
  int x = p.x * step_size;
  float sum = 0.0;
  for (int k = 0; k < 5; ++k) sum += TAPS[k] * texelFetch(t0, ivec2(mirror(x + k - 2, n.x), p.y), 0).r;
  color = vec4(sum);
}
)";

// The remapped plane for one reference r.  mode 0: Highlights, 1: Shadows.
inline constexpr const char* kRemap = R"(
uniform int mode;
uniform float r;
uniform float range_lo;
uniform float span;
uniform float amount;
uniform float sigma;
uniform vec4 knot_a;
uniform float knot_a4;
uniform vec4 knot_b;
uniform float knot_b4;
float knot(float t, vec4 k, float k4) {
  float x = clamp(t, 0.0, 1.0) * 4.0;
  int i = min(int(x), 3);
  float ki = i < 4 ? k[i] : k4, kn = i + 1 < 4 ? k[i + 1] : k4;
  return ki + (kn - ki) * (x - float(i));
}
// tanh past +-9 is +-1 in float; some drivers overflow there.
float tanh_safe(float x) { return abs(x) > 9.0 ? sign(x) : tanh(x); }
float excess(float d, float s) { return d - s * tanh_safe(d / s); }
void main() {
  float x = texelFetch(t0, here(), 0).r, g = r, o;
  if (mode == 0) {
    o = x + knot((g - range_lo) / span, knot_a, knot_a4) * amount * excess(max(x - g, 0.0), sigma);
  } else {
    float below = clamp((g - x) / sigma, 0.0, 1.0);
    if (below == 0.0) {
      o = x;
    } else {
      float w = below * below * (3.0 - 2.0 * below);
      o = x + amount * knot((x - range_lo) / span, knot_a, knot_a4) * knot((g - range_lo) / span, knot_b, knot_b4) * w;
    }
  }
  color = vec4(o);
}
)";

// One reference's Laplacian, weighted, added to the level's sum: t0 = sum so far, t1 = level l
// of the input's pyramid, t2 = level l of the remapped pyramid, t3 = its level l + 1.
inline constexpr const char* kLapAccum = R"(
uniform float lo;
uniform float hi;
uniform float r;
uniform float step_ref;
void main() {
  ivec2 p = here(), n = size0();
  float prev = texelFetch(t0, p, 0).r;
  float wgt = max(1.0 - abs(clamp(texelFetch(t1, p, 0).r, lo, hi) - r) / step_ref, 0.0);
  if (wgt > 0.0) prev += wgt * (texelFetch(t2, p, 0).r - up_at(t3, p.x, p.y, n.x, n.y));
  color = vec4(prev);
}
)";

// The collapse: up(t0) to t1's size plus t1.
inline constexpr const char* kUpAdd = R"(
void main() {
  ivec2 p = here(), n = textureSize(t1, 0);
  color = vec4(up_at(t0, p.x, p.y, n.x, n.y) + texelFetch(t1, p, 0).r);
}
)";

inline constexpr const char* kLogLuma = R"(
uniform float floor_value;
float srgb_to_linear(float v) { return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4); }
void main() {
  vec3 c = texelFetch(t0, here(), 0).rgb;
  float l = 0.2126 * srgb_to_linear(c.r) + 0.7152 * srgb_to_linear(c.g) + 0.0722 * srgb_to_linear(c.b);
  color = vec4(log2(max(l, 0.0) + floor_value));
}
)";
// t0 + offset, or t0 - t1 when subtract is set.
inline constexpr const char* kAdd = R"(
uniform float offset;
void main() { color = vec4(texelFetch(t0, here(), 0).r + offset); }
)";
inline constexpr const char* kSubtract = R"(
void main() { color = vec4(texelFetch(t0, here(), 0).r - texelFetch(t1, here(), 0).r); }
)";
inline constexpr const char* kZero = R"(
void main() { color = vec4(0.0); }
)";
// The frame (t0) with each channel's linear light times 2^change (t1).
inline constexpr const char* kGain = R"(
float srgb_to_linear(float v) { return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4); }
float linear_to_srgb(float v) { return v <= 0.0031308 ? v * 12.92 : 1.055 * pow(v, 1.0 / 2.4) - 0.055; }
void main() {
  vec4 q = texelFetch(t0, here(), 0);
  float gain = exp2(texelFetch(t1, here(), 0).r);
  for (int c = 0; c < 3; ++c) q[c] = linear_to_srgb(srgb_to_linear(q[c]) * gain);
  color = q;
}
)";

}  // namespace pixcake::gpu::camera_raw_gl::shaders
