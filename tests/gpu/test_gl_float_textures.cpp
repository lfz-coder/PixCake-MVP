// R32F and RGBA32F textures and float readback on the native backend: a texelFetch copy into a
// float framebuffer reads back the same bits (the Camera Raw whole-frame stages keep unclipped
// floats between passes).
#include <pixcake/gl/gl.hpp>
#include <pixcake/gl/native_backend.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define CHECK(x)                                                         \
  do {                                                                   \
    if (!(x)) {                                                          \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x);  \
      std::exit(1);                                                      \
    }                                                                    \
  } while (0)

namespace {

namespace gl = pixcake::gl;

constexpr const char* kVertex = R"(#version 330 core
void main() {
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";
constexpr const char* kCopy = R"(#version 330 core
uniform sampler2D source;
out vec4 color;
void main() { color = texelFetch(source, ivec2(gl_FragCoord.xy), 0); }
)";

// Draws `in` (w x h, format) through the copy shader into a texture of the same format and
// returns what reads back.
std::vector<std::byte> copy(gl::Context& context, gl::PixelFormat format, std::uint32_t w, std::uint32_t h,
                            const std::vector<std::byte>& in) {
  std::string error;
  gl::Shader vs = context.createShader(), fs = context.createShader();
  gl::Program program = context.createProgram();
  CHECK(vs.compile(gl::ShaderStage::Vertex, kVertex, error));
  CHECK(fs.compile(gl::ShaderStage::Fragment, kCopy, error));
  CHECK(program.link(vs, fs, error));
  gl::Texture2D src = context.createTexture2D(), dst = context.createTexture2D();
  const auto bpp = static_cast<std::uint32_t>(gl::bytesPerPixel(format));
  const gl::ImageView view{w, h, w * bpp, format, in.data(), in.size()};
  CHECK(src.upload(view, error));
  CHECK(dst.allocate(w, h, format, error));
  gl::Framebuffer fb = context.createFramebuffer();
  CHECK(fb.attachColor(dst, error) && fb.complete(error));
  CHECK(program.use(error) && fb.bind(error) && context.setViewport(w, h, error) && src.bind(0, error) &&
        program.setTextureUnit("source", 0, error) && context.drawTriangles(3, error));
  std::vector<std::byte> out;
  CHECK(context.readPixels(w, h, format, out, error));
  if (format == gl::PixelFormat::Rgba8) {
    // The four-argument readback is the RGBA8 one.
    std::vector<std::byte> legacy;
    CHECK(context.readPixels(w, h, legacy, error) && legacy == out);
  }
  return out;
}

std::vector<std::byte> bytes_of(const std::vector<float>& v) {
  std::vector<std::byte> b(v.size() * sizeof(float));
  std::memcpy(b.data(), v.data(), b.size());
  return b;
}
std::vector<float> floats_of(const std::vector<std::byte>& b) {
  std::vector<float> v(b.size() / sizeof(float));
  std::memcpy(v.data(), b.data(), v.size() * sizeof(float));
  return v;
}
void expect_same(const std::vector<float>& want, const std::vector<float>& got) {
  CHECK(want.size() == got.size());
  for (std::size_t i = 0; i < want.size(); ++i) {
    // A denormal may be flushed to a zero of the same sign.
    const bool flushed = std::fpclassify(want[i]) == FP_SUBNORMAL && got[i] == 0.0F;
    std::uint32_t a, b;
    std::memcpy(&a, &want[i], 4);
    std::memcpy(&b, &got[i], 4);
    if (a != b && !flushed) std::fprintf(stderr, "sample %zu: want %.9g got %.9g\n", i, want[i], got[i]);
    CHECK(a == b || flushed);
  }
}

std::vector<float> samples(std::size_t n) {
  const float special[] = {-0.5F, 1.75F, 1e-6F, 65504.0F, 3.0e-39F, 0.0F, -0.0F, 2.5F, 0.123456789F, -3.75F};
  std::vector<float> v(n);
  for (std::size_t i = 0; i < n; ++i) v[i] = special[i % 10] + static_cast<float>(i / 10) * 0.001F;
  return v;
}

void test_rgba32f_roundtrip(gl::Context& context) {
  const auto in = samples(3 * 2 * 4);
  expect_same(in, floats_of(copy(context, gl::PixelFormat::Rgba32F, 3, 2, bytes_of(in))));
}
void test_r32f_roundtrip(gl::Context& context) {
  const auto in = samples(5 * 3);
  expect_same(in, floats_of(copy(context, gl::PixelFormat::R32F, 5, 3, bytes_of(in))));
}
void test_rgba8_unchanged(gl::Context& context) {
  std::vector<std::byte> in(4 * 3 * 4);
  for (std::size_t i = 0; i < in.size(); ++i) in[i] = static_cast<std::byte>(i * 17 % 256);
  const auto out = copy(context, gl::PixelFormat::Rgba8, 4, 3, in);
  CHECK(out == in);
}

}  // namespace

int main() {
  if (!gl::native::available()) {
    std::printf("test_gl_float_textures: skipped (no native GL)\n");
    return 0;
  }
  auto context = gl::native::create_context();
  if (!context.valid() || !context.makeCurrent()) {
    std::printf("test_gl_float_textures: skipped (no context)\n");
    return 0;
  }
  CHECK(gl::bytesPerPixel(gl::PixelFormat::Rgba8) == 4 && gl::bytesPerPixel(gl::PixelFormat::R32F) == 4 &&
        gl::bytesPerPixel(gl::PixelFormat::Rgba32F) == 16);
  test_rgba32f_roundtrip(context);
  test_r32f_roundtrip(context);
  test_rgba8_unchanged(context);
  context.doneCurrent();
  std::printf("test_gl_float_textures: ok\n");
  return 0;
}
