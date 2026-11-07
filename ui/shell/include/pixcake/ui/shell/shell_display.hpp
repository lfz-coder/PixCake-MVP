#pragma once

#include "pixcake/support/result_code.hpp"
#include "pixcake/support/session_state.hpp"
#include "pixcake/support/user_notice.hpp"

#include <optional>
#include <string>
#include <vector>

namespace pixcake::ui::shell {

// These are page identities only. Rendering and business actions stay outside
// this contract; the shell only describes what the window should expose.
enum class Page { Login, Home, Library, Refine, Creative };
enum class RegionPlacement { Top, Left, Center, Right, Bottom };

const char* page_label(Page page);

struct RegionEntry {
  std::string id;
  std::string label;
};

struct Region {
  std::string id;
  std::string label;
  bool collapsible = false;
  RegionPlacement placement = RegionPlacement::Center;
  std::vector<RegionEntry> entries;
};

struct WindowContract {
  std::string title;  // 来自页面文案表
  bool single_instance = true;
  Page page = Page::Login;
  bool in_project = false;
  std::vector<Region> regions;
};

class ShellDisplay {
 public:
  static ShellDisplay logged_out();
  // 按会话决定首屏（需求第 79、103 行）：未登录是登录页，本机已有会话直接进主页。
  static ShellDisplay for_session(const SessionState& session);
  // 只给开发构建的 --page 预览用（PIXCAKE_DEV_PAGES，门禁规则 18）；发布构建由会话决定页面（S-25）。
  static ShellDisplay dev_preview(Page page);

  Page page() const;
  bool in_project() const;
  WindowContract contract() const;
  const std::vector<Region>& regions() const;

  // Navigation is the only state-changing operation in this batch.
  // 从主页进入项目。landing 只能是图库或精修：再次打开回到该项目上次所在的工作区，
  // 没有记录和导入新建时进精修（需求 5.3，像素蛋糕-test 实测 2026-11-06）。
  ResultCode open_project(Page landing = Page::Refine);
  ResultCode navigate(Page target);
  // 登录成功后换成主页；会话没有登录时拒绝。退出登录回到登录页，不保留项目（需求第 226 行）。
  ResultCode signed_in(const SessionState& session);
  ResultCode signed_out();
  // 用户能看见的失败（运维清单第 3 节）：提示留在当前页面上，换页或登录成功后清空。空提示不记。
  void post_notice(const UserNotice& notice);
  const std::vector<std::string>& notices() const;

  // 图库的显示控制（需求 §6）：只在图库页可改。视图是 grid、single、filter-view；网格每行 1 到 7 张，
  // 越界夹到两端，只有网格视图可改；show-raw、show-edit、show-export 三种显示状态默认都开。
  const std::string& library_view() const;
  std::optional<int> grid_per_row() const;
  std::optional<bool> shown(const std::string& id) const;
  std::optional<bool> full_effect() const;
  ResultCode set_library_view(const std::string& id);
  ResultCode set_grid_per_row(int count);
  ResultCode set_shown(const std::string& id, bool on);
  ResultCode set_full_effect(bool on);

 private:
  Page page_ = Page::Login;
  bool in_project_ = false;
  std::vector<Region> regions_;
  std::vector<std::string> notices_;
  std::string library_view_ = "grid";
  std::optional<int> grid_per_row_;
  bool show_raw_ = true;
  bool show_edit_ = true;
  bool show_export_ = true;
  std::optional<bool> full_effect_;

  void rebuild_regions();
};

// 需求 §3.2：文件拖到应用图标（Mac 程序坞或访达里的 .app）时的去向。
//   - 模态层开着：不导入，窗口保持原样（边界 B-71）。
//   - 未登录（登录页）：业务命令不可用，不导入。
//   - 在项目里（精修，以及同属项目的图库、创意）：追加进当前项目。
//   - 主页：按第 5.2 节新建项目。
enum class IconDropAction { NewProject, AppendToProject, Ignore };
IconDropAction icon_drop_action(const ShellDisplay& display, bool modal_open);

}  // namespace pixcake::ui::shell
