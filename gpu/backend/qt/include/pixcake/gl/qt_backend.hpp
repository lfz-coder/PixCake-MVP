#pragma once
#include <pixcake/gl/gl.hpp>
namespace pixcake::gl::qt {
Context create_context();
Shader create_shader();
Program create_program();
Texture2D create_texture2d();
Framebuffer create_framebuffer();
}  // namespace pixcake::gl::qt
