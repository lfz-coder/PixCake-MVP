#include "pixcake/gl/gl.hpp"

#if defined(PIXCAKE_GL_BACKEND_QT)
#include <QGuiApplication>
#endif

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const char* expression, int line) {
  if (!condition) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, expression);
    ++failures;
  }
}

#define EXPECT(condition) expect(static_cast<bool>(condition), #condition, __LINE__)

}  // namespace

int main(int argc, char** argv) {
#if defined(PIXCAKE_GL_BACKEND_QT)
  QGuiApplication app(argc, argv);
#else
  (void)argc;
  (void)argv;
#endif

  // The failure paths must be useful before a context or resource exists.
  pixcake::gl::Shader shader;
  EXPECT(!shader.valid());
  std::string error;
  EXPECT(!shader.compile(pixcake::gl::ShaderStage::Vertex, {}, &error));
  EXPECT(!error.empty());

  pixcake::gl::Program program;
  EXPECT(!program.use(&error));
  EXPECT(!error.empty());

  pixcake::gl::Buffer buffer;
  EXPECT(!buffer.bind(&error));
  pixcake::gl::VertexArray vertex_array;
  EXPECT(!vertex_array.bind(&error));
  pixcake::gl::Texture2D texture;
  EXPECT(!texture.bind(0, &error));
  pixcake::gl::Framebuffer framebuffer;
  EXPECT(!framebuffer.complete(&error));
  pixcake::gl::Fence fence;
  EXPECT(!fence.wait(0, &error));
  EXPECT(!error.empty());

#if defined(PIXCAKE_GL_BACKEND_QT) || defined(PIXCAKE_GL_BACKEND_NATIVE)
  std::string context_error;
  auto context = pixcake::gl::Context::create(&context_error);
  if (!context) {
    // CI may not expose a hardware or software GL provider. The API/error
    // paths above still validate the contract in that environment.
    std::fprintf(stderr, "GPU context unavailable; runtime smoke skipped: %s\n", context_error.c_str());
    return failures == 0 ? 0 : 1;
  }
  const bool current = context->makeCurrent();
  EXPECT(current);
  if (current) {
    {
      const std::string vertex_source =
          "#version 330 core\nlayout(location=0) in vec2 position;\n"
          "void main(){gl_Position=vec4(position,0,1);}";
      const std::string fragment_source =
          "#version 330 core\nout vec4 color;\nuniform float gain;\n"
          "void main(){color=vec4(gain,0,0,1);}";
      pixcake::gl::Shader vertex;
      pixcake::gl::Shader fragment;
      EXPECT(vertex.compile(pixcake::gl::ShaderStage::Vertex, vertex_source, &error));
      EXPECT(fragment.compile(pixcake::gl::ShaderStage::Fragment, fragment_source, &error));
      EXPECT(program.link(vertex, fragment, &error));
      EXPECT(program.use(&error));
      EXPECT(program.setUniform("gain", 1.0F, &error));

      std::array<std::uint8_t, 16> pixels = {255, 0, 0, 255, 0, 255, 0, 255,
                                              0, 0, 255, 255, 255, 255, 255, 255};
      EXPECT(texture.allocate(2, 2, pixcake::gl::PixelFormat::RGBA8, &error));
      EXPECT(texture.upload(pixcake::gl::PixelUpload{2, 2, 8, pixcake::gl::PixelFormat::RGBA8,
                                                     pixels.data()},
                           &error));
      EXPECT(texture.bind(0, &error));
      EXPECT(framebuffer.attachColor(texture, &error));
      EXPECT(framebuffer.complete(&error));
      EXPECT(framebuffer.bind(&error));

      EXPECT(buffer.allocate(16, pixcake::gl::BufferUsage::DynamicDraw, &error));
      EXPECT(buffer.write(0, pixels.data(), 16, &error));
      EXPECT(vertex_array.bind(&error));
      EXPECT(buffer.bind(&error));
      EXPECT(vertex_array.enableAttribute(0, 2, pixcake::gl::AttributeType::Float32, 8, 0, false,
                                          &error));

      EXPECT(fence.insert(&error));
      EXPECT(fence.wait(1000000000U, &error));
    }
    context->doneCurrent();
  }
#endif

  return failures == 0 ? 0 : 1;
}
