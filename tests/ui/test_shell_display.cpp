#include "pixcake/ui/shell/page_copy.hpp"
#include "pixcake/ui/shell/shell_display.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

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

// O-13：登录失败的说明留在登录页上；登录成功后提示清空。
void test_o13_notice_stays_on_the_page() {
  using namespace pixcake::ui::shell;
  SignedIn session;
  session.on = false;
  auto display = ShellDisplay::for_session(session);
  pixcake::UserNotice notice;
  notice.text = "验证码已过期，请重新获取";
  display.post_notice(notice);
  EXPECT(display.page() == Page::Login);
  EXPECT(display.notices() == std::vector<std::string>({"验证码已过期，请重新获取"}));
  display.post_notice(pixcake::UserNotice{});
  EXPECT(display.notices().size() == 1);
  session.on = true;
  EXPECT(display.signed_in(session) == pixcake::ResultCode::Ok);
  EXPECT(display.notices().empty());
}

void expect_region(const std::vector<pixcake::ui::shell::Region>& regions, const std::string& id,
                   const std::string& label, const std::vector<std::string>& entries) {
  const auto* region = find_region_by_id(regions, id);
  EXPECT(region != nullptr);
  if (region == nullptr) return;
  if (region->label != label) std::fprintf(stderr, "  region %s label %s\n", id.c_str(), region->label.c_str());
  EXPECT(region->label == label);
  EXPECT(region->entries.size() == entries.size());
  for (std::size_t index = 0; index < entries.size() && index < region->entries.size(); ++index) {
    EXPECT(region->entries[index].label == entries[index]);
  }
}

void expect_ids(const std::vector<pixcake::ui::shell::Region>& regions, const std::vector<std::string>& ids) {
  EXPECT(regions.size() == ids.size());
  for (std::size_t index = 0; index < ids.size() && index < regions.size(); ++index) {
    EXPECT(regions[index].id == ids[index]);
  }
}

// Q-11：显示模型的区域名与条目来自页面文案表；原 kernel/workspace 的第二份显示模型已删除，
// 条目内容在这里逐条核对（官方手册的叫法）。
void test_q11_regions_take_copy_from_the_page_table() {
  using namespace pixcake::ui::shell;
  EXPECT(std::string(page_label(Page::Login)) == "登录页面");
  EXPECT(std::string(page_label(Page::Home)) == "主页面");
  EXPECT(std::string(page_label(Page::Library)) == "图库");
  EXPECT(std::string(page_label(Page::Refine)) == "精修");
  EXPECT(std::string(page_label(Page::Creative)) == "创意");
  EXPECT(ShellDisplay::logged_out().contract().title == "像素蛋糕");

  const auto login = ShellDisplay::logged_out().regions();
  expect_ids(login, {"login-account", "login-code", "login-methods", "login-help"});
  expect_region(login, "login-account", "账号输入框", {});
  expect_region(login, "login-code", "验证码输入框", {"获取验证码"});
  expect_region(login, "login-methods", "登录方式切换", {"手机验证码登录", "微信登录", "商家账号登录"});
  expect_region(login, "login-help", "帮助", {"联系售后", "查看快捷键"});

  SignedIn session;
  const auto home = ShellDisplay::for_session(session).regions();
  expect_ids(home, {"home-nav", "home-import", "home-search", "home-global", "home-common", "home-quick",
                    "home-projects"});
  expect_region(home, "home-nav", "导航栏", {"主页", "AI工具箱", "本地项目", "云空间", "工作流", "选片交付", "回收站"});
  expect_region(home, "home-import", "导入照片创建新项目", {});
  expect_region(home, "home-search", "搜索", {});
  expect_region(home, "home-global", "全局功能区", {"技术支持", "个人中心", "消息中心", "传输列表", "导出列表"});
  expect_region(home, "home-common", "常用功能", {});
  expect_region(home, "home-quick", "快捷功能", {});
  expect_region(home, "home-projects", "项目列表", {});

  auto project = ShellDisplay::for_session(session);
  EXPECT(project.open_project() == pixcake::ResultCode::Ok);
  const auto refine = project.regions();
  expect_ids(refine, {"refine-top", "refine-switch", "refine-global", "refine-toolbar", "refine-canvas", "refine-thumbs",
                      "refine-modules"});
  expect_region(refine, "refine-top", "返回主页面、搜索和像素助手", {"返回主页面", "搜索", "像素助手"});
  expect_region(refine, "refine-switch", "图库/精修/创意页面切换", {"图库", "精修", "创意"});
  expect_region(refine, "refine-global", "全局功能区",
                {"技术支持", "个人中心", "消息中心", "传输列表", "导出列表", "导出"});
  expect_region(refine, "refine-toolbar", "工具栏",
                {"显示比例调整", "抓手", "祛边", "参考线", "液化", "污点修复", "修补", "仿制图章", "智能消除", "AI点修",
                 "重置效果", "预设"});
  expect_region(refine, "refine-canvas", "画布", {"查看对比"});
  expect_region(refine, "refine-thumbs", "缩略图区",
                {"+", "筛选", "排序", "去挑图", "星标", "旗标", "色标", "单视图", "对比视图", "参考视图"});
  expect_region(refine, "refine-modules", "功能区", {});

  EXPECT(project.navigate(Page::Library) == pixcake::ResultCode::Ok);
  const auto library = project.regions();
  expect_ids(library, {"library-nav", "library-status", "library-effect", "library-thumbs", "library-delete",
                       "library-side", "library-tools", "library-views"});
  expect_region(library, "library-nav", "项目内导航", {"首页", "搜索", "图库", "精修", "创意"});
  expect_region(library, "library-status", "显示状态", {"显示RAW格式", "显示修图状态", "显示导出状态"});
  expect_region(library, "library-effect", "全效果模式", {});
  expect_region(library, "library-thumbs", "缩略图区", {"RAW图", "已导出", "已编辑", "旗标", "星标", "色标"});
  expect_region(library, "library-delete", "批量删除", {});
  expect_region(library, "library-side", "侧边栏", {"AI挑图", "预设", "元数据"});
  expect_region(library, "library-tools", "导入新图片/筛选/排序", {"导入新图片", "筛选", "排序"});
  expect_region(library, "library-views", "视图切换", {"网格视图", "单视图", "筛选视图"});

  EXPECT(project.navigate(Page::Creative) == pixcake::ResultCode::Ok);
  const auto creative = project.regions();
  expect_ids(creative, {"creative-tools", "creative-canvas", "creative-thumbs", "creative-toolbox", "creative-side",
                        "creative-layers"});
  expect_region(creative, "creative-tools", "手动工具栏",
                {"显示比例调整", "更改屏幕模式", "抓手", "移动图层", "缩放工具", "液化", "污点修复画笔", "修补工具",
                 "仿制图章", "矩形选区工具&椭圆选区工具", "套索工具&选区画笔工具", "裁切"});
  const auto* tools = find_region_by_id(creative, "creative-tools");
  EXPECT(tools != nullptr && tools->collapsible);
  expect_region(creative, "creative-canvas", "画布", {});
  expect_region(creative, "creative-thumbs", "缩略图区", {"导入新图片", "筛选", "排序"});
  expect_region(creative, "creative-toolbox", "AI工具箱", {});
  expect_region(creative, "creative-side", "侧边栏", {"AI工具", "历史记录", "批量设计", "帮助", "新手引导", "反馈"});
  expect_region(creative, "creative-layers", "图层面板", {});

  // 个人中心的五个动作（需求第 226 行）。
  std::vector<std::string> actions;
  for (const auto& entry : page_list("account-actions")) actions.push_back(entry.text);
  EXPECT(actions == std::vector<std::string>({"查看剩余张数", "查看已购买套餐", "当前软件版本", "升级软件", "退出登录"}));
  EXPECT(missing_page_copy().empty());
}

// 图库的显示控制（从原 kernel/workspace 显示模型移来）：只在图库页可改；网格每行 1 到 7 张；
// 只有网格视图能改每行张数；三种显示状态默认都开。
void test_b31_library_controls() {
  using namespace pixcake::ui::shell;
  SignedIn session;
  auto home = ShellDisplay::for_session(session);
  EXPECT(home.set_library_view("grid") == pixcake::ResultCode::StateConflict);
  EXPECT(home.set_grid_per_row(4) == pixcake::ResultCode::StateConflict);
  EXPECT(home.set_shown("show-raw", true) == pixcake::ResultCode::StateConflict);
  EXPECT(home.set_full_effect(true) == pixcake::ResultCode::StateConflict);

  auto library = ShellDisplay::for_session(session);
  EXPECT(library.open_project() == pixcake::ResultCode::Ok);
  EXPECT(library.navigate(Page::Library) == pixcake::ResultCode::Ok);
  EXPECT(library.library_view() == "grid");
  EXPECT(!library.grid_per_row().has_value());
  EXPECT(library.shown("show-raw") == true);
  EXPECT(library.shown("show-edit") == true);
  EXPECT(library.shown("show-export") == true);
  EXPECT(!library.full_effect().has_value());
  EXPECT(library.set_grid_per_row(0) == pixcake::ResultCode::Ok);
  EXPECT(library.grid_per_row() == 1);
  EXPECT(library.set_grid_per_row(8) == pixcake::ResultCode::Ok);
  EXPECT(library.grid_per_row() == 7);
  EXPECT(library.set_grid_per_row(1) == pixcake::ResultCode::Ok);
  EXPECT(library.set_library_view("filter-view") == pixcake::ResultCode::Ok);
  EXPECT(library.set_grid_per_row(3) == pixcake::ResultCode::StateConflict);
  EXPECT(library.grid_per_row() == 1);
  EXPECT(library.set_library_view("no-such-view") == pixcake::ResultCode::StateConflict);
  EXPECT(library.library_view() == "filter-view");
  EXPECT(library.set_library_view("grid") == pixcake::ResultCode::Ok);
  EXPECT(library.set_shown("show-export", false) == pixcake::ResultCode::Ok);
  EXPECT(library.shown("show-export") == false);
  EXPECT(library.set_shown("star", true) == pixcake::ResultCode::StateConflict);
  EXPECT(!library.shown("star").has_value());
  EXPECT(library.set_full_effect(false) == pixcake::ResultCode::Ok);
  EXPECT(library.full_effect() == false);
  EXPECT(library.navigate(Page::Refine) == pixcake::ResultCode::Ok);
  EXPECT(library.set_full_effect(true) == pixcake::ResultCode::StateConflict);
  EXPECT(library.full_effect() == false);
}

// 打开项目落在上次所在的工作区（像素蛋糕-test 实测 2026-11-06）：只能是图库或精修。
void test_project_click_lands_in_the_library() {
  using pixcake::ResultCode;
  using pixcake::ui::shell::Page;
  using pixcake::ui::shell::ShellDisplay;
  auto home = ShellDisplay::dev_preview(Page::Home);
  EXPECT(home.open_project(Page::Creative) == ResultCode::StateConflict);
  EXPECT(home.open_project(Page::Home) == ResultCode::StateConflict);
  EXPECT(home.page() == Page::Home && !home.in_project());
  EXPECT(home.open_project(Page::Library) == ResultCode::Ok);
  EXPECT(home.page() == Page::Library && home.in_project());
  EXPECT(home.open_project(Page::Library) == ResultCode::StateConflict);  // 已在项目里
  EXPECT(home.navigate(Page::Refine) == ResultCode::Ok);
}

// 需求 §3.2 与边界 B-71：拖到应用图标的三种情况，加上未登录。
void test_b71_icon_drop_follows_the_page() {
  using pixcake::ResultCode;
  using pixcake::ui::shell::IconDropAction;
  using pixcake::ui::shell::icon_drop_action;
  using pixcake::ui::shell::Page;
  using pixcake::ui::shell::ShellDisplay;
  const auto login = ShellDisplay::logged_out();
  EXPECT(icon_drop_action(login, false) == IconDropAction::Ignore);
  auto home = ShellDisplay::dev_preview(Page::Home);
  EXPECT(icon_drop_action(home, false) == IconDropAction::NewProject);
  EXPECT(icon_drop_action(home, true) == IconDropAction::Ignore);
  EXPECT(home.open_project() == ResultCode::Ok);  // 进项目后是精修页
  EXPECT(home.page() == Page::Refine);
  EXPECT(icon_drop_action(home, false) == IconDropAction::AppendToProject);
  EXPECT(icon_drop_action(home, true) == IconDropAction::Ignore);
  for (const Page page : {Page::Library, Page::Creative}) {
    EXPECT(home.navigate(page) == ResultCode::Ok);
    EXPECT(icon_drop_action(home, false) == IconDropAction::AppendToProject);
  }
  EXPECT(home.navigate(Page::Home) == ResultCode::Ok);
  EXPECT(icon_drop_action(home, false) == IconDropAction::NewProject);
}

// 快捷键表（需求 §23.1）：编号不重复；同一作用范围里不能有两个功能用同一个键（Cmd+N 按页面分流，不算冲突）；
// 键写法 Qt 认得（修饰键在前）；标记键 0–5、6–9、P/X/U 都在。
bool scopes_overlap(const std::string& a, const std::string& b) {
  if (a == b || a == "global" || b == "global") return true;
  const auto in_project = [](const std::string& scope) { return scope == "library" || scope == "refine"; };
  return (a == "project" && (in_project(b) || b == "project")) || (b == "project" && in_project(a));
}

void test_shortcut_table_has_no_conflicts() {
  const auto& table = pixcake::ui::shell::shortcut_table();
  EXPECT(table.size() > 30);
  for (std::size_t i = 0; i < table.size(); ++i) {
    EXPECT(!table[i].id.empty() && !table[i].key.empty() && !table[i].name.empty() && !table[i].scope.empty());
    EXPECT(pixcake::ui::shell::shortcut_key(table[i].id) == table[i].key);
    for (std::size_t j = i + 1; j < table.size(); ++j) {
      EXPECT(table[i].id != table[j].id);
      if (table[i].key == table[j].key && scopes_overlap(table[i].scope, table[j].scope)) {
        std::fprintf(stderr, "shortcut conflict: %s and %s share %s\n", table[i].id.c_str(), table[j].id.c_str(),
                     table[i].key.c_str());
        EXPECT(false);
      }
    }
  }
  for (const char* id : {"rating-0", "rating-5", "color-red", "color-blue", "flag-pick", "flag-exclude", "flag-clear",
                         "undo", "redo", "copy-effect", "paste-effect", "sync-effect", "virtual-copy"}) {
    EXPECT(!pixcake::ui::shell::shortcut_key(id).empty());
  }
  EXPECT(pixcake::ui::shell::shortcut_key("color-red") == "6" && pixcake::ui::shell::shortcut_key("sync-effect") == "Ctrl+Shift+S");
  EXPECT(pixcake::ui::shell::shortcut_key("no-such-shortcut").empty());
  const auto missing = pixcake::ui::shell::missing_page_copy();
  EXPECT(std::find(missing.begin(), missing.end(), "shortcut:no-such-shortcut") != missing.end());
}

int main() {
  test_project_click_lands_in_the_library();
  test_b71_icon_drop_follows_the_page();
  test_q11_regions_take_copy_from_the_page_table();
  test_b31_library_controls();
  test_o13_notice_stays_on_the_page();
  test_s25_session_decides_the_page();
  EXPECT(std::string(pixcake::ui::shell::page_label(pixcake::ui::shell::Page::Login)) == "登录页面");
  EXPECT(std::string(pixcake::ui::shell::page_label(pixcake::ui::shell::Page::Home)) == "主页面");
  EXPECT(std::string(pixcake::ui::shell::page_label(pixcake::ui::shell::Page::Library)) == "图库");
  EXPECT(std::string(pixcake::ui::shell::page_label(pixcake::ui::shell::Page::Refine)) == "精修");
  EXPECT(std::string(pixcake::ui::shell::page_label(pixcake::ui::shell::Page::Creative)) == "创意");
  test_contract();
  test_shortcut_table_has_no_conflicts();  // last: it looks up a missing id on purpose
  return failures == 0 ? 0 : 1;
}
