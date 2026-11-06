#include "pixcake/gl/qt_backend.hpp"

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace pixcake::gl::qt {
namespace {

QOpenGLFunctions* current(std::string& error) {
  auto* context = QOpenGLContext::currentContext();
  if (context == nullptr) { error = "no current OpenGL context"; return nullptr; }
  return context->functions();
}

class ContextImpl final : public detail::ContextImpl {
 public:
  ContextImpl() {
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    surface_.setFormat(format); surface_.create(); context_.setFormat(format);
    if (!context_.create()) error_ = "failed to create OpenGL context";
  }
  bool make_current(std::string& error) override {
    if (!error_.empty()) { error = error_; return false; }
    if (!context_.makeCurrent(&surface_)) { error_ = "failed to make OpenGL context current"; error = error_; return false; }
    return true;
  }
  void done_current() override { context_.doneCurrent(); }
  bool draw_triangles(std::uint32_t count, std::string& error) override {
    QOpenGLFunctions* gl = current(error); if (gl == nullptr) return false;
    gl->glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(count)); return true;
  }
  bool set_viewport(std::uint32_t width, std::uint32_t height, std::string& error) override {
    auto* gl = current(error); if (gl == nullptr) return false;
    gl->glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height)); return true;
  }
  bool read_pixels(std::uint32_t width, std::uint32_t height, std::vector<std::byte>& data, std::string& error) override {
    auto* gl = current(error); if (gl == nullptr) return false;
    data.resize(static_cast<std::size_t>(width) * height * 4);
    gl->glReadPixels(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height), GL_RGBA, GL_UNSIGNED_BYTE, data.data());
    return true;
  }
  std::shared_ptr<detail::ShaderImpl> create_shader() override;
  std::shared_ptr<detail::ProgramImpl> create_program() override;
  std::shared_ptr<detail::Texture2DImpl> create_texture() override;
  std::shared_ptr<detail::FramebufferImpl> create_framebuffer() override;
  std::string last_error() const override { return error_; }
 private:
  QOpenGLContext context_; QOffscreenSurface surface_; std::string error_;
};

class ShaderImpl final : public detail::ShaderImpl {
 public:
  ~ShaderImpl() override { std::string e; if (handle_ != 0) if (auto* gl = current(e)) gl->glDeleteShader(handle_); }
  bool compile(ShaderStage stage, std::string_view source, std::string& error) override {
    auto* gl = current(error); if (gl == nullptr) return false;
    if (handle_ != 0) gl->glDeleteShader(handle_);
    handle_ = gl->glCreateShader(stage == ShaderStage::Vertex ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER);
    const char* text = source.data(); const GLint length = static_cast<GLint>(source.size());
    gl->glShaderSource(handle_, 1, &text, &length); gl->glCompileShader(handle_);
    GLint ok = GL_FALSE; gl->glGetShaderiv(handle_, GL_COMPILE_STATUS, &ok); if (ok == GL_TRUE) return true;
    GLint size = 0; gl->glGetShaderiv(handle_, GL_INFO_LOG_LENGTH, &size); std::string log(static_cast<std::size_t>(std::max(size, 1)), '\0'); gl->glGetShaderInfoLog(handle_, size, nullptr, log.data()); error = log; return false;
  }
  GLuint handle() const { return handle_; }
 private: GLuint handle_ = 0;
};

class ProgramImpl final : public detail::ProgramImpl {
 public:
  ~ProgramImpl() override { std::string e; if (handle_ != 0) if (auto* gl = current(e)) gl->glDeleteProgram(handle_); }
  bool link(const detail::ShaderImpl& vertex, const detail::ShaderImpl& fragment, std::string& error) override {
    const auto* v = dynamic_cast<const ShaderImpl*>(&vertex); const auto* f = dynamic_cast<const ShaderImpl*>(&fragment); auto* gl = current(error);
    if (gl == nullptr || v == nullptr || f == nullptr) { if (error.empty()) error = "shader belongs to a different backend"; return false; }
    if (handle_ != 0) gl->glDeleteProgram(handle_); handle_ = gl->glCreateProgram(); gl->glAttachShader(handle_, v->handle()); gl->glAttachShader(handle_, f->handle()); gl->glLinkProgram(handle_);
    GLint ok = GL_FALSE; gl->glGetProgramiv(handle_, GL_LINK_STATUS, &ok); if (ok == GL_TRUE) return true;
    GLint size = 0; gl->glGetProgramiv(handle_, GL_INFO_LOG_LENGTH, &size); std::string log(static_cast<std::size_t>(std::max(size, 1)), '\0'); gl->glGetProgramInfoLog(handle_, size, nullptr, log.data()); error = log; return false;
  }
  bool use(std::string& error) override { auto* gl = current(error); if (gl == nullptr || handle_ == 0) { if (error.empty()) error = "program is not linked"; return false; } gl->glUseProgram(handle_); return true; }
  bool set_float(std::string_view name, float value, std::string& error) override { return set(name, error, [value](QOpenGLFunctions* gl, GLint location) { gl->glUniform1f(location, value); }); }
  bool set_int(std::string_view name, std::int32_t value, std::string& error) override { return set(name, error, [value](QOpenGLFunctions* gl, GLint location) { gl->glUniform1i(location, value); }); }
  bool set_vec4(std::string_view name, const std::array<float, 4>& value, std::string& error) override { return set(name, error, [&value](QOpenGLFunctions* gl, GLint location) { gl->glUniform4fv(location, 1, value.data()); }); }
  bool set_mat4(std::string_view name, const std::array<float, 16>& value, std::string& error) override { return set(name, error, [&value](QOpenGLFunctions* gl, GLint location) { gl->glUniformMatrix4fv(location, 1, GL_FALSE, value.data()); }); }
  bool set_texture_unit(std::string_view name, std::uint32_t unit, std::string& error) override { return set_int(name, static_cast<std::int32_t>(unit), error); }
 private:
  template <typename Setter> bool set(std::string_view name, std::string& error, Setter setter) { auto* gl = current(error); if (gl == nullptr || handle_ == 0) return false; const std::string copy(name); const GLint location = gl->glGetUniformLocation(handle_, copy.c_str()); if (location < 0) { error = "uniform not found: " + copy; return false; } setter(gl, location); return true; }
  GLuint handle_ = 0;
};

class TextureImpl final : public detail::Texture2DImpl {
 public:
  ~TextureImpl() override { std::string e; if (handle_ != 0) if (auto* gl = current(e)) gl->glDeleteTextures(1, &handle_); }
  bool allocate(std::uint32_t width, std::uint32_t height, PixelFormat, std::string& error) override { auto* gl = current(error); if (gl == nullptr) return false; if (handle_ == 0) gl->glGenTextures(1, &handle_); gl->glBindTexture(GL_TEXTURE_2D, handle_); gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr); width_ = width; height_ = height; return true; }
  bool upload(const ImageView& image, std::string& error) override { if (image.data == nullptr || image.width == 0 || image.height == 0) { error = "image view is empty"; return false; } if (handle_ == 0 || width_ != image.width || height_ != image.height) if (!allocate(image.width, image.height, image.format, error)) return false; auto* gl = current(error); if (gl == nullptr) return false; gl->glBindTexture(GL_TEXTURE_2D, handle_); gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1); gl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(image.width), static_cast<GLsizei>(image.height), GL_RGBA, GL_UNSIGNED_BYTE, image.data); return true; }
  bool bind(std::uint32_t unit, std::string& error) override { auto* gl = current(error); if (gl == nullptr || handle_ == 0) return false; gl->glActiveTexture(GL_TEXTURE0 + unit); gl->glBindTexture(GL_TEXTURE_2D, handle_); return true; }
  GLuint handle() const { return handle_; }
 private: GLuint handle_ = 0; std::uint32_t width_ = 0; std::uint32_t height_ = 0;
};

class FramebufferImpl final : public detail::FramebufferImpl {
 public:
  ~FramebufferImpl() override { std::string e; if (handle_ != 0) if (auto* gl = current(e)) gl->glDeleteFramebuffers(1, &handle_); }
  bool attach_color(const detail::Texture2DImpl& texture, std::string& error) override { const auto* image = dynamic_cast<const TextureImpl*>(&texture); auto* gl = current(error); if (gl == nullptr || image == nullptr) { if (error.empty()) error = "texture belongs to a different backend"; return false; } if (handle_ == 0) gl->glGenFramebuffers(1, &handle_); gl->glBindFramebuffer(GL_FRAMEBUFFER, handle_); gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, image->handle(), 0); return true; }
  bool complete(std::string& error) override { auto* gl = current(error); if (gl == nullptr || handle_ == 0) return false; gl->glBindFramebuffer(GL_FRAMEBUFFER, handle_); if (gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) return true; error = "framebuffer is incomplete"; return false; }
  bool bind(std::string& error) override { auto* gl = current(error); if (gl == nullptr || handle_ == 0) return false; gl->glBindFramebuffer(GL_FRAMEBUFFER, handle_); return true; }
 private: GLuint handle_ = 0;
};

}  // namespace

std::shared_ptr<detail::ShaderImpl> ContextImpl::create_shader() { return std::make_shared<ShaderImpl>(); }
std::shared_ptr<detail::ProgramImpl> ContextImpl::create_program() { return std::make_shared<ProgramImpl>(); }
std::shared_ptr<detail::Texture2DImpl> ContextImpl::create_texture() { return std::make_shared<TextureImpl>(); }
std::shared_ptr<detail::FramebufferImpl> ContextImpl::create_framebuffer() { return std::make_shared<FramebufferImpl>(); }

Context create_context() { return Context(std::make_shared<ContextImpl>()); }
Shader create_shader() { return Shader(std::make_shared<ShaderImpl>()); }
Program create_program() { return Program(std::make_shared<ProgramImpl>()); }
Texture2D create_texture2d() { return Texture2D(std::make_shared<TextureImpl>()); }
Framebuffer create_framebuffer() { return Framebuffer(std::make_shared<FramebufferImpl>()); }

}  // namespace pixcake::gl::qt
