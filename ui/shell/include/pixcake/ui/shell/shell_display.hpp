#pragma once

#include "pixcake/support/result_code.hpp"
#include "pixcake/support/session_state.hpp"

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
  std::string title = "像素蛋糕";
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
  ResultCode open_project();
  ResultCode navigate(Page target);
  // 登录成功后换成主页；会话没有登录时拒绝。退出登录回到登录页，不保留项目（需求第 226 行）。
  ResultCode signed_in(const SessionState& session);
  ResultCode signed_out();

 private:
  Page page_ = Page::Login;
  bool in_project_ = false;
  std::vector<Region> regions_;

  void rebuild_regions();
};

}  // namespace pixcake::ui::shell
