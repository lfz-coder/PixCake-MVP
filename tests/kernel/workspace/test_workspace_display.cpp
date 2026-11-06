#include "pixcake/workspace/workspace_display.hpp"

#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void expect(bool cond, const char* text, int line) {
  if (!cond) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, text);
    ++g_failures;
  }
}

#define EXPECT(cond) expect(static_cast<bool>(cond), #cond, __LINE__)

const pixcake::DisplayRegion* find_region(const std::vector<pixcake::DisplayRegion>& regions, const std::string& label) {
  for (const auto& region : regions) {
    if (region.label == label) {
      return &region;
    }
  }
  return nullptr;
}

void expect_region(const std::vector<pixcake::DisplayRegion>& regions, const std::string& label, bool closed,
                   const std::vector<std::string>& entries) {
  const pixcake::DisplayRegion* region = find_region(regions, label);
  EXPECT(region != nullptr);
  if (region == nullptr) {
    return;
  }
  EXPECT(region->closed == closed);
  EXPECT(region->entries.size() == entries.size());
  for (std::size_t index = 0; index < entries.size() && index < region->entries.size(); ++index) {
    EXPECT(region->entries[index].label == entries[index]);
  }
}

void expect_order(const std::vector<pixcake::DisplayRegion>& regions, const std::vector<std::string>& labels) {
  EXPECT(regions.size() == labels.size());
  for (std::size_t index = 0; index < labels.size() && index < regions.size(); ++index) {
    EXPECT(regions[index].label == labels[index]);
  }
}

void test_regions() {
  EXPECT(pixcake::verification_code_digit_count() == 6);
  const std::vector<std::string> center = pixcake::personal_center_actions();
  EXPECT(center.size() == 5);
  EXPECT(center[0] == "查看剩余张数");
  EXPECT(center[1] == "查看已购买套餐");
  EXPECT(center[2] == "当前软件版本");
  EXPECT(center[3] == "升级软件");
  EXPECT(center[4] == "退出登录");

  const auto login = pixcake::WorkspaceDisplay::logged_out().regions();
  expect_order(login, {"账号输入框", "验证码输入框", "登录方式切换", "帮助"});
  expect_region(login, "账号输入框", true, {});
  expect_region(login, "验证码输入框", true, {"获取验证码"});
  expect_region(login, "登录方式切换", true, {"手机验证码登录", "微信登录", "商家账号登录"});
  expect_region(login, "帮助", false, {"联系售后", "查看快捷键"});

  const auto home = pixcake::WorkspaceDisplay::home().regions();
  expect_order(home, {"导航栏", "导入照片创建新项目", "搜索", "全局功能区", "常用功能", "快捷功能", "项目列表"});
  expect_region(home, "导航栏", true,
                {"主页", "AI工具箱", "本地项目", "云空间", "工作流", "选片交付", "回收站"});
  expect_region(home, "导入照片创建新项目", true, {});
  expect_region(home, "全局功能区", true, {"技术支持", "个人中心", "消息中心", "传输列表", "导出列表"});
  expect_region(home, "常用功能", true, {});
  expect_region(home, "快捷功能", true, {});

  auto project = pixcake::WorkspaceDisplay::entered_project();
  const auto refine = project.regions();
  expect_order(refine, {"返回主页面、搜索和像素助手", "图库/精修/创意页面切换", "全局功能区", "工具栏", "画布", "缩略图区",
                        "功能区"});
  expect_region(refine, "返回主页面、搜索和像素助手", true, {"返回主页面", "搜索", "像素助手"});
  expect_region(refine, "图库/精修/创意页面切换", true, {"图库", "精修", "创意"});
  expect_region(refine, "全局功能区", true,
                {"技术支持", "个人中心", "消息中心", "传输列表", "导出列表", "导出"});
  expect_region(refine, "工具栏", true,
                {"显示比例调整", "抓手", "祛边", "参考线", "液化", "污点修复", "修补", "仿制图章", "智能消除", "AI点修",
                 "重置效果", "预设"});
  expect_region(refine, "画布", true, {"查看对比"});
  expect_region(refine, "缩略图区", true,
                {"+", "筛选", "排序", "去挑图", "星标", "旗标", "色标", "单视图", "对比视图", "参考视图"});
  expect_region(refine, "功能区", true, {});

  EXPECT(project.show(pixcake::WorkspacePage::Library) == pixcake::ResultCode::Ok);
  const auto library = project.regions();
  expect_order(library, {"显示状态", "全效果模式", "缩略图区", "批量删除", "侧边栏", "导入新图片/筛选/排序", "视图切换"});
  expect_region(library, "显示状态", true, {"显示RAW格式", "显示修图状态", "显示导出状态"});
  expect_region(library, "缩略图区", true, {"RAW图", "已导出", "已编辑", "旗标", "星标", "色标"});
  expect_region(library, "侧边栏", true, {"AI挑图", "预设", "元数据"});
  expect_region(library, "导入新图片/筛选/排序", true, {"导入新图片", "筛选", "排序"});
  expect_region(library, "视图切换", true, {"网格视图", "单视图", "筛选视图"});

  EXPECT(project.show(pixcake::WorkspacePage::Creative) == pixcake::ResultCode::Ok);
  const auto creative = project.regions();
  expect_order(creative, {"手动工具栏", "画布", "缩略图区", "AI工具箱", "侧边栏", "图层面板"});
  expect_region(creative, "手动工具栏", false,
                {"显示比例调整", "更改屏幕模式", "抓手", "移动图层", "缩放工具", "液化", "污点修复画笔", "修补工具",
                 "仿制图章", "矩形选区工具&椭圆选区工具", "套索工具&选区画笔工具", "裁切"});
  expect_region(creative, "缩略图区", true, {"导入新图片", "筛选", "排序"});
  expect_region(creative, "AI工具箱", true, {});
  expect_region(creative, "侧边栏", true, {"AI工具", "历史记录", "批量设计", "帮助", "新手引导", "反馈"});
  expect_region(creative, "图层面板", true, {});
}

void test_library_controls() {
  auto home = pixcake::WorkspaceDisplay::home();
  EXPECT(home.set_library_view("网格视图") == pixcake::ResultCode::StateConflict);
  EXPECT(home.set_grid_per_row(4) == pixcake::ResultCode::StateConflict);
  EXPECT(home.set_shown("显示RAW格式", true) == pixcake::ResultCode::StateConflict);
  EXPECT(home.set_full_effect(true) == pixcake::ResultCode::StateConflict);

  auto library = pixcake::WorkspaceDisplay::entered_project();
  EXPECT(library.show(pixcake::WorkspacePage::Library) == pixcake::ResultCode::Ok);
  EXPECT(library.library_view() == "网格视图");
  EXPECT(!library.grid_per_row().has_value());
  EXPECT(library.shown("显示RAW格式") == true);
  EXPECT(library.shown("显示修图状态") == true);
  EXPECT(library.shown("显示导出状态") == true);
  EXPECT(!library.full_effect().has_value());
  EXPECT(library.set_grid_per_row(0) == pixcake::ResultCode::Ok);
  EXPECT(library.grid_per_row() == 1);
  EXPECT(library.set_grid_per_row(8) == pixcake::ResultCode::Ok);
  EXPECT(library.grid_per_row() == 7);
  EXPECT(library.set_grid_per_row(7) == pixcake::ResultCode::Ok);
  EXPECT(library.set_grid_per_row(1) == pixcake::ResultCode::Ok);
  EXPECT(library.grid_per_row() == 1);
  EXPECT(library.set_library_view("筛选视图") == pixcake::ResultCode::Ok);
  EXPECT(library.set_grid_per_row(3) == pixcake::ResultCode::StateConflict);
  EXPECT(library.grid_per_row() == 1);
  EXPECT(library.set_library_view("没有这种视图") == pixcake::ResultCode::StateConflict);
  EXPECT(library.library_view() == "筛选视图");
  EXPECT(library.set_library_view("网格视图") == pixcake::ResultCode::Ok);
  EXPECT(library.set_shown("显示导出状态", false) == pixcake::ResultCode::Ok);
  EXPECT(library.shown("显示导出状态") == false);
  EXPECT(library.set_shown("星标", true) == pixcake::ResultCode::StateConflict);
  EXPECT(!library.shown("星标").has_value());
  EXPECT(library.set_full_effect(false) == pixcake::ResultCode::Ok);
  EXPECT(library.full_effect() == false);
  EXPECT(library.show(pixcake::WorkspacePage::Refine) == pixcake::ResultCode::Ok);
  EXPECT(library.set_full_effect(true) == pixcake::ResultCode::StateConflict);
  EXPECT(library.full_effect() == false);
}

}  // namespace

int main() {
  EXPECT(std::string(pixcake::workspace_page_label(pixcake::WorkspacePage::Login)) == "登录页面");
  EXPECT(std::string(pixcake::workspace_page_label(pixcake::WorkspacePage::Home)) == "主页面");
  EXPECT(std::string(pixcake::workspace_page_label(pixcake::WorkspacePage::Library)) == "图库");
  EXPECT(std::string(pixcake::workspace_page_label(pixcake::WorkspacePage::Refine)) == "精修");
  EXPECT(std::string(pixcake::workspace_page_label(pixcake::WorkspacePage::Creative)) == "创意");

  auto login = pixcake::WorkspaceDisplay::logged_out();
  EXPECT(login.page() == pixcake::WorkspacePage::Login);
  EXPECT(!login.in_project());
  EXPECT(login.show(pixcake::WorkspacePage::Home) == pixcake::ResultCode::StateConflict);
  EXPECT(login.page() == pixcake::WorkspacePage::Login);
  EXPECT(login.open_project() == pixcake::ResultCode::StateConflict);

  auto home = pixcake::WorkspaceDisplay::home();
  EXPECT(home.page() == pixcake::WorkspacePage::Home);
  EXPECT(home.show(pixcake::WorkspacePage::Refine) == pixcake::ResultCode::StateConflict);
  EXPECT(home.show(pixcake::WorkspacePage::Home) == pixcake::ResultCode::StateConflict);
  EXPECT(home.open_project() == pixcake::ResultCode::Ok);
  EXPECT(home.in_project());
  EXPECT(home.page() == pixcake::WorkspacePage::Refine);
  EXPECT(home.open_project() == pixcake::ResultCode::StateConflict);

  EXPECT(home.show(pixcake::WorkspacePage::Library) == pixcake::ResultCode::Ok);
  EXPECT(home.page() == pixcake::WorkspacePage::Library);
  EXPECT(home.show(pixcake::WorkspacePage::Home) == pixcake::ResultCode::StateConflict);
  EXPECT(home.page() == pixcake::WorkspacePage::Library);
  EXPECT(home.show(pixcake::WorkspacePage::Creative) == pixcake::ResultCode::Ok);
  EXPECT(home.show(pixcake::WorkspacePage::Refine) == pixcake::ResultCode::Ok);
  EXPECT(home.show(pixcake::WorkspacePage::Home) == pixcake::ResultCode::Ok);
  EXPECT(!home.in_project());
  EXPECT(home.page() == pixcake::WorkspacePage::Home);
  EXPECT(home.show(pixcake::WorkspacePage::Login) == pixcake::ResultCode::StateConflict);

  auto project = pixcake::WorkspaceDisplay::entered_project();
  EXPECT(project.page() == pixcake::WorkspacePage::Refine);
  EXPECT(project.in_project());

  test_regions();
  test_library_controls();

  if (g_failures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  return 0;
}
