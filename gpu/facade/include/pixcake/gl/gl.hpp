#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace pixcake::gl {

enum class ShaderStage { Vertex, Fragment };
enum class PixelFormat { Rgba8 };

struct ImageView {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t row_stride = 0;
  PixelFormat format = PixelFormat::Rgba8;
  const std::byte* data = nullptr;
  std::size_t bytes = 0;
};

namespace detail {
class ShaderImpl;
class ProgramImpl;
class Texture2DImpl;
class FramebufferImpl;
class ContextImpl { public: virtual ~ContextImpl() = default; virtual bool make_current(std::string&) = 0; virtual void done_current() = 0; virtual bool draw_triangles(std::uint32_t, std::string&) = 0; virtual bool read_pixels(std::uint32_t, std::uint32_t, std::vector<std::byte>&, std::string&) = 0; virtual std::shared_ptr<ShaderImpl> create_shader() = 0; virtual std::shared_ptr<ProgramImpl> create_program() = 0; virtual std::shared_ptr<Texture2DImpl> create_texture() = 0; virtual std::shared_ptr<FramebufferImpl> create_framebuffer() = 0; virtual std::string last_error() const = 0; };
class ShaderImpl { public: virtual ~ShaderImpl() = default; virtual bool compile(ShaderStage, std::string_view, std::string&) = 0; };
class ProgramImpl { public: virtual ~ProgramImpl() = default; virtual bool link(const ShaderImpl&, const ShaderImpl&, std::string&) = 0; virtual bool use(std::string&) = 0; virtual bool set_float(std::string_view, float, std::string&) = 0; virtual bool set_int(std::string_view, std::int32_t, std::string&) = 0; virtual bool set_vec4(std::string_view, const std::array<float, 4>&, std::string&) = 0; virtual bool set_mat4(std::string_view, const std::array<float, 16>&, std::string&) = 0; virtual bool set_texture_unit(std::string_view, std::uint32_t, std::string&) = 0; };
class BufferImpl { public: virtual ~BufferImpl() = default; virtual bool allocate(std::size_t, const void*, std::string&) = 0; virtual bool write(std::size_t, std::size_t, const void*, std::string&) = 0; };
class VertexArrayImpl { public: virtual ~VertexArrayImpl() = default; virtual bool bind(std::string&) = 0; virtual bool enable_attribute(std::uint32_t, std::int32_t, std::uint32_t, std::size_t, std::string&) = 0; };
class Texture2DImpl { public: virtual ~Texture2DImpl() = default; virtual bool allocate(std::uint32_t, std::uint32_t, PixelFormat, std::string&) = 0; virtual bool upload(const ImageView&, std::string&) = 0; virtual bool bind(std::uint32_t, std::string&) = 0; };
class FramebufferImpl { public: virtual ~FramebufferImpl() = default; virtual bool attach_color(const Texture2DImpl&, std::string&) = 0; virtual bool complete(std::string&) = 0; virtual bool bind(std::string&) = 0; };
class FenceImpl { public: virtual ~FenceImpl() = default; virtual bool insert(std::string&) = 0; virtual bool wait(std::uint64_t, std::string&) = 0; };
}  // namespace detail

class Shader;
class Program;
class Texture2D;
class Framebuffer;
class Context { public: Context(); explicit Context(std::shared_ptr<detail::ContextImpl>); static Context create(); bool makeCurrent(); void doneCurrent(); bool drawTriangles(std::uint32_t count, std::string& error); bool readPixels(std::uint32_t width, std::uint32_t height, std::vector<std::byte>& data, std::string& error); Shader createShader() const; Program createProgram() const; Texture2D createTexture2D() const; Framebuffer createFramebuffer() const; std::string lastError() const; bool valid() const; private: std::shared_ptr<detail::ContextImpl> impl_; };
class Shader { public: Shader(); explicit Shader(std::shared_ptr<detail::ShaderImpl>); static Shader create(); bool compile(ShaderStage, std::string_view, std::string&); bool valid() const; const detail::ShaderImpl* implementation() const; private: std::shared_ptr<detail::ShaderImpl> impl_; };
class Program { public: Program(); explicit Program(std::shared_ptr<detail::ProgramImpl>); static Program create(); bool link(const Shader&, const Shader&, std::string&); bool use(std::string&); bool setFloat(std::string_view, float, std::string&); bool setInt(std::string_view, std::int32_t, std::string&); bool setVec4(std::string_view, const std::array<float, 4>&, std::string&); bool setMat4(std::string_view, const std::array<float, 16>&, std::string&); bool setTextureUnit(std::string_view, std::uint32_t, std::string&); bool valid() const; private: std::shared_ptr<detail::ProgramImpl> impl_; };
class Buffer { public: Buffer(); explicit Buffer(std::shared_ptr<detail::BufferImpl>); static Buffer create(); bool allocate(std::size_t, const void*, std::string&); bool write(std::size_t, std::size_t, const void*, std::string&); bool valid() const; private: std::shared_ptr<detail::BufferImpl> impl_; };
class VertexArray { public: VertexArray(); explicit VertexArray(std::shared_ptr<detail::VertexArrayImpl>); static VertexArray create(); bool bind(std::string&); bool enableAttribute(std::uint32_t, std::int32_t, std::uint32_t, std::size_t, std::string&); bool valid() const; private: std::shared_ptr<detail::VertexArrayImpl> impl_; };
class Texture2D { public: Texture2D(); explicit Texture2D(std::shared_ptr<detail::Texture2DImpl>); static Texture2D create(); bool allocate(std::uint32_t, std::uint32_t, PixelFormat, std::string&); bool upload(const ImageView&, std::string&); bool bind(std::uint32_t, std::string&); bool valid() const; const detail::Texture2DImpl* implementation() const; private: std::shared_ptr<detail::Texture2DImpl> impl_; };
class Framebuffer { public: Framebuffer(); explicit Framebuffer(std::shared_ptr<detail::FramebufferImpl>); static Framebuffer create(); bool attachColor(const Texture2D&, std::string&); bool complete(std::string&); bool bind(std::string&); bool valid() const; private: std::shared_ptr<detail::FramebufferImpl> impl_; };
class Fence { public: Fence(); explicit Fence(std::shared_ptr<detail::FenceImpl>); static Fence create(); bool insert(std::string&); bool wait(std::uint64_t, std::string&); bool valid() const; private: std::shared_ptr<detail::FenceImpl> impl_; };

}  // namespace pixcake::gl
