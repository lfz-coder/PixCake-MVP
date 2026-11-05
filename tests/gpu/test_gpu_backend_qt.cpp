#include <pixcake/gl/qt_backend.hpp>

#include <QGuiApplication>

#include <cassert>

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  auto context = pixcake::gl::qt::create_context();
  assert(context.valid());
  // CI may not expose an offscreen OpenGL implementation. In that case the
  // backend reports an actionable error; construction and facade wiring still
  // remain testable without pretending pixels were rendered.
  if (context.makeCurrent()) context.doneCurrent();
  return 0;
}
