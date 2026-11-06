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

struct SignedIn final : pixcake::SessionState {
  bool on = true;
  bool signed_in() const override { return on; }
};

// S-25、需求第 79、103、204 行：未登录只停在登录页；本机已有会话直接进主页；退出登录回登录页。
void test_s25_session_decides_the_page() {
  using namespace pixcake::ui::shell;
  SignedIn session;
  session.on = false;
  auto display = ShellDisplay::for_session(session);
  EXPECT(display.page() == Page::Login);
  EXPECT(display.navigate(Page::Home) == pixcake::ResultCode::StateConflict);
  EXPECT(display.open_project() == pixcake::ResultCode::StateConflict);
  EXPECT(display.signed_in(session) == pixcake::ResultCode::StateConflict);
  session.on = true;
  EXPECT(display.signed_in(session) == pixcake::ResultCode::Ok);
  EXPECT(display.page() == Page::Home);
  EXPECT(display.open_project() == pixcake::ResultCode::Ok);
  EXPECT(display.page() == Page::Refine);
  EXPECT(display.signed_out() == pixcake::ResultCode::Ok);
  EXPECT(display.page() == Page::Login && !display.in_project());
  EXPECT(ShellDisplay::for_session(session).page() == Page::Home);
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

  SignedIn signed_in;
  auto home = ShellDisplay::for_session(signed_in);
  const auto* home_nav = find_region_by_id(home.regions(), "home-nav");
  const auto* home_global = find_region_by_id(home.regions(), "home-global");
  const auto* home_projects = find_region_by_id(home.regions(), "home-projects");
  EXPECT(home_nav != nullptr);
  EXPECT(home_global != nullptr);
  EXPECT(home_projects != nullptr);
  if (home_nav != nullptr) EXPECT(home_nav->placement == RegionPlacement::Left);
  if (home_global != nullptr) EXPECT(home_global->placement == RegionPlacement::Top);
  if (home_projects != nullptr) EXPECT(home_projects->placement == RegionPlacement::Bottom);

  auto shell = ShellDisplay::for_session(signed_in);
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
  test_s25_session_decides_the_page();
  EXPECT(std::string(pixcake::ui::shell::page_label(pixcake::ui::shell::Page::Login)) == "登录页面");
  EXPECT(std::string(pixcake::ui::shell::page_label(pixcake::ui::shell::Page::Home)) == "主页面");
  EXPECT(std::string(pixcake::ui::shell::page_label(pixcake::ui::shell::Page::Library)) == "图库");
  EXPECT(std::string(pixcake::ui::shell::page_label(pixcake::ui::shell::Page::Refine)) == "精修");
  EXPECT(std::string(pixcake::ui::shell::page_label(pixcake::ui::shell::Page::Creative)) == "创意");
  test_contract();
  return failures == 0 ? 0 : 1;
}
