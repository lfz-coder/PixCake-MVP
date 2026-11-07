#include "pixcake/gl/native_backend.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <glad/gl.h>
// Apple marks the legacy OpenGL framework deprecated, but it remains the
// platform's native Core-profile context API. GL calls are loaded through
// Glad so this translation unit never depends on Qt or a platform GL header.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#include <OpenGL/OpenGL.h>
#pragma clang diagnostic pop
#endif

namespace pixcake::gl::native {
namespace {

#if defined(__APPLE__)

bool require_context(std::string& error) {
  if (CGLGetCurrentContext() == nullptr) { error = "no current native OpenGL context"; return false; }
  if (GLAD_GL_VERSION_3_3 == 0) { error = "native OpenGL Core 3.3 functions are not loaded"; return false; }
  return true;
}

std::string cgl_error(CGLError error) {
  const char* message = CGLErrorString(error);
  return message == nullptr ? "native OpenGL context error" : std::string(message);
}

std::string shader_log(GLuint shader) {
  GLint length = 0;
  glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
  if (length <= 1) return "shader compilation failed";
  std::string log(static_cast<std::size_t>(length), '\0');
  GLsizei written = 0;
  glGetShaderInfoLog(shader, length, &written, log.data());
  log.resize(static_cast<std::size_t>(std::max<GLsizei>(written, 0)));
  return log;
}

std::string program_log(GLuint program) {
  GLint length = 0;
  glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
  if (length <= 1) return "program link failed";
  std::string log(static_cast<std::size_t>(length), '\0');
  GLsizei written = 0;
  glGetProgramInfoLog(program, length, &written, log.data());
  log.resize(static_cast<std::size_t>(std::max<GLsizei>(written, 0)));
  return log;
}

class ContextImpl final : public detail::ContextImpl {
 public:
  ContextImpl() {
    const CGLPixelFormatAttribute attributes[] = {
        kCGLPFAAccelerated,
        kCGLPFAOpenGLProfile,
        static_cast<CGLPixelFormatAttribute>(kCGLOGLPVersion_3_2_Core),
        kCGLPFAColorSize,
        static_cast<CGLPixelFormatAttribute>(24),
        static_cast<CGLPixelFormatAttribute>(0),
    };
    GLint format_count = 0;
    const auto format_error = CGLChoosePixelFormat(attributes, &pixel_format_, &format_count);
    if (format_error != kCGLNoError || pixel_format_ == nullptr || format_count == 0) {
      error_ = "CGLChoosePixelFormat failed: " + cgl_error(format_error);
      return;
    }
    const auto context_error = CGLCreateContext(pixel_format_, nullptr, &context_);
    if (context_error != kCGLNoError || context_ == nullptr) {
      error_ = "CGLCreateContext failed: " + cgl_error(context_error);
      context_ = nullptr;
    }
  }
  ~ContextImpl() override {
    if (context_ != nullptr) {
      if (vertex_array_ != 0 && CGLSetCurrentContext(context_) == kCGLNoError) {
        glDeleteVertexArrays(1, &vertex_array_);
      }
      if (CGLGetCurrentContext() == context_) CGLSetCurrentContext(nullptr);
      CGLDestroyContext(context_);
    }
    if (pixel_format_ != nullptr) CGLDestroyPixelFormat(pixel_format_);
  }
  bool make_current(std::string& error) override {
    if (context_ == nullptr) { error = error_.empty() ? "context is not initialized" : error_; return false; }
    const auto result = CGLSetCurrentContext(context_);
    if (result != kCGLNoError) { error_ = "CGLSetCurrentContext failed: " + cgl_error(result); error = error_; return false; }
    const int version = gladLoaderLoadGL();
    if (version < GLAD_MAKE_VERSION(3, 3) || GLAD_GL_VERSION_3_3 == 0) {
      CGLSetCurrentContext(nullptr);
      error_ = "native OpenGL Core 3.3 functions are unavailable";
      error = error_;
      return false;
    }
    // A Core profile draw needs a bound vertex array even for attribute-less
    // full-screen passes, so the context owns one default VAO.
    if (vertex_array_ == 0) glGenVertexArrays(1, &vertex_array_);
    glBindVertexArray(vertex_array_);
    error_.clear();
    return true;
  }
  void done_current() override {
    if (context_ != nullptr && CGLGetCurrentContext() == context_) CGLSetCurrentContext(nullptr);
  }
  bool set_viewport(std::uint32_t width, std::uint32_t height, std::string& error) override {
    if (!require_context(error)) return false;
    glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    return true;
  }
  bool draw_triangles(std::uint32_t count, std::string& error) override {
    if (!require_context(error)) return false;
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(count));
    if (glGetError() != GL_NO_ERROR) { error = "glDrawArrays failed"; return false; }
    return true;
  }
  bool read_pixels(std::uint32_t width, std::uint32_t height, std::vector<std::byte>& data,
                   std::string& error) override {
    if (!require_context(error)) return false;
    data.resize(static_cast<std::size_t>(width) * height * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height), GL_RGBA,
                 GL_UNSIGNED_BYTE, data.data());
    if (glGetError() != GL_NO_ERROR) { error = "glReadPixels failed"; return false; }
    return true;
  }
  std::shared_ptr<detail::ShaderImpl> create_shader() override;
  std::shared_ptr<detail::ProgramImpl> create_program() override;
  std::shared_ptr<detail::Texture2DImpl> create_texture() override;
  std::shared_ptr<detail::FramebufferImpl> create_framebuffer() override;
  std::string last_error() const override { return error_; }

 private:
  CGLPixelFormatObj pixel_format_ = nullptr;
  CGLContextObj context_ = nullptr;
  GLuint vertex_array_ = 0;
  std::string error_;
};

class ShaderImpl final : public detail::ShaderImpl {
 public:
  ~ShaderImpl() override { if (handle_ != 0 && CGLGetCurrentContext() != nullptr) glDeleteShader(handle_); }
  bool compile(ShaderStage stage, std::string_view source, std::string& error) override {
    if (!require_context(error)) return false;
    if (source.empty()) { error = "shader source is empty"; return false; }
    if (handle_ != 0) glDeleteShader(handle_);
    handle_ = glCreateShader(stage == ShaderStage::Vertex ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER);
    if (handle_ == 0) { error = "glCreateShader failed"; return false; }
    const char* text = source.data();
    const GLint length = static_cast<GLint>(source.size());
    glShaderSource(handle_, 1, &text, &length);
    glCompileShader(handle_);
    GLint ok = GL_FALSE;
    glGetShaderiv(handle_, GL_COMPILE_STATUS, &ok);
    if (ok == GL_TRUE) return true;
    error = shader_log(handle_);
    return false;
  }
  GLuint handle() const { return handle_; }

 private:
  GLuint handle_ = 0;
};

class ProgramImpl final : public detail::ProgramImpl {
 public:
  ~ProgramImpl() override { if (handle_ != 0 && CGLGetCurrentContext() != nullptr) glDeleteProgram(handle_); }
  bool link(const detail::ShaderImpl& vertex, const detail::ShaderImpl& fragment, std::string& error) override {
    const auto* v = dynamic_cast<const ShaderImpl*>(&vertex);
    const auto* f = dynamic_cast<const ShaderImpl*>(&fragment);
    if (v == nullptr || f == nullptr) { error = "shader belongs to a different backend"; return false; }
    if (!require_context(error)) return false;
    if (v->handle() == 0 || f->handle() == 0) { error = "shader is not compiled"; return false; }
    if (handle_ != 0) glDeleteProgram(handle_);
    handle_ = glCreateProgram();
    glAttachShader(handle_, v->handle());
    glAttachShader(handle_, f->handle());
    glLinkProgram(handle_);
    GLint ok = GL_FALSE;
    glGetProgramiv(handle_, GL_LINK_STATUS, &ok);
    if (ok == GL_TRUE) return true;
    error = program_log(handle_);
    return false;
  }
  bool use(std::string& error) override {
    if (!require_context(error)) return false;
    if (handle_ == 0) { error = "program is not linked"; return false; }
    glUseProgram(handle_);
    return true;
  }
  bool set_float(std::string_view name, float value, std::string& error) override {
    return set(name, error, [value](GLint location) { glUniform1f(location, value); });
  }
  bool set_int(std::string_view name, std::int32_t value, std::string& error) override {
    return set(name, error, [value](GLint location) { glUniform1i(location, value); });
  }
  bool set_vec4(std::string_view name, const std::array<float, 4>& value, std::string& error) override {
    return set(name, error, [&value](GLint location) { glUniform4fv(location, 1, value.data()); });
  }
  bool set_mat4(std::string_view name, const std::array<float, 16>& value, std::string& error) override {
    return set(name, error, [&value](GLint location) { glUniformMatrix4fv(location, 1, GL_FALSE, value.data()); });
  }
  bool set_texture_unit(std::string_view name, std::uint32_t unit, std::string& error) override {
    return set_int(name, static_cast<std::int32_t>(unit), error);
  }

 private:
  template <typename Setter>
  bool set(std::string_view name, std::string& error, Setter setter) {
    if (!require_context(error)) return false;
    if (handle_ == 0) { error = "program is not linked"; return false; }
    const std::string key(name);
    const GLint location = glGetUniformLocation(handle_, key.c_str());
    if (location < 0) { error = "uniform not found: " + key; return false; }
    setter(location);
    return true;
  }
  GLuint handle_ = 0;
};

class BufferImpl final : public detail::BufferImpl {
 public:
  ~BufferImpl() override { if (handle_ != 0 && CGLGetCurrentContext() != nullptr) glDeleteBuffers(1, &handle_); }
  bool allocate(std::size_t bytes, const void* data, std::string& error) override {
    if (!require_context(error)) return false;
    if (bytes == 0) { error = "buffer size is zero"; return false; }
    if (handle_ == 0) glGenBuffers(1, &handle_);
    glBindBuffer(GL_ARRAY_BUFFER, handle_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes), data, GL_DYNAMIC_DRAW);
    size_ = bytes;
    return true;
  }
  bool write(std::size_t offset, std::size_t bytes, const void* data, std::string& error) override {
    if (!require_context(error)) return false;
    if (handle_ == 0) { error = "buffer is not allocated"; return false; }
    if (data == nullptr || offset > size_ || bytes > size_ - offset) { error = "buffer write is out of range"; return false; }
    glBindBuffer(GL_ARRAY_BUFFER, handle_);
    glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(bytes), data);
    return true;
  }

 private:
  GLuint handle_ = 0;
  std::size_t size_ = 0;
};

class VertexArrayImpl final : public detail::VertexArrayImpl {
 public:
  ~VertexArrayImpl() override { if (handle_ != 0 && CGLGetCurrentContext() != nullptr) glDeleteVertexArrays(1, &handle_); }
  bool bind(std::string& error) override {
    if (!require_context(error)) return false;
    if (handle_ == 0) glGenVertexArrays(1, &handle_);
    glBindVertexArray(handle_);
    return true;
  }
  bool enable_attribute(std::uint32_t index, std::int32_t components, std::uint32_t stride,
                        std::size_t offset, std::string& error) override {
    if (!require_context(error)) return false;
    if (handle_ == 0) { error = "vertex array is not bound"; return false; }
    if (components < 1 || components > 4) { error = "attribute component count is invalid"; return false; }
    glBindVertexArray(handle_);
    glEnableVertexAttribArray(index);
    glVertexAttribPointer(index, components, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride),
                          reinterpret_cast<const void*>(offset));
    return true;
  }

 private:
  GLuint handle_ = 0;
};

class TextureImpl final : public detail::Texture2DImpl {
 public:
  ~TextureImpl() override { if (handle_ != 0 && CGLGetCurrentContext() != nullptr) glDeleteTextures(1, &handle_); }
  bool allocate(std::uint32_t width, std::uint32_t height, PixelFormat, std::string& error) override {
    if (!require_context(error)) return false;
    if (width == 0 || height == 0) { error = "texture size is zero"; return false; }
    if (handle_ == 0) glGenTextures(1, &handle_);
    glBindTexture(GL_TEXTURE_2D, handle_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    width_ = width;
    height_ = height;
    return true;
  }
  bool upload(const ImageView& image, std::string& error) override {
    if (image.data == nullptr || image.width == 0 || image.height == 0) { error = "image view is empty"; return false; }
    const std::uint32_t stride = image.row_stride == 0 ? image.width * 4 : image.row_stride;
    if (stride < image.width * 4 || stride % 4 != 0 ||
        // The last row needs only its own pixels, not a whole stride: a tile
        // cut from a larger image ends inside the image's last row.
        image.bytes < static_cast<std::size_t>(stride) * (image.height - 1) + static_cast<std::size_t>(image.width) * 4) {
      error = "image view row stride or size is invalid";
      return false;
    }
    if (handle_ == 0 || width_ != image.width || height_ != image.height) {
      if (!allocate(image.width, image.height, image.format, error)) return false;
    }
    if (!require_context(error)) return false;
    glBindTexture(GL_TEXTURE_2D, handle_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(stride / 4));
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(image.width), static_cast<GLsizei>(image.height),
                    GL_RGBA, GL_UNSIGNED_BYTE, image.data);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    return true;
  }
  bool bind(std::uint32_t unit, std::string& error) override {
    if (!require_context(error)) return false;
    if (handle_ == 0) { error = "texture is not allocated"; return false; }
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, handle_);
    return true;
  }
  GLuint handle() const { return handle_; }

 private:
  GLuint handle_ = 0;
  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
};

class FramebufferImpl final : public detail::FramebufferImpl {
 public:
  ~FramebufferImpl() override { if (handle_ != 0 && CGLGetCurrentContext() != nullptr) glDeleteFramebuffers(1, &handle_); }
  bool attach_color(const detail::Texture2DImpl& texture, std::string& error) override {
    const auto* image = dynamic_cast<const TextureImpl*>(&texture);
    if (image == nullptr) { error = "texture belongs to a different backend"; return false; }
    if (!require_context(error)) return false;
    if (image->handle() == 0) { error = "texture is not allocated"; return false; }
    if (handle_ == 0) glGenFramebuffers(1, &handle_);
    glBindFramebuffer(GL_FRAMEBUFFER, handle_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, image->handle(), 0);
    return true;
  }
  bool complete(std::string& error) override {
    if (!require_context(error)) return false;
    if (handle_ == 0) { error = "framebuffer has no attachment"; return false; }
    glBindFramebuffer(GL_FRAMEBUFFER, handle_);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) return true;
    error = "framebuffer is incomplete";
    return false;
  }
  bool bind(std::string& error) override {
    if (!require_context(error)) return false;
    if (handle_ == 0) { error = "framebuffer has no attachment"; return false; }
    glBindFramebuffer(GL_FRAMEBUFFER, handle_);
    return true;
  }

 private:
  GLuint handle_ = 0;
};

class FenceImpl final : public detail::FenceImpl {
 public:
  ~FenceImpl() override { if (sync_ != nullptr && CGLGetCurrentContext() != nullptr) glDeleteSync(sync_); }
  bool insert(std::string& error) override {
    if (!require_context(error)) return false;
    if (sync_ != nullptr) glDeleteSync(sync_);
    sync_ = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (sync_ == nullptr) { error = "glFenceSync failed"; return false; }
    return true;
  }
  bool wait(std::uint64_t timeout_ns, std::string& error) override {
    if (!require_context(error)) return false;
    if (sync_ == nullptr) { error = "fence was not inserted"; return false; }
    const GLenum result = glClientWaitSync(sync_, GL_SYNC_FLUSH_COMMANDS_BIT, timeout_ns);
    if (result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED) return true;
    error = result == GL_TIMEOUT_EXPIRED ? "fence wait timed out" : "fence wait failed";
    return false;
  }

 private:
  GLsync sync_ = nullptr;
};

std::shared_ptr<detail::ShaderImpl> ContextImpl::create_shader() { return std::make_shared<ShaderImpl>(); }
std::shared_ptr<detail::ProgramImpl> ContextImpl::create_program() { return std::make_shared<ProgramImpl>(); }
std::shared_ptr<detail::Texture2DImpl> ContextImpl::create_texture() { return std::make_shared<TextureImpl>(); }
std::shared_ptr<detail::FramebufferImpl> ContextImpl::create_framebuffer() { return std::make_shared<FramebufferImpl>(); }

#else  // !__APPLE__

constexpr const char* kUnsupported = "native Core OpenGL backend is not implemented for this platform";

class ContextImpl final : public detail::ContextImpl {
 public:
  bool make_current(std::string& error) override { error = kUnsupported; return false; }
  void done_current() override {}
  bool set_viewport(std::uint32_t, std::uint32_t, std::string& error) override { error = kUnsupported; return false; }
  bool draw_triangles(std::uint32_t, std::string& error) override { error = kUnsupported; return false; }
  bool read_pixels(std::uint32_t, std::uint32_t, std::vector<std::byte>&, std::string& error) override { error = kUnsupported; return false; }
  std::shared_ptr<detail::ShaderImpl> create_shader() override { return nullptr; }
  std::shared_ptr<detail::ProgramImpl> create_program() override { return nullptr; }
  std::shared_ptr<detail::Texture2DImpl> create_texture() override { return nullptr; }
  std::shared_ptr<detail::FramebufferImpl> create_framebuffer() override { return nullptr; }
  std::string last_error() const override { return kUnsupported; }
};

#endif

}  // namespace

#if defined(__APPLE__)
bool available() { return true; }
Context create_context() { return Context(std::make_shared<ContextImpl>()); }
Shader create_shader() { return Shader(std::make_shared<ShaderImpl>()); }
Program create_program() { return Program(std::make_shared<ProgramImpl>()); }
Buffer create_buffer() { return Buffer(std::make_shared<BufferImpl>()); }
VertexArray create_vertex_array() { return VertexArray(std::make_shared<VertexArrayImpl>()); }
Texture2D create_texture2d() { return Texture2D(std::make_shared<TextureImpl>()); }
Framebuffer create_framebuffer() { return Framebuffer(std::make_shared<FramebufferImpl>()); }
Fence create_fence() { return Fence(std::make_shared<FenceImpl>()); }
#else
bool available() { return false; }
Context create_context() { return Context(std::make_shared<ContextImpl>()); }
// Without a platform implementation every resource stays a facade null
// object, which already reports failure on every call.
Shader create_shader() { return Shader(); }
Program create_program() { return Program(); }
Buffer create_buffer() { return Buffer(); }
VertexArray create_vertex_array() { return VertexArray(); }
Texture2D create_texture2d() { return Texture2D(); }
Framebuffer create_framebuffer() { return Framebuffer(); }
Fence create_fence() { return Fence(); }
#endif

}  // namespace pixcake::gl::native
