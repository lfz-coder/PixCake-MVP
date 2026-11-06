#include "shell_controller.hpp"

namespace pixcake::shell {

void ShellController::request_page(Page target) {
  if (display_.navigate(target) == ResultCode::Ok && changed_) changed_();
}

}  // namespace pixcake::shell
