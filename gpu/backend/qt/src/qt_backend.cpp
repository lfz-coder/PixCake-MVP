#include "pixcake/gl/qt_backend.hpp"
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <memory>

namespace pixcake::gl::qt {
namespace {
class ContextImpl final : public detail::ContextImpl {
 public:
  ContextImpl() {
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    surface_.setFormat(format);
    surface_.create();
    context_.setFormat(format);
    if (!context_.create()) error_ = "failed to create OpenGL context";
  }
  bool make_current(std::string& error) override {
    if (!error_.empty()) { error = error_; return false; }
    if (!context_.makeCurrent(&surface_)) { error_ = "failed to make OpenGL context current"; error = error_; return false; }
    return true;
  }
  void done_current() override { context_.doneCurrent(); }
  std::string last_error() const override { return error_; }
 private:
  QOpenGLContext context_;
  QOffscreenSurface surface_;
  std::string error_;
};
}  // namespace
Context create_context() { return Context(std::make_shared<ContextImpl>()); }
}  // namespace pixcake::gl::qt
