#pragma once

#include "pixcake/support/result_code.hpp"

#include <optional>
#include <string>
#include <vector>

namespace pixcake {

enum class WorkspacePage { Login, Home, Library, Refine, Creative };

const char* workspace_page_label(WorkspacePage page);

struct DisplayEntry {
  std::string id;
  std::string label;
};

struct DisplayRegion {
  std::string id;
  std::string label;
  bool closed = true;
  std::vector<DisplayEntry> entries;
};

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

  std::vector<DisplayRegion> regions() const;
  std::string library_view() const;
  std::optional<int> grid_per_row() const;
  std::optional<bool> shown(const std::string& label) const;
  std::optional<bool> full_effect() const;
  ResultCode set_library_view(const std::string& view);
  ResultCode set_grid_per_row(int count);
  ResultCode set_shown(const std::string& label, bool on);
  ResultCode set_full_effect(bool on);

 private:
  WorkspacePage page_ = WorkspacePage::Login;
  bool in_project_ = false;
  std::string library_view_ = "网格视图";
  std::optional<int> grid_per_row_;
  std::optional<bool> show_raw_;
  std::optional<bool> show_edit_;
  std::optional<bool> show_export_;
  std::optional<bool> full_effect_;
};

int verification_code_digit_count();
std::vector<std::string> personal_center_actions();

}  // namespace pixcake
