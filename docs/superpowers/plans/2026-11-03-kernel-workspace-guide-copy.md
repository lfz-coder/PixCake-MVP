# 工作区手册补句 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把《工作区页面介绍》里已经点名、但工作区目录还没记下的展示句子补进 `kernel/workspace`。

**Architecture:** 新文件只保存手册原文里的短语和数量。它不改五个页面的切换，不往已有区域里加按钮，不画窗口，不登录，不读图片。阶段设计对同一处写了手册这一页没有的控件时，以这一页为准，不写进去。

**Tech Stack:** C++17、CMake、CTest。测试是独立可执行文件，不引入第三方测试框架。

## Global Constraints

- 展示以手册 [工作区页面介绍](https://www.pixcakeai.com/guide/32445)（更新日期 2026-09-17）为准。这一页高于阶段设计第 5、6 节对同一处展示的写法。
- 手册写「具体详见」并链到别的页面时，不打开那一页，不摘工具名。AI工具箱、工作流、选片交付只保留导航里已经记下的名字。
- 阶段设计有、这一页没有的，不写：联机拍摄、「+ 照片导入」、会员续费、创意点数明细、直方图显示开关、导出列表上的极速导出开关、筛选条件表、排序字段表、色标的红黄绿蓝紫、手机号位数。
- 三个显示开关和全效果模式，这一页没写默认开或关。不填默认布尔值。阶段设计第 6.4 节写的「默认都开」不采用。
- 「每张图片含 AI 精选标签、旗标、星标、颜色标及 X 关闭按钮，支持动态补位」写在「视图切换」整段，位于网格、单视图、筛选视图三条之前。三份视图都带这些，不按阶段设计第 6.5 节收成只有筛选视图。手册没有写按下 X 就会补位，这两件事分开记，不写成因果关系。
- 云标识只出现在主页那三种项目里的后两种，位置是左上角。本地项目那一句没有云标识，不补。
- 已有区域的条目数量和文字保持 `2026-11-03-kernel-workspace-display` 的结果。这一份只增加旁边的短语，不把新短语塞进 `regions()`。
- `kernel/workspace` 不包含 Qt 头，不包含 `sqlite3.h`，不包含 `pixcake/ledger/`，不包含 `jpeglib.h`，不调用 `system`，不编码图片。规则 14 已经扫整个目录，不新增规则。
- 执行时只在本地提交，不推远端，不建 Pull Request。

本计划明确不做：登录请求、短信、窗口、像素、图层拖入的真正导入、筛选规则、排序字段、快捷键、Qt 壳。

---

### Task 1: 登录、主页和全局功能区的手册短语

**Files:**
- Create: `kernel/workspace/include/pixcake/workspace/guide_copy.hpp`
- Create: `kernel/workspace/src/guide_copy.cpp`
- Create: `tests/kernel/workspace/test_guide_copy.cpp`
- Modify: `kernel/workspace/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `WorkspaceDisplay`、`DisplayRegion`（只用来确认旧目录没被加长）
- Produces: `CloudMarkedProject`、`account_input_kind`、`home_search_looks_for`、`home_project_kinds`、`local_project_kinds`、`cloud_space_kinds`、`deleted_item_keep_days`、`project_list_shows`、`support_action`、`message_center_shows`、`transfer_list_shows`、`export_list_shows`

- [x] **Step 1: Write the failing test**

`kernel/workspace/CMakeLists.txt` 在现有源文件旁加上 `src/guide_copy.cpp`。`tests/CMakeLists.txt` 在 `test_workspace_display` 的 `add_test` 之后加入 `test_guide_copy`，链接 `pixcake_workspace`、`pixcake_warnings`。

把 `tests/kernel/workspace/test_guide_copy.cpp` 写成下面的文件。

```cpp
#include "pixcake/workspace/guide_copy.hpp"
#include "pixcake/workspace/workspace_display.hpp"

#include <cstdio>
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

const pixcake::DisplayRegion* find_region(const std::vector<pixcake::DisplayRegion>& regions,
                                          const std::string& label) {
  for (const auto& region : regions) {
    if (region.label == label) {
      return &region;
    }
  }
  return nullptr;
}

}  // namespace

int main() {
  EXPECT(std::string(pixcake::account_input_kind()) == "手机号");
  EXPECT(std::string(pixcake::home_search_looks_for()) == "项目名称或关键词");
  EXPECT(std::string(pixcake::project_list_shows()) == "全部项目");
  EXPECT(pixcake::deleted_item_keep_days() == 30);

  const std::vector<pixcake::CloudMarkedProject> home = pixcake::home_project_kinds();
  EXPECT(home.size() == 3);
  EXPECT(home[0].label == "本台设备创建的项目");
  EXPECT(!home[0].cloud_badge);
  EXPECT(home[1].label == "已上传至云空间的项目");
  EXPECT(home[1].cloud_badge);
  EXPECT(home[2].label == "从云空间下载到本台设备的项目");
  EXPECT(home[2].cloud_badge);

  const std::vector<std::string> local = pixcake::local_project_kinds();
  EXPECT(local.size() == 2);
  EXPECT(local[0] == "本台设备创建的项目");
  EXPECT(local[1] == "从云空间下载到本台设备的项目");

  const std::vector<std::string> cloud = pixcake::cloud_space_kinds();
  EXPECT(cloud.size() == 2);
  EXPECT(cloud[0] == "个人空间");
  EXPECT(cloud[1] == "管理员创建的团队空间");

  EXPECT(std::string(pixcake::support_action()) == "在线联系像素蛋糕官方售后技术获取支持");
  EXPECT(std::string(pixcake::message_center_shows()) == "查看软件内消息");
  EXPECT(std::string(pixcake::transfer_list_shows()) == "查看上传及下载任务");
  EXPECT(std::string(pixcake::export_list_shows()) == "查看导出任务");

  const auto regions = pixcake::WorkspaceDisplay::home().regions();
  const pixcake::DisplayRegion* nav = find_region(regions, "导航栏");
  const pixcake::DisplayRegion* common = find_region(regions, "常用功能");
  EXPECT(nav != nullptr && nav->entries.size() == 7);
  EXPECT(common != nullptr && common->entries.empty());

  if (g_failures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  return 0;
}
```

- [x] **Step 2: Run test to verify it fails**

Run: `cmake -S . -B build && cmake --build build --target test_guide_copy`

Expected: 编译失败，因为还没有 `pixcake/workspace/guide_copy.hpp`。

- [x] **Step 3: Write minimal implementation**

`kernel/workspace/include/pixcake/workspace/guide_copy.hpp`：

```cpp
#pragma once

#include <string>
#include <vector>

namespace pixcake {

struct CloudMarkedProject {
  std::string label;
  bool cloud_badge = false;
};

const char* account_input_kind();
const char* home_search_looks_for();
std::vector<CloudMarkedProject> home_project_kinds();
std::vector<std::string> local_project_kinds();
std::vector<std::string> cloud_space_kinds();
int deleted_item_keep_days();
const char* project_list_shows();
const char* support_action();
const char* message_center_shows();
const char* transfer_list_shows();
const char* export_list_shows();

}  // namespace pixcake
```

`kernel/workspace/src/guide_copy.cpp`：

```cpp
#include "pixcake/workspace/guide_copy.hpp"

namespace pixcake {

const char* account_input_kind() { return "手机号"; }
const char* home_search_looks_for() { return "项目名称或关键词"; }
const char* project_list_shows() { return "全部项目"; }
int deleted_item_keep_days() { return 30; }

std::vector<CloudMarkedProject> home_project_kinds() {
  return {CloudMarkedProject{"本台设备创建的项目", false},
          CloudMarkedProject{"已上传至云空间的项目", true},
          CloudMarkedProject{"从云空间下载到本台设备的项目", true}};
}

std::vector<std::string> local_project_kinds() {
  return {"本台设备创建的项目", "从云空间下载到本台设备的项目"};
}

std::vector<std::string> cloud_space_kinds() {
  return {"个人空间", "管理员创建的团队空间"};
}

const char* support_action() { return "在线联系像素蛋糕官方售后技术获取支持"; }
const char* message_center_shows() { return "查看软件内消息"; }
const char* transfer_list_shows() { return "查看上传及下载任务"; }
const char* export_list_shows() { return "查看导出任务"; }

}  // namespace pixcake
```

- [x] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_guide_copy && ctest --test-dir build -R '^test_guide_copy$' --output-on-failure`

Expected: `test_guide_copy` 通过。

- [x] **Step 5: Commit**

```bash
git add kernel/workspace/include/pixcake/workspace/guide_copy.hpp kernel/workspace/src/guide_copy.cpp kernel/workspace/CMakeLists.txt tests/kernel/workspace/test_guide_copy.cpp tests/CMakeLists.txt docs/superpowers/plans/2026-11-03-kernel-workspace-guide-copy.md
git commit -m "$(cat <<'EOF'
feat: record the home-page sentences named in the guide

EOF
)"
```

执行时只在本地提交，不推远端，不建 Pull Request。不要把未提交的图层文档修改加进这次提交。

---

### Task 2: 图库、精修和创意的手册短语

**Files:**
- Modify: `kernel/workspace/include/pixcake/workspace/guide_copy.hpp`
- Modify: `kernel/workspace/src/guide_copy.cpp`
- Modify: `tests/kernel/workspace/test_guide_copy.cpp`

**Interfaces:**
- Consumes: Task 1 的 `guide_copy.hpp`
- Produces: `PlacedMark`、`refine_export_target`、`refine_canvas_shows`、`compare_button_corner`、`full_effect_on_image`、`full_effect_off_image`、`thumbnail_mark_limit`、`thumbnail_marks`、`color_label_paints`、`view_card_labels`、`view_card_close_label`、`view_card_reflow`、`single_view_form`、`filter_view_layout`、`filter_view_selection`、`creative_canvas_shows`、`creative_canvas_drop_imports_layer`

- [x] **Step 1: Write the failing test**

在 `test_guide_copy.cpp` 的 `main` 里、失败计数之前，加入下面的断言。

```cpp
  EXPECT(std::string(pixcake::refine_export_target()) == "当前选中的图片");
  EXPECT(std::string(pixcake::refine_canvas_shows()) == "精修后的图片");
  EXPECT(std::string(pixcake::compare_button_corner()) == "右下角");
  EXPECT(std::string(pixcake::full_effect_on_image()) == "效果图");
  EXPECT(std::string(pixcake::full_effect_off_image()) == "原图");
  EXPECT(pixcake::thumbnail_mark_limit() == 6);

  const std::vector<pixcake::PlacedMark> marks = pixcake::thumbnail_marks();
  EXPECT(marks.size() == 6);
  EXPECT(marks[0].place == "上方" && marks[0].label == "RAW图");
  EXPECT(marks[1].place == "上方" && marks[1].label == "已导出");
  EXPECT(marks[2].place == "上方" && marks[2].label == "已编辑");
  EXPECT(marks[3].place == "下方" && marks[3].label == "旗标");
  EXPECT(marks[4].place == "下方" && marks[4].label == "星标");
  EXPECT(marks[5].place == "下方" && marks[5].label == "色标");
  EXPECT(std::string(pixcake::color_label_paints()) == "缩略图框背景色");

  const std::vector<std::string> cards = pixcake::view_card_labels();
  EXPECT(cards.size() == 4);
  EXPECT(cards[0] == "AI精选标签");
  EXPECT(cards[1] == "旗标");
  EXPECT(cards[2] == "星标");
  EXPECT(cards[3] == "颜色标");
  EXPECT(std::string(pixcake::view_card_close_label()) == "X");
  EXPECT(std::string(pixcake::view_card_reflow()) == "动态补位");
  EXPECT(std::string(pixcake::single_view_form()) == "大图");
  EXPECT(std::string(pixcake::filter_view_layout()) == "同屏对比多张图片");
  EXPECT(std::string(pixcake::filter_view_selection()) == "与缩略图库实时同步选中状态");

  EXPECT(std::string(pixcake::creative_canvas_shows()) == "当前选中的图片及对应图层的效果");
  EXPECT(pixcake::creative_canvas_drop_imports_layer());
```

- [x] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_guide_copy`

Expected: 编译失败，因为还没有 `refine_export_target` 或 `PlacedMark`。

- [x] **Step 3: Write minimal implementation**

`guide_copy.hpp` 在 `CloudMarkedProject` 后加入：

```cpp
struct PlacedMark {
  std::string place;
  std::string label;
};
```

在 `export_list_shows` 声明后、命名空间结束前加入：

```cpp
const char* refine_export_target();
const char* refine_canvas_shows();
const char* compare_button_corner();
const char* full_effect_on_image();
const char* full_effect_off_image();
int thumbnail_mark_limit();
std::vector<PlacedMark> thumbnail_marks();
const char* color_label_paints();
std::vector<std::string> view_card_labels();
const char* view_card_close_label();
const char* view_card_reflow();
const char* single_view_form();
const char* filter_view_layout();
const char* filter_view_selection();
const char* creative_canvas_shows();
bool creative_canvas_drop_imports_layer();
```

`guide_copy.cpp` 在命名空间结束前加入：

```cpp
const char* refine_export_target() { return "当前选中的图片"; }
const char* refine_canvas_shows() { return "精修后的图片"; }
const char* compare_button_corner() { return "右下角"; }
const char* full_effect_on_image() { return "效果图"; }
const char* full_effect_off_image() { return "原图"; }
int thumbnail_mark_limit() { return 6; }

std::vector<PlacedMark> thumbnail_marks() {
  return {PlacedMark{"上方", "RAW图"}, PlacedMark{"上方", "已导出"}, PlacedMark{"上方", "已编辑"},
          PlacedMark{"下方", "旗标"},   PlacedMark{"下方", "星标"},   PlacedMark{"下方", "色标"}};
}

const char* color_label_paints() { return "缩略图框背景色"; }

std::vector<std::string> view_card_labels() {
  return {"AI精选标签", "旗标", "星标", "颜色标"};
}

const char* view_card_close_label() { return "X"; }
const char* view_card_reflow() { return "动态补位"; }
const char* single_view_form() { return "大图"; }
const char* filter_view_layout() { return "同屏对比多张图片"; }
const char* filter_view_selection() { return "与缩略图库实时同步选中状态"; }
const char* creative_canvas_shows() { return "当前选中的图片及对应图层的效果"; }
bool creative_canvas_drop_imports_layer() { return true; }
```

- [x] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_guide_copy && ctest --test-dir build --output-on-failure`

Expected: 全量测试通过。`arch_dependencies` 通过。

- [x] **Step 5: Commit**

```bash
git add kernel/workspace/include/pixcake/workspace/guide_copy.hpp kernel/workspace/src/guide_copy.cpp tests/kernel/workspace/test_guide_copy.cpp docs/superpowers/plans/2026-11-03-kernel-workspace-guide-copy.md
git commit -m "$(cat <<'EOF'
feat: record the library and canvas sentences named in the guide

EOF
)"
```

执行时只在本地提交，不推远端，不建 Pull Request。不要把未提交的图层文档修改加进这次提交。
