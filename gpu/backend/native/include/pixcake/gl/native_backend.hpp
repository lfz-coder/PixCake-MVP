#pragma once
#include <pixcake/gl/gl.hpp>

// Native Core OpenGL backend. It implements the same detail::*Impl contract as
// the Qt backend, so callers pick a backend at runtime by choosing which
// factory creates the Context; nothing above the facade includes this header's
// platform dependencies (CGL + Glad stay inside native_backend.cpp).
namespace pixcake::gl::native {
// True when this build has a native context implementation for the platform.
bool available();
Context create_context();
Shader create_shader();
Program create_program();
Buffer create_buffer();
VertexArray create_vertex_array();
Texture2D create_texture2d();
Framebuffer create_framebuffer();
Fence create_fence();
}  // namespace pixcake::gl::native
