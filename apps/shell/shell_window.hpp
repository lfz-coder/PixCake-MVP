#pragma once

#include "shell_controller.hpp"

#include <QLabel>
#include <QMainWindow>
#include <QWidget>

namespace pixcake::shell {

// 窗口框架：按显示模型选页面构建函数，把页面放进窗口，并在页面上方显示当前页的提示（运维清单 O-13）。
class ShellWindow final : public QMainWindow {
 public:
  explicit ShellWindow(ShellDisplay display);

 private:
  void render();
  void setBody(QWidget* page);

  ShellController controller_;
  QWidget* body_ = nullptr;
  QLabel* title_ = nullptr;
};

}  // namespace pixcake::shell
