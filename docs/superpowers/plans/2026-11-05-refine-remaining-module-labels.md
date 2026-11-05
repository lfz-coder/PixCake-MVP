# 精修其余模块只读名称 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把页面图里已经有名称的人像、图像、宠物控件放进精修侧栏，不放数值。

**Architecture:** 名称从 Jarvis 页面图 `com.xiangtian.pixcakepc.test.yaml` 的 `refine_portrait_slider`、`refine_image_slider`、`refine_pet_slider` 原样生成到 `apps/shell/refine_verified_labels.hpp`。`render_workspace()` 只增加三个禁用标签组。不手改任何一个名称。

**Tech Stack:** C++17、Qt 6 Widgets、现有 `pixcake_app_shell` smoke、CTest。

## Global Constraints

- 分支保持 `codex/desktop-official-layout`。
- 可见名和无障碍名都使用页面图 `name` 字段，不缩短、不翻译。
- 「表情管理-笑容管理」在页面图里出现 3 次，3 次都保留。
- 不调用 `setValue`、`setRange`、`setChecked`，不写范围、步进、默认值。
- 局部、后调色在页面图里没有子项清单。不给它们编名称。
- 宠物页签这次没有出现在辅助功能树里。分组无障碍名用已经在侧栏上的「宠物」，不另写「宠物美化」。
- 人像分组无障碍名用现场读到的「人像美化」。图像分组用现场读到的「图像美化」。
- 不改颜色、字号、间距，不执行官方页面图级视觉复刻里的像素取样。
- 登录、导入、渲染、AI、导出不在本计划。

## 本阶段不做

官方页面图级视觉复刻和官方工作区视觉对齐仍待执行。那两份计划要求颜色、间距来自官方页面图或资源，并且写明 Jarvis 不能用来推断颜色和间距。这次没有新的测量值，所以不做。调色滑杆记录里的 -100 到 100 仍不写入界面。

---

### Task 1: 按页面图生成三份名称

**Files:**
- Create: `apps/shell/refine_verified_labels.hpp`
- Create: `docs/superpowers/evidence/2026-11-05-refine-remaining-module-names.md`

**Interfaces:**
- Produces: `pixcake::shell::verifiedPortraitLabels()`、`verifiedImageLabels()`、`verifiedPetLabels()`，返回 `QStringList`。

- [x] **Step 1: 从页面图原样写出名称**

只读取 `refine_portrait_slider`、`refine_image_slider`、`refine_pet_slider` 的 `name`。数量必须是 387、64、18。不增删。

- [x] **Step 2: 核对数量**

三个函数返回的列表长度分别是 387、64、18。证据文件记录局部和后调色没有子项清单。

### Task 2: 把三个名称组放进精修侧栏

**Files:**
- Modify: `apps/shell/main.cpp`

**Interfaces:**
- Consumes: Task 1 的三个函数。
- Produces: `refine-portrait-labels`、`refine-image-labels`、`refine-pet-labels`。

- [x] **Step 1: 在衣物名称组之后加入三个组**

在 `modules_layout->addWidget(clothing_labels);` 之后、`addStretch()` 之前调用下面的局部函数三次。顺序是人像、图像、宠物。

```cpp
const auto add_verified = [&](const QString& object_name, const QString& accessible, const QStringList& labels) {
  auto* frame = new QFrame(modules);
  frame->setObjectName(object_name);
  frame->setAccessibleName(accessible);
  auto* layout = new QVBoxLayout(frame);
  layout->setContentsMargins(0, 0, 0, 0);
  for (const auto& label : labels) {
    auto* name = text(label);
    name->setAccessibleName(label);
    name->setEnabled(false);
    layout->addWidget(name);
  }
  modules_layout->addWidget(frame);
};
add_verified(QStringLiteral("refine-portrait-labels"), QStringLiteral("人像美化"), pixcake::shell::verifiedPortraitLabels());
add_verified(QStringLiteral("refine-image-labels"), QStringLiteral("图像美化"), pixcake::shell::verifiedImageLabels());
add_verified(QStringLiteral("refine-pet-labels"), QStringLiteral("宠物"), pixcake::shell::verifiedPetLabels());
```

加入 `#include "refine_verified_labels.hpp"`。

- [x] **Step 2: 跑精修 smoke**

Run: `cmake --build build-desktop -j2 --target pixcake_app_shell && QT_QPA_PLATFORM=offscreen build-desktop/apps/shell/pixcake_app_shell.app/Contents/MacOS/pixcake_app_shell --page refine --smoke`

Expected: 退出码 0。

### Task 3: 验收并本地提交

- [x] **Step 1: 跑完整验证**

```sh
ctest --test-dir build-desktop --output-on-failure
cmake --build build-desktop --target architecture_gate
git diff --check
```

尚未编出可执行文件的未提交测试登记，记录为未运行，不修改那些 CMake 文件，也不写成通过。

- [x] **Step 2: 本地提交**

只提交 `apps/shell/main.cpp`、`apps/shell/refine_verified_labels.hpp`、本计划、读取记录。提交说明：

```text
feat(desktop): show verified refine module labels
```

不推送，不创建 PR。
