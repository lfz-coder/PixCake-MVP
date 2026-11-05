#include "pixcake/gl/qt_backend.hpp"

#include <QCoreApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFunctions>
#include <QOpenGLShader>
#include <QOpenGLShaderProgram>
#include <QSurfaceFormat>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>
#include <QMatrix4x4>

#include <algorithm>
#include <limits>
#include <utility>

namespace pixcake::gl::qt {
namespace {

bool fail(std::string* error, std::string message) {
  if (error != nullptr) {
    *error = std::move(message);
  }
  return false;
}

QOpenGLFunctions* functions(std::string* error) {
  auto* context = QOpenGLContext::currentContext();
  if (context == nullptr) {
    fail(error, "no current OpenGL context");
    return nullptr;
  }
  return context->functions();
}

QOpenGLExtraFunctions* extra_functions(std::string* error) {
  auto* context = QOpenGLContext::currentContext();
  if (context == nullptr) {
    fail(error, "no current OpenGL context");
    return nullptr;
  }
  return context->extraFunctions();
}

QOpenGLShader::ShaderType shader_type(ShaderStage stage) {
  switch (stage) {
    case ShaderStage::Vertex:
      return QOpenGLShader::Vertex;
    case ShaderStage::Fragment:
      return QOpenGLShader::Fragment;
    case ShaderStage::Compute:
      return QOpenGLShader::Compute;
  }
  return QOpenGLShader::Vertex;
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

GLint internal_format(PixelFormat /*format*/) {
  return GL_RGBA8;
}

std::string shader_error(const QOpenGLShader& shader) {
  const auto log = shader.log();
  return log.isEmpty() ? "shader compilation failed" : log.toStdString();
}

std::string program_error(const QOpenGLShaderProgram& program) {
  const auto log = program.log();
  return log.isEmpty() ? "program link failed" : log.toStdString();
}

std::string uniform_name(std::string_view name) {
  return std::string(name.data(), name.size());
}

}  // namespace

struct Context::Impl {
  std::unique_ptr<QOpenGLContext> context;
  std::unique_ptr<QOffscreenSurface> surface;
  std::string error;
};

Context::Context() = default;
Context::Context(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Context::~Context() = default;
Context::Context(Context&&) noexcept = default;
Context& Context::operator=(Context&&) noexcept = default;

std::unique_ptr<Context> Context::create(std::string* error) {
  if (QCoreApplication::instance() == nullptr) {
    fail(error, "a QGuiApplication or QCoreApplication is required before creating a context");
    return nullptr;
  }

  auto impl = std::make_unique<Impl>();
  impl->context = std::make_unique<QOpenGLContext>();
  QSurfaceFormat format = QSurfaceFormat::defaultFormat();
  format.setRenderableType(QSurfaceFormat::OpenGL);
  if (format.majorVersion() < 3 || (format.majorVersion() == 3 && format.minorVersion() < 3)) {
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
  }
  impl->context->setFormat(format);
  if (!impl->context->create()) {
    const std::string message = "Qt failed to create an OpenGL context";
    impl->error = message;
    fail(error, message);
    return nullptr;
  }

  impl->surface = std::make_unique<QOffscreenSurface>();
  impl->surface->setFormat(impl->context->format());
  impl->surface->create();
  if (!impl->surface->isValid()) {
    const std::string message = "Qt failed to create an offscreen surface";
    impl->error = message;
    fail(error, message);
    return nullptr;
  }
  return std::unique_ptr<Context>(new Context(std::move(impl)));
}

bool Context::makeCurrent() {
  if (!impl_ || !impl_->context || !impl_->surface || !impl_->surface->isValid()) {
    return fail(impl_ ? &impl_->error : nullptr, "context is not initialized");
  }
  if (!impl_->context->makeCurrent(impl_->surface.get())) {
    impl_->error = "Qt failed to make the OpenGL context current";
    return false;
  }
  impl_->error.clear();
  return true;
}

void Context::doneCurrent() {
  if (impl_ && impl_->context) {
    impl_->context->doneCurrent();
  }
}

std::string Context::lastError() const {
  return impl_ ? impl_->error : "context is not initialized";
}

struct Shader::Impl {
  std::unique_ptr<QOpenGLShader> shader;
  std::string error;
};

Shader::Shader() : impl_(std::make_unique<Impl>()) {}
Shader::~Shader() = default;
Shader::Shader(Shader&&) noexcept = default;
Shader& Shader::operator=(Shader&&) noexcept = default;

bool Shader::compile(ShaderStage stage, std::string_view source, std::string* error) {
  if (source.empty()) {
    impl_->error = "shader source is empty";
    return fail(error, impl_->error);
  }
  auto shader = std::make_unique<QOpenGLShader>(shader_type(stage));
  const QByteArray source_bytes(source.data(), static_cast<int>(source.size()));
  if (!shader->compileSourceCode(source_bytes)) {
    impl_->error = shader_error(*shader);
    return fail(error, impl_->error);
  }
  impl_->shader = std::move(shader);
  impl_->error.clear();
  if (error != nullptr) {
    error->clear();
  }
  return true;
}

bool Shader::valid() const {
  return impl_ != nullptr && impl_->shader != nullptr && impl_->shader->isCompiled();
}

struct Program::Impl {
  std::unique_ptr<QOpenGLShaderProgram> program;
  std::string error;
  bool linked = false;
};

Program::Program() : impl_(std::make_unique<Impl>()) {}
Program::~Program() = default;
Program::Program(Program&&) noexcept = default;
Program& Program::operator=(Program&&) noexcept = default;

bool Program::link(const Shader& vertex, const Shader& fragment, std::string* error) {
  if (!vertex.valid() || !fragment.valid()) {
    impl_->error = "program link requires two compiled shaders";
    return fail(error, impl_->error);
  }
  auto program = std::make_unique<QOpenGLShaderProgram>();
  if (!program->addShader(vertex.impl_->shader.get()) ||
      !program->addShader(fragment.impl_->shader.get()) || !program->link()) {
    impl_->error = program_error(*program);
    return fail(error, impl_->error);
  }
  impl_->program = std::move(program);
  impl_->linked = true;
  impl_->error.clear();
  if (error != nullptr) {
    error->clear();
  }
  return true;
}

bool Program::link(const Shader& compute, std::string* error) {
  if (!compute.valid()) {
    impl_->error = "program link requires a compiled shader";
    return fail(error, impl_->error);
  }
  auto program = std::make_unique<QOpenGLShaderProgram>();
  if (!program->addShader(compute.impl_->shader.get()) || !program->link()) {
    impl_->error = program_error(*program);
    return fail(error, impl_->error);
  }
  impl_->program = std::move(program);
  impl_->linked = true;
  impl_->error.clear();
  if (error != nullptr) {
    error->clear();
  }
  return true;
}

bool Program::use(std::string* error) {
  if (!impl_->linked || !impl_->program || !impl_->program->bind()) {
    impl_->error = "program is not linked or could not be bound";
    return fail(error, impl_->error);
  }
  if (error != nullptr) {
    error->clear();
  }
  return true;
}

bool Program::setUniform(std::string_view name, float value, std::string* error) {
  if (!impl_->linked || !impl_->program) {
    return fail(error, "program is not linked");
  }
  const auto key = uniform_name(name);
  if (impl_->program->uniformLocation(key.c_str()) < 0) {
    return fail(error, "uniform not found: " + key);
  }
  impl_->program->setUniformValue(key.c_str(), value);
  return true;
}

bool Program::setUniform(std::string_view name, std::int32_t value, std::string* error) {
  if (!impl_->linked || !impl_->program) {
    return fail(error, "program is not linked");
  }
  const auto key = uniform_name(name);
  if (impl_->program->uniformLocation(key.c_str()) < 0) {
    return fail(error, "uniform not found: " + key);
  }
  impl_->program->setUniformValue(key.c_str(), static_cast<GLint>(value));
  return true;
}

bool Program::setUniform(std::string_view name, const Vec2& value, std::string* error) {
  if (!impl_->linked || !impl_->program) {
    return fail(error, "program is not linked");
  }
  const auto key = uniform_name(name);
  if (impl_->program->uniformLocation(key.c_str()) < 0) {
    return fail(error, "uniform not found: " + key);
  }
  impl_->program->setUniformValue(key.c_str(), QVector2D(value[0], value[1]));
  return true;
}

bool Program::setUniform(std::string_view name, const Vec3& value, std::string* error) {
  if (!impl_->linked || !impl_->program) {
    return fail(error, "program is not linked");
  }
  const auto key = uniform_name(name);
  if (impl_->program->uniformLocation(key.c_str()) < 0) {
    return fail(error, "uniform not found: " + key);
  }
  impl_->program->setUniformValue(key.c_str(), QVector3D(value[0], value[1], value[2]));
  return true;
}

bool Program::setUniform(std::string_view name, const Vec4& value, std::string* error) {
  if (!impl_->linked || !impl_->program) {
    return fail(error, "program is not linked");
  }
  const auto key = uniform_name(name);
  if (impl_->program->uniformLocation(key.c_str()) < 0) {
    return fail(error, "uniform not found: " + key);
  }
  impl_->program->setUniformValue(key.c_str(), QVector4D(value[0], value[1], value[2], value[3]));
  return true;
}

bool Program::setUniform(std::string_view name, const Mat4& value, std::string* error) {
  if (!impl_->linked || !impl_->program) {
    return fail(error, "program is not linked");
  }
  const auto key = uniform_name(name);
  if (impl_->program->uniformLocation(key.c_str()) < 0) {
    return fail(error, "uniform not found: " + key);
  }
  QMatrix4x4 matrix(value.data());
  impl_->program->setUniformValue(key.c_str(), matrix);
  return true;
}

bool Program::setTextureUnit(std::string_view name, std::uint32_t unit, std::string* error) {
  if (unit > static_cast<std::uint32_t>(std::numeric_limits<GLint>::max())) {
    return fail(error, "texture unit is out of range");
  }
  return setUniform(name, static_cast<std::int32_t>(unit), error);
}

struct Buffer::Impl {
  GLuint id = 0;
  std::size_t size = 0;
};

Buffer::Buffer() : impl_(std::make_unique<Impl>()) {}
Buffer::~Buffer() {
  if (impl_ && impl_->id != 0) {
    if (auto* f = functions(nullptr); f != nullptr) {
      f->glDeleteBuffers(1, &impl_->id);
    }
  }
}
Buffer::Buffer(Buffer&&) noexcept = default;
Buffer& Buffer::operator=(Buffer&&) noexcept = default;

bool Buffer::allocate(std::size_t size, BufferUsage usage, std::string* error) {
  auto* f = functions(error);
  if (f == nullptr || size > static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max())) {
    return f == nullptr ? false : fail(error, "buffer size is out of range");
  }
  if (impl_->id == 0) {
    f->glGenBuffers(1, &impl_->id);
  }
  f->glBindBuffer(GL_ARRAY_BUFFER, impl_->id);
  f->glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(size), nullptr, buffer_usage(usage));
  impl_->size = size;
  return true;
}

bool Buffer::write(std::size_t offset, const void* data, std::size_t size, std::string* error) {
  auto* f = functions(error);
  if (f == nullptr) {
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
  f->glBindBuffer(GL_ARRAY_BUFFER, impl_->id);
  f->glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size), data);
  return true;
}

bool Buffer::bind(std::string* error) {
  auto* f = functions(error);
  if (f == nullptr || impl_->id == 0) {
    return f == nullptr ? false : fail(error, "buffer is not allocated");
  }
  f->glBindBuffer(GL_ARRAY_BUFFER, impl_->id);
  return true;
}

std::size_t Buffer::size() const {
  return impl_ ? impl_->size : 0;
}

struct VertexArray::Impl {
  GLuint id = 0;
};

VertexArray::VertexArray() : impl_(std::make_unique<Impl>()) {}
VertexArray::~VertexArray() {
  if (impl_ && impl_->id != 0) {
    if (auto* f = extra_functions(nullptr); f != nullptr) {
      f->glDeleteVertexArrays(1, &impl_->id);
    }
  }
}
VertexArray::VertexArray(VertexArray&&) noexcept = default;
VertexArray& VertexArray::operator=(VertexArray&&) noexcept = default;

bool VertexArray::bind(std::string* error) {
  auto* f = extra_functions(error);
  if (f == nullptr) {
    return false;
  }
  if (impl_->id == 0) {
    f->glGenVertexArrays(1, &impl_->id);
  }
  f->glBindVertexArray(impl_->id);
  return true;
}

bool VertexArray::enableAttribute(std::uint32_t index, std::int32_t components, AttributeType type,
                                  std::size_t stride, std::size_t offset, bool normalized,
                                  std::string* error) {
  if (components < 1 || components > 4 || stride > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())) {
    return fail(error, "vertex attribute layout is out of range");
  }
  auto* f = extra_functions(error);
  if (f == nullptr || !bind(error)) {
    return false;
  }
  f->glEnableVertexAttribArray(index);
  f->glVertexAttribPointer(index, components, attribute_type(type), normalized ? GL_TRUE : GL_FALSE,
                            static_cast<GLsizei>(stride), reinterpret_cast<const void*>(offset));
  return true;
}

struct Texture2D::Impl {
  GLuint id = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  PixelFormat format = PixelFormat::RGBA8;
};

Texture2D::Texture2D() : impl_(std::make_unique<Impl>()) {}
Texture2D::~Texture2D() {
  if (impl_ && impl_->id != 0) {
    if (auto* f = functions(nullptr); f != nullptr) {
      f->glDeleteTextures(1, &impl_->id);
    }
  }
}
Texture2D::Texture2D(Texture2D&&) noexcept = default;
Texture2D& Texture2D::operator=(Texture2D&&) noexcept = default;

bool Texture2D::allocate(std::uint32_t width, std::uint32_t height, PixelFormat format,
                         std::string* error) {
  if (width == 0 || height == 0 || width > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max()) ||
      height > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max())) {
    return fail(error, "texture dimensions are out of range");
  }
  auto* f = functions(error);
  if (f == nullptr) {
    return false;
  }
  if (impl_->id == 0) {
    f->glGenTextures(1, &impl_->id);
  }
  f->glBindTexture(GL_TEXTURE_2D, impl_->id);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  f->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  f->glTexImage2D(GL_TEXTURE_2D, 0, internal_format(format), static_cast<GLsizei>(width),
                  static_cast<GLsizei>(height), 0, source_format(format), GL_UNSIGNED_BYTE, nullptr);
  f->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  impl_->width = width;
  impl_->height = height;
  impl_->format = format;
  return true;
}

bool Texture2D::upload(const PixelUpload& upload, std::string* error) {
  if (impl_->id == 0 || upload.width != impl_->width || upload.height != impl_->height ||
      upload.format != impl_->format) {
    return fail(error, "texture upload does not match the allocated texture");
  }
  const auto packed_stride = static_cast<std::size_t>(upload.width) * 4U;
  if (upload.row_stride < packed_stride || upload.row_stride % 4U != 0U ||
      (upload.width != 0 && upload.height != 0 && upload.data == nullptr)) {
    return fail(error, "texture upload has invalid data or row stride");
  }
  auto* f = functions(error);
  if (f == nullptr) {
    return false;
  }
  f->glBindTexture(GL_TEXTURE_2D, impl_->id);
  f->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  if (upload.row_stride != packed_stride) {
    f->glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(upload.row_stride / 4U));
  }
  f->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(upload.width),
                     static_cast<GLsizei>(upload.height), source_format(upload.format), GL_UNSIGNED_BYTE,
                     upload.data);
  if (upload.row_stride != packed_stride) {
    f->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  }
  f->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  return true;
}

bool Texture2D::bind(std::uint32_t unit, std::string* error) {
  if (impl_->id == 0) {
    return fail(error, "texture is not allocated");
  }
  auto* f = functions(error);
  if (f == nullptr) {
    return false;
  }
  f->glActiveTexture(GL_TEXTURE0 + unit);
  f->glBindTexture(GL_TEXTURE_2D, impl_->id);
  return true;
}

std::uint32_t Texture2D::width() const { return impl_ ? impl_->width : 0; }
std::uint32_t Texture2D::height() const { return impl_ ? impl_->height : 0; }

struct Framebuffer::Impl {
  GLuint id = 0;
};

Framebuffer::Framebuffer() : impl_(std::make_unique<Impl>()) {}
Framebuffer::~Framebuffer() {
  if (impl_ && impl_->id != 0) {
    if (auto* f = functions(nullptr); f != nullptr) {
      f->glDeleteFramebuffers(1, &impl_->id);
    }
  }
}
Framebuffer::Framebuffer(Framebuffer&&) noexcept = default;
Framebuffer& Framebuffer::operator=(Framebuffer&&) noexcept = default;

bool Framebuffer::attachColor(const Texture2D& texture, std::string* error) {
  if (texture.impl_ == nullptr || texture.impl_->id == 0) {
    return fail(error, "cannot attach an unallocated texture");
  }
  auto* f = functions(error);
  if (f == nullptr) {
    return false;
  }
  if (impl_->id == 0) {
    f->glGenFramebuffers(1, &impl_->id);
  }
  f->glBindFramebuffer(GL_FRAMEBUFFER, impl_->id);
  f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture.impl_->id, 0);
  return true;
}

bool Framebuffer::complete(std::string* error) const {
  if (impl_ == nullptr || impl_->id == 0) {
    return fail(error, "framebuffer is not allocated");
  }
  auto* f = functions(error);
  if (f == nullptr) {
    return false;
  }
  f->glBindFramebuffer(GL_FRAMEBUFFER, impl_->id);
  const auto status = f->glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    return fail(error, "framebuffer is incomplete (status " + std::to_string(status) + ")");
  }
  return true;
}

bool Framebuffer::bind(std::string* error) {
  auto* f = functions(error);
  if (f == nullptr || impl_->id == 0) {
    return f == nullptr ? false : fail(error, "framebuffer is not allocated");
  }
  f->glBindFramebuffer(GL_FRAMEBUFFER, impl_->id);
  return true;
}

struct Fence::Impl {
  GLsync sync = nullptr;
};

Fence::Fence() : impl_(std::make_unique<Impl>()) {}
Fence::~Fence() {
  if (impl_ && impl_->sync != nullptr) {
    if (auto* f = extra_functions(nullptr); f != nullptr) {
      f->glDeleteSync(impl_->sync);
    }
  }
}
Fence::Fence(Fence&&) noexcept = default;
Fence& Fence::operator=(Fence&&) noexcept = default;

bool Fence::insert(std::string* error) {
  auto* f = extra_functions(error);
  if (f == nullptr) {
    return false;
  }
  if (impl_->sync != nullptr) {
    f->glDeleteSync(impl_->sync);
  }
  impl_->sync = f->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  if (impl_->sync == nullptr) {
    return fail(error, "failed to insert GPU fence");
  }
  return true;
}

bool Fence::wait(std::uint64_t timeout_nanoseconds, std::string* error) {
  if (impl_->sync == nullptr) {
    return fail(error, "fence has not been inserted");
  }
  auto* f = extra_functions(error);
  if (f == nullptr) {
    return false;
  }
  const auto status = f->glClientWaitSync(impl_->sync, 0, static_cast<GLuint64>(timeout_nanoseconds));
  if (status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED) {
    return true;
  }
  if (status == GL_TIMEOUT_EXPIRED) {
    return fail(error, "GPU fence wait timed out");
  }
  return fail(error, "GPU fence wait failed");
}

}  // namespace pixcake::gl::qt
