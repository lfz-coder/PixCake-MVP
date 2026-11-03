#pragma once

#include "pixcake/support/result_code.hpp"

namespace pixcake {

enum class WorkspacePage { Login, Home, Library, Refine, Creative };

const char* workspace_page_label(WorkspacePage page);

// 只记录当前展示的页面。不登录，不打开项目库。
class WorkspaceDisplay {
 public:
  static WorkspaceDisplay logged_out();
  static WorkspaceDisplay home();
  static WorkspaceDisplay entered_project();

  WorkspacePage page() const;
  bool in_project() const;
  ResultCode show(WorkspacePage target);
  ResultCode open_project();

 private:
  WorkspacePage page_ = WorkspacePage::Login;
  bool in_project_ = false;
};

}  // namespace pixcake
