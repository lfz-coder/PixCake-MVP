#include "pixcake/ui/shell/shell_display.hpp"

#include <cstdio>
#include <string>

namespace {
int failures = 0;

void expect(bool condition, const char* expression, int line) {
  if (!condition) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, expression);
    ++failures;
  }
}

#define EXPECT(value) expect(static_cast<bool>(value), #value, __LINE__)

const pixcake::ui::shell::Region* find_region(const std::vector<pixcake::ui::shell::Region>& regions,
                                               const std::string& label) {
  for (const auto& region : regions) {
    if (region.label == label) {
      return &region;
    }
  }
  return nullptr;
}

const pixcake::ui::shell::Region* find_region_by_id(const std::vector<pixcake::ui::shell::Region>& regions,
                                                    const std::string& id) {
  for (const auto& region : regions) {
    if (region.id == id) {
      return &region;
    }
  }
  return nullptr;
}

void test_contract() {
  using namespace pixcake::ui::shell;
  auto login = ShellDisplay::logged_out();
  EXPECT(login.page() == Page::Login);
  EXPECT(!login.in_project());
  EXPECT(login.contract().single_instance);
  EXPECT(login.contract().title == "像素蛋糕");
  EXPECT(login.navigate(Page::Home) == pixcake::ResultCode::StateConflict);
  EXPECT(find_region(login.regions(), "登录方式切换") != nullptr);
  const auto* login_help = find_region_by_id(login.regions(), "login-help");
  EXPECT(login_help != nullptr);
  if (login_help != nullptr) EXPECT(login_help->placement == RegionPlacement::Bottom);

  auto home = ShellDisplay::home();
  const auto* home_nav = find_region_by_id(home.regions(), "home-nav");
  const auto* home_global = find_region_by_id(home.regions(), "home-global");
  const auto* home_projects = find_region_by_id(home.regions(), "home-projects");
  EXPECT(home_nav != nullptr);
  EXPECT(home_global != nullptr);
  EXPECT(home_projects != nullptr);
  if (home_nav != nullptr) EXPECT(home_nav->placement == RegionPlacement::Left);
  if (home_global != nullptr) EXPECT(home_global->placement == RegionPlacement::Top);
  if (home_projects != nullptr) EXPECT(home_projects->placement == RegionPlacement::Bottom);

  auto shell = ShellDisplay::home();
  EXPECT(shell.open_project() == pixcake::ResultCode::Ok);
  EXPECT(shell.page() == Page::Refine);
  EXPECT(shell.in_project());
  EXPECT(find_region(shell.regions(), "工具栏") != nullptr);
  EXPECT(find_region(shell.regions(), "缩略图区") != nullptr);
  EXPECT(find_region(shell.regions(), "全局功能区") != nullptr);

  EXPECT(shell.navigate(Page::Library) == pixcake::ResultCode::Ok);
  EXPECT(find_region(shell.regions(), "侧边栏") != nullptr);
  EXPECT(find_region(shell.regions(), "视图切换") != nullptr);
  EXPECT(shell.navigate(Page::Creative) == pixcake::ResultCode::Ok);
  EXPECT(find_region(shell.regions(), "图层面板") != nullptr);
  EXPECT(find_region(shell.regions(), "手动工具栏") != nullptr);
  EXPECT(shell.navigate(Page::Home) == pixcake::ResultCode::Ok);
  EXPECT(!shell.in_project());
  EXPECT(shell.navigate(Page::Creative) == pixcake::ResultCode::StateConflict);
}
}  // namespace

int main() {
  EXPECT(std::string(pixcake::ui::shell::page_label(pixcake::ui::shell::Page::Login)) == "登录页面");
  EXPECT(std::string(pixcake::ui::shell::page_label(pixcake::ui::shell::Page::Home)) == "主页面");
  EXPECT(std::string(pixcake::ui::shell::page_label(pixcake::ui::shell::Page::Library)) == "图库");
  EXPECT(std::string(pixcake::ui::shell::page_label(pixcake::ui::shell::Page::Refine)) == "精修");
  EXPECT(std::string(pixcake::ui::shell::page_label(pixcake::ui::shell::Page::Creative)) == "创意");
  test_contract();
  return failures == 0 ? 0 : 1;
}
