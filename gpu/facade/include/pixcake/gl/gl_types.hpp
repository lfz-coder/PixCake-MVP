#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace pixcake::gl {

enum class ShaderStage : std::uint8_t {
  Vertex,
  Fragment,
  Compute,
};

enum class BufferUsage : std::uint8_t {
  StaticDraw,
  DynamicDraw,
  StreamDraw,
};

enum class AttributeType : std::uint8_t {
  Float32,
  UInt8,
};

enum class PixelFormat : std::uint8_t {
  RGBA8,
  BGRA8,
};

using Vec2 = std::array<float, 2>;
using Vec3 = std::array<float, 3>;
using Vec4 = std::array<float, 4>;
using Mat4 = std::array<float, 16>;

// All byte counts and row strides are explicit so a backend never has to
// inspect an image file or infer a platform-specific image layout.
struct PixelUpload {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::size_t row_stride = 0;
  PixelFormat format = PixelFormat::RGBA8;
  const void* data = nullptr;
};

inline constexpr std::string_view kBackendQt = "qt";
inline constexpr std::string_view kBackendNative = "native";

}  // namespace pixcake::gl
