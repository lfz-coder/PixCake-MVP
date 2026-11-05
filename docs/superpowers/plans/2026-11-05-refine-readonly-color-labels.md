# 精修调色只读名称 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 9.3.0 现场读到的调色名称放进精修侧栏，不放任何滑杆数值。

**Architecture:** 只改 `apps/shell/main.cpp` 的精修侧栏。模块页签继续禁用。调色面板的名称单独放在一组禁用标签里，数值、范围和默认值都不写。颜色、字号、间距继续用现有 token。

**Tech Stack:** C++17、Qt 6 Widgets、现有 `pixcake_app_shell` smoke、CTest。

## Global Constraints

- 名称以 `docs/superpowers/evidence/2026-11-05-refine-color-panel-reading.md` 为准。
- 分支保持 `codex/desktop-official-layout`。
- 不新增图标，不用 Unicode 补图标。
- 不调用 `setValue`、`setRange`、`setChecked`。
- 不把现场滑杆文字 70、102、-1、4650 或内核 `ParameterMode` 写进界面。
- 不加入「宠物」的新描述，不删除已有「宠物」按钮。
- 不加入「ISO -」「- mm」「F/-」「-秒」和文字为 test 的控件。
- 登录、导入、渲染、AI、导出和滑杆交互不在本计划。

---

### Task 1: 给已核对的模块页签写上描述

**Files:**
- Modify: `apps/shell/main.cpp` 的 `render_workspace()`

**Interfaces:**
- Consumes: 已有的 `module_labels` 和 `action()`。
- Produces: 九个按钮的无障碍描述。可见文字不变。

- [x] **Step 1: 在创建模块按钮后设置描述**

保留这 11 个可见名和现有禁用状态：AI色彩、调色、局部、人像、图像、衣物、宠物、后调色、裁剪、历史、联机。

只给下面九个调用 `setAccessibleDescription`：

```cpp
const QHash<QString, QString> module_descriptions = {
    {QStringLiteral("调色"), QStringLiteral("色彩调节")},
    {QStringLiteral("局部"), QStringLiteral("局部调色")},
    {QStringLiteral("人像"), QStringLiteral("人像美化")},
    {QStringLiteral("图像"), QStringLiteral("图像美化")},
    {QStringLiteral("衣物"), QStringLiteral("衣物美化")},
    {QStringLiteral("后调色"), QStringLiteral("后期调色")},
    {QStringLiteral("裁剪"), QStringLiteral("裁剪、旋转")},
    {QStringLiteral("历史"), QStringLiteral("历史记录")},
    {QStringLiteral("联机"), QStringLiteral("联机拍摄")}};
```

AI色彩和宠物不进入这个表。按钮继续 `setEnabled(false)`。

- [x] **Step 2: 跑精修 smoke**

Run: `cmake --build build-desktop -j2 --target pixcake_app_shell && QT_QPA_PLATFORM=offscreen build-desktop/apps/shell/pixcake_app_shell.app/Contents/MacOS/pixcake_app_shell --page refine --smoke`

Expected: 退出码 0。

### Task 2: 放入调色面板的只读名称

**Files:**
- Modify: `apps/shell/main.cpp` 的 `render_workspace()`
- 需要在文件顶部确认已包含 `<QHash>`。没有则加入。

**Interfaces:**
- Consumes: Task 1 的模块按钮。
- Produces: 对象名 `refine-color-labels`，无障碍名「色彩调节」。里面只有下列禁用标签，没有滑杆。

- [x] **Step 1: 在模块按钮后面加名称组**

父控件使用已有的 `modules`。不要新建 `modules_frame`。

```cpp
auto* color_labels = new QFrame(modules);
color_labels->setObjectName(QStringLiteral("refine-color-labels"));
color_labels->setAccessibleName(QStringLiteral("色彩调节"));
auto* color_layout = new QVBoxLayout(color_labels);
color_layout->setContentsMargins(0, 0, 0, 0);
const QStringList color_names = {
    QStringLiteral("开启 AI 色彩风格"), QStringLiteral("配置文件"),
    QStringLiteral("色彩调节"), QStringLiteral("预设"), QStringLiteral("滤镜"),
    QStringLiteral("推荐"), QStringLiteral("我的"), QStringLiteral("AI 校准"),
    QStringLiteral("程度"), QStringLiteral("白平衡"), QStringLiteral("AI校准白平衡"),
    QStringLiteral("白平衡选择器"), QStringLiteral("色温"), QStringLiteral("色调"),
    QStringLiteral("影调"), QStringLiteral("AI校准曝光"), QStringLiteral("曝光"),
    QStringLiteral("对比度"), QStringLiteral("亮度"), QStringLiteral("高光"),
    QStringLiteral("阴影"), QStringLiteral("白色"), QStringLiteral("黑色"),
    QStringLiteral("纹理"), QStringLiteral("清晰度"), QStringLiteral("祛雾"),
    QStringLiteral("鲜艳度"), QStringLiteral("饱和度"), QStringLiteral("色阶"),
    QStringLiteral("曲线"), QStringLiteral("HSL"), QStringLiteral("色彩编辑器"),
    QStringLiteral("颜色分级"), QStringLiteral("可选颜色"), QStringLiteral("细节"),
    QStringLiteral("颗粒"), QStringLiteral("镜头调整"), QStringLiteral("透视矫正"),
    QStringLiteral("校准"), QStringLiteral("创意色彩")};
for (const auto& label : color_names) {
  auto* name = text(label);
  name->setEnabled(false);
  color_layout->addWidget(name);
}
modules_layout->addWidget(color_labels);
```

`text()` 是现有辅助函数。不要给这些标签加数值、滑杆或勾选状态。边距保持 0，不新增间距测量，也不改主题。

- [x] **Step 2: 再跑精修 smoke**

Run: `QT_QPA_PLATFORM=offscreen build-desktop/apps/shell/pixcake_app_shell.app/Contents/MacOS/pixcake_app_shell --page refine --smoke`

Expected: 退出码 0。

### Task 3: 验收并本地提交

**Files:**
- Modify: `docs/superpowers/evidence/2026-11-05-refine-color-panel-reading.md`，只追加验证命令结果。

- [x] **Step 1: 跑完整验证**

```sh
ctest --test-dir build-desktop --output-on-failure
cmake --build build-desktop --target architecture_gate
git diff --check
```

Expected: CTest 全部通过，架构门禁通过，`git diff --check` 无输出。

- [x] **Step 2: 把验证结果补进读取记录**

写明精修侧栏有九个无障碍描述，以及「色彩调节」组里的名称数量。写明仍然没有滑杆范围、默认值和当前值。

- [x] **Step 3: 本地提交**

只提交 `apps/shell/main.cpp`、本计划、读取记录。提交说明：

```text
feat(desktop): show verified refine color labels
```

不推送，不创建 PR。
