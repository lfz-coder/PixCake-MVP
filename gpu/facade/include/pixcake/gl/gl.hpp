#pragma once

#include "pixcake/gl/gl_types.hpp"

#if defined(PIXCAKE_GL_BACKEND_NATIVE)
#include "pixcake/gl/native_backend.hpp"
namespace pixcake::gl {
using Context = native::Context;
using Shader = native::Shader;
using Program = native::Program;
using Buffer = native::Buffer;
using VertexArray = native::VertexArray;
using Texture2D = native::Texture2D;
using Framebuffer = native::Framebuffer;
using Fence = native::Fence;
}  // namespace pixcake::gl
#else
#include "pixcake/gl/qt_backend.hpp"
namespace pixcake::gl {
using Context = qt::Context;
using Shader = qt::Shader;
using Program = qt::Program;
using Buffer = qt::Buffer;
using VertexArray = qt::VertexArray;
using Texture2D = qt::Texture2D;
using Framebuffer = qt::Framebuffer;
using Fence = qt::Fence;
}  // namespace pixcake::gl
#endif
