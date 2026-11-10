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
  if (scale <= 1.0) {
    vec4 m = texelFetch(t0, p, 0);
    color = vec4(m.x * i + m.y);
  } else {
    vec2 at = (vec2(p) + 0.5) / scale - 0.5;
    color = vec4(bilinear_at(0, at) * i + bilinear_at(1, at));
  }
}
)";

// The mean of each `block` x `block` cell (cut to t0), rows then columns as the CPU adds them.
inline constexpr const char* kBlockMean = R"(
uniform int block;
void main() {
  ivec2 p = here(), n = size0();
  vec4 sum = vec4(0.0);
  float count = 0.0;
  for (int y = p.y * block; y < min(p.y * block + block, n.y); ++y) {
    for (int x = p.x * block; x < min(p.x * block + block, n.x); ++x) {
      sum += texelFetch(t0, ivec2(x, y), 0);
      count += 1.0;
    }
  }
  color = sum / max(count, 1.0);
}
)";

inline constexpr const char* kCopy = R"(
void main() { color = texelFetch(t0, here(), 0); }
)";

}  // namespace pixcake::gpu::camera_raw_gl::shaders
