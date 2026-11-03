# 工作区展示 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 按《工作区页面介绍》记下登录页、主页、图库、精修、创意各自要展示的区域和条目，并只允许手册写明的页面切换。

**Architecture:** `kernel/workspace` 只保存当前页面和手册写明的展示事实。它不画窗口，不读图片，不登录。展示文案以手册原文为准。2026-11-03 在像素蛋糕-test 9.3.0 上看到、但手册没有的按钮，不放进这份目录。

**Tech Stack:** C++17、CMake、CTest。测试是独立可执行文件，不引入第三方测试框架。

## Global Constraints

- 展示以手册 [工作区页面介绍](https://www.pixcakeai.com/guide/32445)（更新日期 2026-09-17）为准。这一页高于阶段设计里对同一处展示的写法。手册没写的按钮、默认值和滑杆范围，这一份不编。
- 页面只有五个：登录页面、主页面、图库、精修、创意。进入项目后默认是精修。项目内可以切到图库、精修、创意。
- 「返回主页面」只写在精修页。从精修可以回到主页面。从登录页不能切页面。从主页面进入项目才到精修。
- 主页面导航只有：主页、AI工具箱、本地项目、云空间、工作流、选片交付、回收站。主页面全局功能区只有：技术支持、个人中心、消息中心、传输列表、导出列表。精修的全局功能区是这五项再加「导出」。
- 常用功能、快捷功能在手册里只有标题，没有条目。这两处条目为空。不从软件主页补按钮。
- 精修工具栏手册用「分别是」，按这个顺序，不多不少：显示比例调整、抓手、祛边、参考线、液化、污点修复、修补、仿制图章、智能消除、AI点修、重置效果、预设。
- 创意手动工具栏手册在名单后写了「等工具」。只记录点名的工具，区域标记为未列全。不把没点名的工具补进去。
- 创意 AI 工具箱只写「含数十款 AI 工具」，没有工具名。图层面板只写显示当前图片的全部图层。精修功能区没有写下一级模块名。这三处条目为空。
- 图库网格：滑杆向左每行变多，最多 7 张；向右变少，最少 1 张。网格视图是默认视图。每行张数的初始值手册没写，未设置时不填数字。
- 图库三个显示开关和全效果模式，手册没写默认开或关。未设置时不填布尔值。
- 验证码手册写的是 6 位数字。手机号位数手册没写，不规定长度。
- 2026-11-03 用 Jarvis 看 `/Applications/pixcake-test.app` 9.3.0 的精修页：单选是「图库」「精修」，没有名为「创意」的单选。同一窗口画面上，精修右边是「导出至创意」。顶栏还能读到「首页」「会员续费」「点击安装新版本」「更多」。这些都不是手册用词。目录仍用手册的「返回主页面」和「创意」。
- `kernel/workspace` 不包含 Qt 头，不包含 `sqlite3.h`，不包含 `pixcake/ledger/`，不包含 `jpeglib.h`，不调用 `system`，不编码图片。
- 执行时只在本地提交，不推远端，不建 Pull Request。

本计划明确不做：登录请求、项目库、像素、图层拖入、工具效果、蒙版部位、快捷键、Qt 壳。

---

### Task 1: 五个页面和手册写明的切换

**Files:**
- Create: `kernel/workspace/include/pixcake/workspace/workspace_display.hpp`
- Create: `kernel/workspace/src/workspace_display.cpp`
- Create: `kernel/workspace/CMakeLists.txt`
- Create: `tests/kernel/workspace/test_workspace_display.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Modify: `tests/arch/check_dependencies.cmake`

**Interfaces:**
- Consumes: `ResultCode`
- Produces: `WorkspacePage`、`workspace_page_label`、`WorkspaceDisplay`

- [x] **Step 1: Write the failing test**

把 `tests/kernel/workspace/test_workspace_display.cpp` 写成下面的文件。根 `CMakeLists.txt` 在 `kernel/layerdoc` 后加入 `add_subdirectory(kernel/workspace)`。`tests/CMakeLists.txt` 在 `test_layer_document` 后加入 `test_workspace_display`，链接 `pixcake_workspace`、`pixcake_warnings`。`kernel/workspace/CMakeLists.txt` 先只编译一个空的 `src/workspace_display.cpp`，公开 `include`，并公开链接 `pixcake_support`、`pixcake_warnings`。

`tests/arch/check_dependencies.cmake` 在规则 13 后加入：

```cmake
# 14. 工作区展示不画窗口，也不进账本或 SQLite。
scan("${SOURCE_DIR}/kernel/workspace" "rule 14" "sqlite3.h" "pixcake/ledger/" "jpeglib.h" "QOpenGL" "#include <Q")
```

```cpp
#include "pixcake/workspace/workspace_display.hpp"

#include <cstdio>
#include <string>

namespace {

int g_failures = 0;

void expect(bool cond, const char* text, int line) {
  if (!cond) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, text);
    ++g_failures;
  }
}

#define EXPECT(cond) expect(static_cast<bool>(cond), #cond, __LINE__)

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

  if (g_failures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  return 0;
}
```

- [x] **Step 2: Run test to verify it fails**

Run: `cmake -S . -B build && cmake --build build --target test_workspace_display`

Expected: 编译失败，因为还没有 `pixcake/workspace/workspace_display.hpp`。

- [x] **Step 3: Write minimal implementation**

`kernel/workspace/include/pixcake/workspace/workspace_display.hpp`：

```cpp
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
```

`kernel/workspace/src/workspace_display.cpp`：

```cpp
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
```

`CMakeLists.txt` 改为编译这个文件。

- [x] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_workspace_display && ctest --test-dir build -R '^test_workspace_display$' --output-on-failure`

Expected: `test_workspace_display` 通过。

- [x] **Step 5: Commit**

```bash
git add kernel/workspace tests/kernel/workspace CMakeLists.txt tests/CMakeLists.txt tests/arch/check_dependencies.cmake docs/superpowers/plans/2026-11-03-kernel-workspace-display.md
git commit -m "$(cat <<'EOF'
feat: switch the workspace among the pages named in the guide

EOF
)"
```

执行时只在本地提交，不推远端，不建 Pull Request。

---

### Task 2: 各页区域和手册点名的条目

**Files:**
- Modify: `kernel/workspace/include/pixcake/workspace/workspace_display.hpp`
- Modify: `kernel/workspace/src/workspace_display.cpp`
- Modify: `tests/kernel/workspace/test_workspace_display.cpp`

**Interfaces:**
- Consumes: `WorkspaceDisplay`、`WorkspacePage`
- Produces: `DisplayEntry`、`DisplayRegion`、`WorkspaceDisplay::regions`、`verification_code_digit_count`、`personal_center_actions`、`set_library_view`、`set_grid_per_row`、`set_shown`、`set_full_effect`

- [ ] **Step 1: Write the failing test**

在 `test_workspace_display.cpp` 增加 `#include <optional>`、`#include <vector>`。在 `main` 的失败检查前调用 `test_regions()` 和 `test_library_controls()`。这两个函数放在匿名命名空间里。

```cpp
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
  EXPECT(!library.shown("显示RAW格式").has_value());
  EXPECT(!library.full_effect().has_value());
  EXPECT(library.set_grid_per_row(0) == pixcake::ResultCode::StateConflict);
  EXPECT(library.set_grid_per_row(8) == pixcake::ResultCode::StateConflict);
  EXPECT(!library.grid_per_row().has_value());
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
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_workspace_display`

Expected: 编译失败，因为还没有 `regions` 或 `verification_code_digit_count`。

- [ ] **Step 3: Write minimal implementation**

`workspace_display.hpp` 增加 `#include <optional>`、`#include <string>`、`#include <vector>`。在 `WorkspaceDisplay` 前加入两个结构。类里在 `open_project` 后加入新的公有方法。不要另起一个 `private:` 把原来的成员盖掉。私有成员末尾加上图库状态。类外、命名空间结束前加入两个自由函数。

```cpp
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
```

```cpp
  std::vector<DisplayRegion> regions() const;
  std::string library_view() const;
  std::optional<int> grid_per_row() const;
  std::optional<bool> shown(const std::string& label) const;
  std::optional<bool> full_effect() const;
  ResultCode set_library_view(const std::string& view);
  ResultCode set_grid_per_row(int count);
  ResultCode set_shown(const std::string& label, bool on);
  ResultCode set_full_effect(bool on);
```

```cpp
  std::string library_view_ = "网格视图";
  std::optional<int> grid_per_row_;
  std::optional<bool> show_raw_;
  std::optional<bool> show_edit_;
  std::optional<bool> show_export_;
  std::optional<bool> full_effect_;
```

```cpp
int verification_code_digit_count();
std::vector<std::string> personal_center_actions();
```

`workspace_display.cpp` 增加 `#include <utility>`。保留 Task 1 的页面切换。在 `namespace pixcake` 里、`open_project` 之前加入匿名命名空间。把 `regions` 和后面的函数加在 `open_project` 之后。

手册原文里的区域顺序不要改。创意手动工具栏和登录页「帮助」带「等」，`closed` 为 false。个人中心那一句也有「等操作」，所以 `personal_center_actions` 只返回点了名的五项，不再追加。

```cpp
namespace {

DisplayEntry item(std::string id, std::string label) {
  DisplayEntry entry;
  entry.id = std::move(id);
  entry.label = std::move(label);
  return entry;
}

DisplayRegion area(std::string id, std::string label, std::vector<DisplayEntry> entries, bool closed = true) {
  DisplayRegion region;
  region.id = std::move(id);
  region.label = std::move(label);
  region.closed = closed;
  region.entries = std::move(entries);
  return region;
}

bool library_toggle(const std::string& label) {
  return label == "显示RAW格式" || label == "显示修图状态" || label == "显示导出状态";
}

}  // namespace
```

```cpp
int verification_code_digit_count() { return 6; }

std::vector<std::string> personal_center_actions() {
  return {"查看剩余张数", "查看已购买套餐", "当前软件版本", "升级软件", "退出登录"};
}

std::vector<DisplayRegion> WorkspaceDisplay::regions() const {
  if (page_ == WorkspacePage::Login) {
    return {area("login-account", "账号输入框", {}),
            area("login-code", "验证码输入框", {item("get-code", "获取验证码")}),
            area("login-methods", "登录方式切换",
                 {item("sms", "手机验证码登录"), item("wechat", "微信登录"), item("merchant", "商家账号登录")}),
            area("login-help", "帮助", {item("support", "联系售后"), item("shortcuts", "查看快捷键")}, false)};
  }
  if (page_ == WorkspacePage::Home) {
    return {area("home-nav", "导航栏",
                 {item("home", "主页"), item("toolbox", "AI工具箱"), item("local", "本地项目"), item("cloud", "云空间"),
                  item("workflow", "工作流"), item("delivery", "选片交付"), item("trash", "回收站")}),
            area("home-import", "导入照片创建新项目", {}),
            area("home-search", "搜索", {}),
            area("home-global", "全局功能区",
                 {item("support", "技术支持"), item("account", "个人中心"), item("messages", "消息中心"),
                  item("transfers", "传输列表"), item("export-list", "导出列表")}),
            area("home-common", "常用功能", {}),
            area("home-quick", "快捷功能", {}),
            area("home-projects", "项目列表", {})};
  }
  if (page_ == WorkspacePage::Library) {
    return {area("library-status", "显示状态",
                 {item("show-raw", "显示RAW格式"), item("show-edit", "显示修图状态"), item("show-export", "显示导出状态")}),
            area("library-effect", "全效果模式", {}),
            area("library-thumbs", "缩略图区",
                 {item("raw", "RAW图"), item("exported", "已导出"), item("edited", "已编辑"), item("flag", "旗标"),
                  item("star", "星标"), item("color", "色标")}),
            area("library-delete", "批量删除", {}),
            area("library-side", "侧边栏", {item("cull", "AI挑图"), item("preset", "预设"), item("meta", "元数据")}),
            area("library-tools", "导入新图片/筛选/排序",
                 {item("import", "导入新图片"), item("filter", "筛选"), item("sort", "排序")}),
            area("library-views", "视图切换",
                 {item("grid", "网格视图"), item("single", "单视图"), item("filter-view", "筛选视图")})};
  }
  if (page_ == WorkspacePage::Creative) {
    return {area("creative-tools", "手动工具栏",
                 {item("zoom", "显示比例调整"), item("screen", "更改屏幕模式"), item("hand", "抓手"),
                  item("move", "移动图层"), item("scale", "缩放工具"), item("liquify", "液化"),
                  item("spot", "污点修复画笔"), item("patch", "修补工具"), item("clone", "仿制图章"),
                  item("marquee", "矩形选区工具&椭圆选区工具"), item("lasso", "套索工具&选区画笔工具"),
                  item("crop", "裁切")},
                 false),
            area("creative-canvas", "画布", {}),
            area("creative-thumbs", "缩略图区",
                 {item("import", "导入新图片"), item("filter", "筛选"), item("sort", "排序")}),
            area("creative-toolbox", "AI工具箱", {}),
            area("creative-side", "侧边栏",
                 {item("ai", "AI工具"), item("history", "历史记录"), item("batch", "批量设计"), item("help", "帮助"),
                  item("guide", "新手引导"), item("feedback", "反馈")}),
            area("creative-layers", "图层面板", {})};
  }
  return {area("refine-top", "返回主页面、搜索和像素助手",
               {item("back", "返回主页面"), item("search", "搜索"), item("assistant", "像素助手")}),
          area("refine-switch", "图库/精修/创意页面切换",
               {item("library", "图库"), item("refine", "精修"), item("creative", "创意")}),
          area("refine-global", "全局功能区",
               {item("support", "技术支持"), item("account", "个人中心"), item("messages", "消息中心"),
                item("transfers", "传输列表"), item("export-list", "导出列表"), item("export", "导出")}),
          area("refine-toolbar", "工具栏",
               {item("zoom", "显示比例调整"), item("hand", "抓手"), item("fringe", "祛边"), item("guides", "参考线"),
                item("liquify", "液化"), item("spot", "污点修复"), item("patch", "修补"), item("clone", "仿制图章"),
                item("remove", "智能消除"), item("point", "AI点修"), item("reset", "重置效果"), item("preset", "预设")}),
          area("refine-canvas", "画布", {item("compare", "查看对比")}),
          area("refine-thumbs", "缩略图区",
               {item("add", "+"), item("filter", "筛选"), item("sort", "排序"), item("cull", "去挑图"),
                item("star", "星标"), item("flag", "旗标"), item("color", "色标"), item("single", "单视图"),
                item("compare-view", "对比视图"), item("reference", "参考视图")}),
          area("refine-modules", "功能区", {})};
}

std::string WorkspaceDisplay::library_view() const { return library_view_; }
std::optional<int> WorkspaceDisplay::grid_per_row() const { return grid_per_row_; }

std::optional<bool> WorkspaceDisplay::shown(const std::string& label) const {
  if (label == "显示RAW格式") {
    return show_raw_;
  }
  if (label == "显示修图状态") {
    return show_edit_;
  }
  if (label == "显示导出状态") {
    return show_export_;
  }
  return std::nullopt;
}

std::optional<bool> WorkspaceDisplay::full_effect() const { return full_effect_; }

ResultCode WorkspaceDisplay::set_library_view(const std::string& view) {
  if (page_ != WorkspacePage::Library) {
    return ResultCode::StateConflict;
  }
  if (view != "网格视图" && view != "单视图" && view != "筛选视图") {
    return ResultCode::StateConflict;
  }
  library_view_ = view;
  return ResultCode::Ok;
}

ResultCode WorkspaceDisplay::set_grid_per_row(int count) {
  if (page_ != WorkspacePage::Library || library_view_ != "网格视图" || count < 1 || count > 7) {
    return ResultCode::StateConflict;
  }
  grid_per_row_ = count;
  return ResultCode::Ok;
}

ResultCode WorkspaceDisplay::set_shown(const std::string& label, bool on) {
  if (page_ != WorkspacePage::Library || !library_toggle(label)) {
    return ResultCode::StateConflict;
  }
  if (label == "显示RAW格式") {
    show_raw_ = on;
  } else if (label == "显示修图状态") {
    show_edit_ = on;
  } else {
    show_export_ = on;
  }
  return ResultCode::Ok;
}

ResultCode WorkspaceDisplay::set_full_effect(bool on) {
  if (page_ != WorkspacePage::Library) {
    return ResultCode::StateConflict;
  }
  full_effect_ = on;
  return ResultCode::Ok;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_workspace_display && ctest --test-dir build --output-on-failure`

Expected: 全量测试通过。`arch_dependencies` 通过。

- [ ] **Step 5: Commit**

```bash
git add kernel/workspace tests/kernel/workspace docs/superpowers/plans/2026-11-03-kernel-workspace-display.md
git commit -m "$(cat <<'EOF'
feat: list the workspace regions named in the guide

EOF
)"
```

执行时只在本地提交，不推远端，不建 Pull Request。
