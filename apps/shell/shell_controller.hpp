#pragma once

#include "page_kit.hpp"

#include <functional>
#include <utility>

namespace pixcake::shell {

// 壳层里唯一改显示模型的地方（代码质量清单 Q-12）。页面只发请求；以后的文档命令也从这里
// 调命令入口，不从页面调。
class ShellController final : public PageActions {
 public:
  explicit ShellController(ShellDisplay display) : display_(std::move(display)) {}

  const ShellDisplay& display() const { return display_; }
  void on_changed(std::function<void()> callback) { changed_ = std::move(callback); }

  void request_page(Page target) override;

 private:
  ShellDisplay display_;
  std::function<void()> changed_;
};

}  // namespace pixcake::shell
