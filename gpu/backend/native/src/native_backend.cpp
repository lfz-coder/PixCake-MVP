#include "pixcake/gl/native_backend.hpp"

#include <glad/gl.h>

#if defined(__APPLE__)
// Apple marks the legacy OpenGL framework deprecated, but it remains the
// platform's native Core-profile context API. GL calls are loaded through
// Glad so this translation unit never depends on Qt or a platform GL header.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#include <OpenGL/OpenGL.h>
#pragma clang diagnostic pop
#endif

#include <algorithm>
#include <cstddef>
#include <limits>
#include <utility>

namespace pixcake::gl::native {
namespace {

const char* unsupported_message() {
#if defined(__APPLE__)
  return "native Core OpenGL backend is unavailable";
#else
  return "native Core OpenGL backend is not implemented for this platform";
#endif
}

bool fail(std::string* error, std::string message) {
  if (error != nullptr) {
    *error = std::move(message);
  }
  return false;
}

[[maybe_unused]] bool unsupported(std::string* error) {
  return fail(error, unsupported_message());
}

#if defined(__APPLE__)
bool require_context(std::string* error) {
  if (CGLGetCurrentContext() == nullptr) {
    return fail(error, "no current native OpenGL context");
  }
  if (GLAD_GL_VERSION_3_3 == 0) {
    return fail(error, "native OpenGL Core 3.3 functions are not loaded");
  }
  return true;
}

std::string cgl_error(CGLError error) {
  const char* message = CGLErrorString(error);
  return message == nullptr ? "native OpenGL context error" : std::string(message);
}

GLenum shader_type(ShaderStage stage) {
  switch (stage) {
    case ShaderStage::Vertex:
      return GL_VERTEX_SHADER;
    case ShaderStage::Fragment:
      return GL_FRAGMENT_SHADER;
    case ShaderStage::Compute:
      return 0;
  }
  return GL_VERTEX_SHADER;
}

GLenum buffer_usage(BufferUsage usage) {
  switch (usage) {
    case BufferUsage::StaticDraw:
      return GL_STATIC_DRAW;
    case BufferUsage::DynamicDraw:
      return GL_DYNAMIC_DRAW;
    case BufferUsage::StreamDraw:
      return GL_STREAM_DRAW;
  }
  return GL_STATIC_DRAW;
}

GLenum attribute_type(AttributeType type) {
  switch (type) {
    case AttributeType::Float32:
      return GL_FLOAT;
    case AttributeType::UInt8:
      return GL_UNSIGNED_BYTE;
  }
  return GL_FLOAT;
}

GLenum source_format(PixelFormat format) {
  return format == PixelFormat::BGRA8 ? GL_BGRA : GL_RGBA;
}

std::string shader_log(GLuint shader) {
  GLint length = 0;
  glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
  if (length <= 1) {
    return "shader compilation failed";
  }
  std::string log(static_cast<std::size_t>(length), '\0');
  GLsizei written = 0;
  glGetShaderInfoLog(shader, length, &written, log.data());
  log.resize(static_cast<std::size_t>(std::max<GLsizei>(written, 0)));
  return log;
}

std::string program_log(GLuint program) {
  GLint length = 0;
  glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
  if (length <= 1) {
    return "program link failed";
  }
  std::string log(static_cast<std::size_t>(length), '\0');
  GLsizei written = 0;
  glGetProgramInfoLog(program, length, &written, log.data());
  log.resize(static_cast<std::size_t>(std::max<GLsizei>(written, 0)));
  return log;
}

std::string uniform_name(std::string_view name) {
  return std::string(name.data(), name.size());
}

bool uniform_location(GLuint program, std::string_view name, GLint& location, std::string* error) {
  const auto key = uniform_name(name);
  location = glGetUniformLocation(program, key.c_str());
  if (location < 0) {
    return fail(error, "uniform not found: " + key);
  }
  return true;
}
#endif

}  // namespace

struct Context::Impl {
#if defined(__APPLE__)
  CGLPixelFormatObj pixel_format = nullptr;
  CGLContextObj context = nullptr;
#endif
  bool functions_loaded = false;
  std::string error;

  ~Impl() {
#if defined(__APPLE__)
    if (context != nullptr) {
      if (CGLGetCurrentContext() == context) {
        CGLSetCurrentContext(nullptr);
      }
      CGLDestroyContext(context);
    }
    if (pixel_format != nullptr) {
      CGLDestroyPixelFormat(pixel_format);
    }
#endif
  }
};

Context::Context() : impl_(std::make_unique<Impl>()) {}
Context::Context(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Context::~Context() = default;
Context::Context(Context&&) noexcept = default;
Context& Context::operator=(Context&&) noexcept = default;

std::unique_ptr<Context> Context::create(std::string* error) {
#if defined(__APPLE__)
  auto impl = std::make_unique<Impl>();
  const CGLPixelFormatAttribute attributes[] = {
      kCGLPFAAccelerated,
      kCGLPFAOpenGLProfile,
      static_cast<CGLPixelFormatAttribute>(kCGLOGLPVersion_3_2_Core),
      kCGLPFAColorSize,
      static_cast<CGLPixelFormatAttribute>(24),
      static_cast<CGLPixelFormatAttribute>(0),
  };
  GLint format_count = 0;
  const auto format_error = CGLChoosePixelFormat(attributes, &impl->pixel_format, &format_count);
  if (format_error != kCGLNoError || impl->pixel_format == nullptr || format_count == 0) {
    impl->error = "CGLChoosePixelFormat failed: " + cgl_error(format_error);
    fail(error, impl->error);
    return nullptr;
  }
  const auto context_error = CGLCreateContext(impl->pixel_format, nullptr, &impl->context);
  if (context_error != kCGLNoError || impl->context == nullptr) {
    impl->error = "CGLCreateContext failed: " + cgl_error(context_error);
    fail(error, impl->error);
    return nullptr;
  }
  return std::unique_ptr<Context>(new Context(std::move(impl)));
#else
  (void)error;
  unsupported(error);
  return nullptr;
#endif
}

bool Context::makeCurrent() {
#if defined(__APPLE__)
  if (!impl_ || impl_->context == nullptr) {
    return fail(impl_ ? &impl_->error : nullptr, "context is not initialized");
  }
  const auto result = CGLSetCurrentContext(impl_->context);
  if (result != kCGLNoError) {
    impl_->error = "CGLSetCurrentContext failed: " + cgl_error(result);
    return false;
  }
  const int version = gladLoaderLoadGL();
  if (version < GLAD_MAKE_VERSION(3, 3) || GLAD_GL_VERSION_3_3 == 0) {
    CGLSetCurrentContext(nullptr);
    impl_->functions_loaded = false;
    impl_->error = "native OpenGL Core 3.3 functions are unavailable";
    return false;
  }
  impl_->functions_loaded = true;
  impl_->error.clear();
  return true;
#else
  return unsupported(impl_ ? &impl_->error : nullptr);
#endif
}

void Context::doneCurrent() {
#if defined(__APPLE__)
  if (impl_ && impl_->context != nullptr && CGLGetCurrentContext() == impl_->context) {
    CGLSetCurrentContext(nullptr);
  }
#endif
}

std::string Context::lastError() const {
  return impl_ ? impl_->error : "context is not initialized";
}

struct Shader::Impl {
#if defined(__APPLE__)
  GLuint shader = 0;
#endif
  bool compiled = false;

  ~Impl() {
#if defined(__APPLE__)
    if (shader != 0 && CGLGetCurrentContext() != nullptr) {
      glDeleteShader(shader);
    }
#endif
  }
};

Shader::Shader() : impl_(std::make_unique<Impl>()) {}
Shader::~Shader() = default;
Shader::Shader(Shader&&) noexcept = default;
Shader& Shader::operator=(Shader&&) noexcept = default;

bool Shader::compile(ShaderStage stage, std::string_view source, std::string* error) {
#if defined(__APPLE__)
  if (!require_context(error)) {
    return false;
  }
  if (source.empty()) {
    impl_->compiled = false;
    return fail(error, "shader source is empty");
  }
  if (stage == ShaderStage::Compute) {
    impl_->compiled = false;
    return fail(error, "compute shaders require OpenGL 4.3; this backend targets Core 3.3");
  }
  if (impl_->shader != 0) {
    glDeleteShader(impl_->shader);
    impl_->shader = 0;
  }
  impl_->shader = glCreateShader(shader_type(stage));
  if (impl_->shader == 0) {
    impl_->compiled = false;
    return fail(error, "glCreateShader failed");
  }
  const auto length = static_cast<GLint>(source.size());
  const char* text = source.data();
  glShaderSource(impl_->shader, 1, &text, &length);
  glCompileShader(impl_->shader);
  GLint status = GL_FALSE;
  glGetShaderiv(impl_->shader, GL_COMPILE_STATUS, &status);
  if (status != GL_TRUE) {
    impl_->compiled = false;
    return fail(error, shader_log(impl_->shader));
  }
  impl_->compiled = true;
  if (error != nullptr) {
    error->clear();
  }
  return true;
#else
  (void)stage;
  (void)source;
  impl_->compiled = false;
  return unsupported(error);
#endif
}

bool Shader::valid() const {
  return impl_ != nullptr && impl_->compiled;
}

struct Program::Impl {
#if defined(__APPLE__)
  GLuint program = 0;
#endif
  bool linked = false;

  ~Impl() {
#if defined(__APPLE__)
    if (program != 0 && CGLGetCurrentContext() != nullptr) {
      glDeleteProgram(program);
    }
#endif
  }
};

Program::Program() : impl_(std::make_unique<Impl>()) {}
Program::~Program() = default;
Program::Program(Program&&) noexcept = default;
Program& Program::operator=(Program&&) noexcept = default;

bool Program::link(const Shader& vertex, const Shader& fragment, std::string* error) {
#if defined(__APPLE__)
  if (!require_context(error)) {
    return false;
  }
  if (!vertex.valid() || !fragment.valid()) {
    return fail(error, "program link requires two compiled shaders");
  }
  if (impl_->program != 0) {
    glDeleteProgram(impl_->program);
  }
  impl_->program = glCreateProgram();
  if (impl_->program == 0) {
    impl_->linked = false;
    return fail(error, "glCreateProgram failed");
  }
  glAttachShader(impl_->program, vertex.impl_->shader);
  glAttachShader(impl_->program, fragment.impl_->shader);
  glLinkProgram(impl_->program);
  GLint status = GL_FALSE;
  glGetProgramiv(impl_->program, GL_LINK_STATUS, &status);
  if (status != GL_TRUE) {
    impl_->linked = false;
    return fail(error, program_log(impl_->program));
  }
  impl_->linked = true;
  if (error != nullptr) {
    error->clear();
  }
  return true;
#else
  (void)vertex;
  (void)fragment;
  return unsupported(error);
#endif
}

bool Program::link(const Shader& compute, std::string* error) {
#if defined(__APPLE__)
  if (!require_context(error)) {
    return false;
  }
  if (!compute.valid()) {
    return fail(error, "program link requires a compiled shader");
  }
  if (impl_->program != 0) {
    glDeleteProgram(impl_->program);
  }
  impl_->program = glCreateProgram();
  if (impl_->program == 0) {
    impl_->linked = false;
    return fail(error, "glCreateProgram failed");
  }
  glAttachShader(impl_->program, compute.impl_->shader);
  glLinkProgram(impl_->program);
  GLint status = GL_FALSE;
  glGetProgramiv(impl_->program, GL_LINK_STATUS, &status);
  if (status != GL_TRUE) {
    impl_->linked = false;
    return fail(error, program_log(impl_->program));
  }
  impl_->linked = true;
  if (error != nullptr) {
    error->clear();
  }
  return true;
#else
  (void)compute;
  return unsupported(error);
#endif
}

bool Program::use(std::string* error) {
#if defined(__APPLE__)
  if (!require_context(error)) {
    return false;
  }
  if (!impl_->linked || impl_->program == 0) {
    return fail(error, "program is not linked");
  }
  glUseProgram(impl_->program);
  return true;
#else
  return unsupported(error);
#endif
}

bool Program::setUniform(std::string_view name, float value, std::string* error) {
#if defined(__APPLE__)
  if (!require_context(error) || !impl_->linked || impl_->program == 0) {
    return impl_->linked ? false : fail(error, "program is not linked");
  }
  GLint location = -1;
  if (!uniform_location(impl_->program, name, location, error)) {
    return false;
  }
  glUniform1f(location, value);
  return true;
#else
  (void)name;
  (void)value;
  return unsupported(error);
#endif
}

bool Program::setUniform(std::string_view name, std::int32_t value, std::string* error) {
#if defined(__APPLE__)
  if (!require_context(error) || !impl_->linked || impl_->program == 0) {
    return impl_->linked ? false : fail(error, "program is not linked");
  }
  GLint location = -1;
  if (!uniform_location(impl_->program, name, location, error)) {
    return false;
  }
  glUniform1i(location, static_cast<GLint>(value));
  return true;
#else
  (void)name;
  (void)value;
  return unsupported(error);
#endif
}

bool Program::setUniform(std::string_view name, const Vec2& value, std::string* error) {
#if defined(__APPLE__)
  if (!require_context(error) || !impl_->linked || impl_->program == 0) {
    return impl_->linked ? false : fail(error, "program is not linked");
  }
  GLint location = -1;
  if (!uniform_location(impl_->program, name, location, error)) {
    return false;
  }
  glUniform2fv(location, 1, value.data());
  return true;
#else
  (void)name;
  (void)value;
  return unsupported(error);
#endif
}

bool Program::setUniform(std::string_view name, const Vec3& value, std::string* error) {
#if defined(__APPLE__)
  if (!require_context(error) || !impl_->linked || impl_->program == 0) {
    return impl_->linked ? false : fail(error, "program is not linked");
  }
  GLint location = -1;
  if (!uniform_location(impl_->program, name, location, error)) {
    return false;
  }
  glUniform3fv(location, 1, value.data());
  return true;
#else
  (void)name;
  (void)value;
  return unsupported(error);
#endif
}

bool Program::setUniform(std::string_view name, const Vec4& value, std::string* error) {
#if defined(__APPLE__)
  if (!require_context(error) || !impl_->linked || impl_->program == 0) {
    return impl_->linked ? false : fail(error, "program is not linked");
  }
  GLint location = -1;
  if (!uniform_location(impl_->program, name, location, error)) {
    return false;
  }
  glUniform4fv(location, 1, value.data());
  return true;
#else
  (void)name;
  (void)value;
  return unsupported(error);
#endif
}

bool Program::setUniform(std::string_view name, const Mat4& value, std::string* error) {
#if defined(__APPLE__)
  if (!require_context(error) || !impl_->linked || impl_->program == 0) {
    return impl_->linked ? false : fail(error, "program is not linked");
  }
  GLint location = -1;
  if (!uniform_location(impl_->program, name, location, error)) {
    return false;
  }
  glUniformMatrix4fv(location, 1, GL_FALSE, value.data());
  return true;
#else
  (void)name;
  (void)value;
  return unsupported(error);
#endif
}

bool Program::setTextureUnit(std::string_view name, std::uint32_t unit, std::string* error) {
  if (unit > static_cast<std::uint32_t>(std::numeric_limits<GLint>::max())) {
    return fail(error, "texture unit is out of range");
  }
  return setUniform(name, static_cast<std::int32_t>(unit), error);
}

struct Buffer::Impl {
#if defined(__APPLE__)
  GLuint id = 0;
#endif
  std::size_t size = 0;

  ~Impl() {
#if defined(__APPLE__)
    if (id != 0 && CGLGetCurrentContext() != nullptr) {
      glDeleteBuffers(1, &id);
    }
#endif
  }
};

Buffer::Buffer() : impl_(std::make_unique<Impl>()) {}
Buffer::~Buffer() = default;
Buffer::Buffer(Buffer&&) noexcept = default;
Buffer& Buffer::operator=(Buffer&&) noexcept = default;

bool Buffer::allocate(std::size_t size, BufferUsage usage, std::string* error) {
#if defined(__APPLE__)
  if (!require_context(error)) {
    return false;
  }
  if (size > static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max())) {
    return fail(error, "buffer size is out of range");
  }
  if (impl_->id == 0) {
    glGenBuffers(1, &impl_->id);
  }
  glBindBuffer(GL_ARRAY_BUFFER, impl_->id);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(size), nullptr, buffer_usage(usage));
  impl_->size = size;
  return true;
#else
  (void)size;
  (void)usage;
  return unsupported(error);
#endif
}

bool Buffer::write(std::size_t offset, const void* data, std::size_t size, std::string* error) {
#if defined(__APPLE__)
  if (!require_context(error)) {
    return false;
  }
  if (size > 0 && data == nullptr) {
    return fail(error, "buffer write data is null");
  }
  if (offset > impl_->size || size > impl_->size - offset ||
      size > static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max())) {
    return fail(error, "buffer write exceeds allocation");
  }
  if (impl_->id == 0) {
    return fail(error, "buffer is not allocated");
  }
  glBindBuffer(GL_ARRAY_BUFFER, impl_->id);
  glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size), data);
  return true;
#else
  (void)offset;
  (void)data;
  (void)size;
  return unsupported(error);
#endif
}

bool Buffer::bind(std::string* error) {
#if defined(__APPLE__)
  if (!require_context(error)) {
    return false;
  }
  if (impl_->id == 0) {
    return fail(error, "buffer is not allocated");
  }
  glBindBuffer(GL_ARRAY_BUFFER, impl_->id);
  return true;
#else
  return unsupported(error);
#endif
}

std::size_t Buffer::size() const { return impl_ ? impl_->size : 0; }

struct VertexArray::Impl {
#if defined(__APPLE__)
  GLuint id = 0;
#endif

  ~Impl() {
#if defined(__APPLE__)
    if (id != 0 && CGLGetCurrentContext() != nullptr) {
      glDeleteVertexArrays(1, &id);
    }
#endif
  }
};

VertexArray::VertexArray() : impl_(std::make_unique<Impl>()) {}
VertexArray::~VertexArray() = default;
VertexArray::VertexArray(VertexArray&&) noexcept = default;
VertexArray& VertexArray::operator=(VertexArray&&) noexcept = default;

bool VertexArray::bind(std::string* error) {
#if defined(__APPLE__)
  if (!require_context(error)) {
    return false;
  }
  if (impl_->id == 0) {
    glGenVertexArrays(1, &impl_->id);
  }
  glBindVertexArray(impl_->id);
  return true;
#else
  return unsupported(error);
#endif
}

bool VertexArray::enableAttribute(std::uint32_t index, std::int32_t components, AttributeType type,
                                  std::size_t stride, std::size_t offset, bool normalized,
                                  std::string* error) {
#if defined(__APPLE__)
  if (components < 1 || components > 4 ||
      stride > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())) {
    return fail(error, "vertex attribute layout is out of range");
  }
  if (!bind(error)) {
    return false;
  }
  glEnableVertexAttribArray(index);
  glVertexAttribPointer(index, components, attribute_type(type), normalized ? GL_TRUE : GL_FALSE,
                        static_cast<GLsizei>(stride), reinterpret_cast<const void*>(offset));
  return true;
#else
  (void)index;
  (void)components;
  (void)type;
  (void)stride;
  (void)offset;
  (void)normalized;
  return unsupported(error);
#endif
}

struct Texture2D::Impl {
#if defined(__APPLE__)
  GLuint id = 0;
#endif
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  PixelFormat format = PixelFormat::RGBA8;

  ~Impl() {
#if defined(__APPLE__)
    if (id != 0 && CGLGetCurrentContext() != nullptr) {
      glDeleteTextures(1, &id);
    }
#endif
  }
};

Texture2D::Texture2D() : impl_(std::make_unique<Impl>()) {}
Texture2D::~Texture2D() = default;
Texture2D::Texture2D(Texture2D&&) noexcept = default;
Texture2D& Texture2D::operator=(Texture2D&&) noexcept = default;

bool Texture2D::allocate(std::uint32_t width, std::uint32_t height, PixelFormat format,
                         std::string* error) {
#if defined(__APPLE__)
  if (width == 0 || height == 0 ||
      width > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max()) ||
      height > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max())) {
    return fail(error, "texture dimensions are out of range");
  }
  if (!require_context(error)) {
    return false;
  }
  if (impl_->id == 0) {
    glGenTextures(1, &impl_->id);
  }
  glBindTexture(GL_TEXTURE_2D, impl_->id);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0,
               source_format(format), GL_UNSIGNED_BYTE, nullptr);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  impl_->width = width;
  impl_->height = height;
  impl_->format = format;
  return true;
#else
  (void)width;
  (void)height;
  (void)format;
  return unsupported(error);
#endif
}

bool Texture2D::upload(const PixelUpload& upload, std::string* error) {
#if defined(__APPLE__)
  if (!require_context(error)) {
    return false;
  }
  if (impl_->id == 0 || upload.width != impl_->width || upload.height != impl_->height ||
      upload.format != impl_->format) {
    return fail(error, "texture upload does not match the allocated texture");
  }
  const auto packed_stride = static_cast<std::size_t>(upload.width) * 4U;
  if (upload.row_stride < packed_stride || upload.row_stride % 4U != 0U ||
      (upload.width != 0 && upload.height != 0 && upload.data == nullptr)) {
    return fail(error, "texture upload has invalid data or row stride");
  }
  glBindTexture(GL_TEXTURE_2D, impl_->id);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  if (upload.row_stride != packed_stride) {
    glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(upload.row_stride / 4U));
  }
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(upload.width),
                  static_cast<GLsizei>(upload.height), source_format(upload.format), GL_UNSIGNED_BYTE,
                  upload.data);
  if (upload.row_stride != packed_stride) {
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  }
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  return true;
#else
  (void)upload;
  return unsupported(error);
#endif
}

bool Texture2D::bind(std::uint32_t unit, std::string* error) {
#if defined(__APPLE__)
  if (!require_context(error)) {
    return false;
  }
  if (impl_->id == 0) {
    return fail(error, "texture is not allocated");
  }
  glActiveTexture(GL_TEXTURE0 + unit);
  glBindTexture(GL_TEXTURE_2D, impl_->id);
  return true;
#else
  (void)unit;
  return unsupported(error);
#endif
}

std::uint32_t Texture2D::width() const { return impl_ ? impl_->width : 0; }
std::uint32_t Texture2D::height() const { return impl_ ? impl_->height : 0; }

struct Framebuffer::Impl {
#if defined(__APPLE__)
  GLuint id = 0;
#endif

  ~Impl() {
#if defined(__APPLE__)
    if (id != 0 && CGLGetCurrentContext() != nullptr) {
      glDeleteFramebuffers(1, &id);
    }
#endif
  }
};

Framebuffer::Framebuffer() : impl_(std::make_unique<Impl>()) {}
Framebuffer::~Framebuffer() = default;
Framebuffer::Framebuffer(Framebuffer&&) noexcept = default;
Framebuffer& Framebuffer::operator=(Framebuffer&&) noexcept = default;

bool Framebuffer::attachColor(const Texture2D& texture, std::string* error) {
#if defined(__APPLE__)
  if (!require_context(error)) {
    return false;
  }
  if (texture.impl_ == nullptr || texture.impl_->id == 0) {
    return fail(error, "cannot attach an unallocated texture");
  }
  if (impl_->id == 0) {
    glGenFramebuffers(1, &impl_->id);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, impl_->id);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture.impl_->id, 0);
  return true;
#else
  (void)texture;
  return unsupported(error);
#endif
}

bool Framebuffer::complete(std::string* error) const {
#if defined(__APPLE__)
  if (!require_context(error)) {
    return false;
  }
  if (impl_ == nullptr || impl_->id == 0) {
    return fail(error, "framebuffer is not allocated");
  }
  glBindFramebuffer(GL_FRAMEBUFFER, impl_->id);
  const auto status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    return fail(error, "framebuffer is incomplete (status " + std::to_string(status) + ")");
  }
  return true;
#else
  return unsupported(error);
#endif
}

bool Framebuffer::bind(std::string* error) {
#if defined(__APPLE__)
  if (!require_context(error)) {
    return false;
  }
  if (impl_->id == 0) {
    return fail(error, "framebuffer is not allocated");
  }
  glBindFramebuffer(GL_FRAMEBUFFER, impl_->id);
  return true;
#else
  return unsupported(error);
#endif
}

struct Fence::Impl {
#if defined(__APPLE__)
  GLsync sync = nullptr;
#endif

  ~Impl() {
#if defined(__APPLE__)
    if (sync != nullptr && CGLGetCurrentContext() != nullptr) {
      glDeleteSync(sync);
    }
#endif
  }
};

Fence::Fence() : impl_(std::make_unique<Impl>()) {}
Fence::~Fence() = default;
Fence::Fence(Fence&&) noexcept = default;
Fence& Fence::operator=(Fence&&) noexcept = default;

bool Fence::insert(std::string* error) {
#if defined(__APPLE__)
  if (!require_context(error)) {
    return false;
  }
  if (impl_->sync != nullptr) {
    glDeleteSync(impl_->sync);
  }
  impl_->sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  if (impl_->sync == nullptr) {
    return fail(error, "failed to insert GPU fence");
  }
  return true;
#else
  return unsupported(error);
#endif
}

bool Fence::wait(std::uint64_t timeout_nanoseconds, std::string* error) {
#if defined(__APPLE__)
  if (!require_context(error)) {
    return false;
  }
  if (impl_->sync == nullptr) {
    return fail(error, "fence has not been inserted");
  }
  glFlush();
  const auto status = glClientWaitSync(impl_->sync, 0, static_cast<GLuint64>(timeout_nanoseconds));
  if (status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED) {
    return true;
  }
  if (status == GL_TIMEOUT_EXPIRED) {
    return fail(error, "GPU fence wait timed out");
  }
  return fail(error, "GPU fence wait failed");
#else
  (void)timeout_nanoseconds;
  return unsupported(error);
#endif
}

}  // namespace pixcake::gl::native
