#include "pixcake/workspace/workspace_display.hpp"

namespace pixcake {

const char* workspace_page_label(WorkspacePage page) {
  switch (page) {
    case WorkspacePage::Login:
      return "登录页面";
    case WorkspacePage::Home:
      return "主页面";
    case WorkspacePage::Library:
      return "图库";
    case WorkspacePage::Refine:
      return "精修";
    case WorkspacePage::Creative:
      return "创意";
  }
  return "";
}

WorkspaceDisplay WorkspaceDisplay::logged_out() { return WorkspaceDisplay{}; }

WorkspaceDisplay WorkspaceDisplay::home() {
  WorkspaceDisplay display;
  display.page_ = WorkspacePage::Home;
  return display;
}

WorkspaceDisplay WorkspaceDisplay::entered_project() {
  WorkspaceDisplay display;
  display.page_ = WorkspacePage::Refine;
  display.in_project_ = true;
  return display;
}

WorkspacePage WorkspaceDisplay::page() const { return page_; }
bool WorkspaceDisplay::in_project() const { return in_project_; }

ResultCode WorkspaceDisplay::show(WorkspacePage target) {
  if (page_ == WorkspacePage::Login || target == WorkspacePage::Login) {
    return ResultCode::StateConflict;
  }
  if (target == WorkspacePage::Home) {
    if (page_ != WorkspacePage::Refine) {
      return ResultCode::StateConflict;
    }
    in_project_ = false;
    page_ = WorkspacePage::Home;
    return ResultCode::Ok;
  }
  if (!in_project_ || (target != WorkspacePage::Library && target != WorkspacePage::Refine &&
                       target != WorkspacePage::Creative)) {
    return ResultCode::StateConflict;
  }
  page_ = target;
  return ResultCode::Ok;
}

ResultCode WorkspaceDisplay::open_project() {
  if (page_ != WorkspacePage::Home || in_project_) {
    return ResultCode::StateConflict;
  }
  in_project_ = true;
  page_ = WorkspacePage::Refine;
  return ResultCode::Ok;
}

}  // namespace pixcake
