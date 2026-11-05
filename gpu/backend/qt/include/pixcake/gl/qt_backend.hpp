#pragma once

#include "pixcake/gl/gl_types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace pixcake::gl::qt {

class Context {
 public:
  Context();
  ~Context();
  Context(Context&&) noexcept;
  Context& operator=(Context&&) noexcept;
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;

  static std::unique_ptr<Context> create(std::string* error = nullptr);
  bool makeCurrent();
  void doneCurrent();
  std::string lastError() const;

 private:
  struct Impl;
  explicit Context(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

class Shader {
 public:
  Shader();
  ~Shader();
  Shader(Shader&&) noexcept;
  Shader& operator=(Shader&&) noexcept;
  Shader(const Shader&) = delete;
  Shader& operator=(const Shader&) = delete;

  bool compile(ShaderStage stage, std::string_view source, std::string* error = nullptr);
  bool valid() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  friend class Program;
};

class Program {
 public:
  Program();
  ~Program();
  Program(Program&&) noexcept;
  Program& operator=(Program&&) noexcept;
  Program(const Program&) = delete;
  Program& operator=(const Program&) = delete;

  bool link(const Shader& vertex, const Shader& fragment, std::string* error = nullptr);
  bool link(const Shader& compute, std::string* error = nullptr);
  bool use(std::string* error = nullptr);

  bool setUniform(std::string_view name, float value, std::string* error = nullptr);
  bool setUniform(std::string_view name, std::int32_t value, std::string* error = nullptr);
  bool setUniform(std::string_view name, const Vec2& value, std::string* error = nullptr);
  bool setUniform(std::string_view name, const Vec3& value, std::string* error = nullptr);
  bool setUniform(std::string_view name, const Vec4& value, std::string* error = nullptr);
  bool setUniform(std::string_view name, const Mat4& value, std::string* error = nullptr);
  bool setTextureUnit(std::string_view name, std::uint32_t unit, std::string* error = nullptr);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class Buffer {
 public:
  Buffer();
  ~Buffer();
  Buffer(Buffer&&) noexcept;
  Buffer& operator=(Buffer&&) noexcept;
  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;

  bool allocate(std::size_t size, BufferUsage usage = BufferUsage::StaticDraw,
                std::string* error = nullptr);
  bool write(std::size_t offset, const void* data, std::size_t size,
             std::string* error = nullptr);
  bool bind(std::string* error = nullptr);
  std::size_t size() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class VertexArray {
 public:
  VertexArray();
  ~VertexArray();
  VertexArray(VertexArray&&) noexcept;
  VertexArray& operator=(VertexArray&&) noexcept;
  VertexArray(const VertexArray&) = delete;
  VertexArray& operator=(const VertexArray&) = delete;

  bool bind(std::string* error = nullptr);
  bool enableAttribute(std::uint32_t index, std::int32_t components, AttributeType type,
                       std::size_t stride, std::size_t offset, bool normalized = false,
                       std::string* error = nullptr);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class Texture2D {
 public:
  Texture2D();
  ~Texture2D();
  Texture2D(Texture2D&&) noexcept;
  Texture2D& operator=(Texture2D&&) noexcept;
  Texture2D(const Texture2D&) = delete;
  Texture2D& operator=(const Texture2D&) = delete;

  bool allocate(std::uint32_t width, std::uint32_t height, PixelFormat format = PixelFormat::RGBA8,
                std::string* error = nullptr);
  bool upload(const PixelUpload& upload, std::string* error = nullptr);
  bool bind(std::uint32_t unit = 0, std::string* error = nullptr);
  std::uint32_t width() const;
  std::uint32_t height() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  friend class Framebuffer;
};

class Framebuffer {
 public:
  Framebuffer();
  ~Framebuffer();
  Framebuffer(Framebuffer&&) noexcept;
  Framebuffer& operator=(Framebuffer&&) noexcept;
  Framebuffer(const Framebuffer&) = delete;
  Framebuffer& operator=(const Framebuffer&) = delete;

  bool attachColor(const Texture2D& texture, std::string* error = nullptr);
  bool complete(std::string* error = nullptr) const;
  bool bind(std::string* error = nullptr);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class Fence {
 public:
  Fence();
  ~Fence();
  Fence(Fence&&) noexcept;
  Fence& operator=(Fence&&) noexcept;
  Fence(const Fence&) = delete;
  Fence& operator=(const Fence&) = delete;

  bool insert(std::string* error = nullptr);
  bool wait(std::uint64_t timeout_nanoseconds, std::string* error = nullptr);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace pixcake::gl::qt
