# 已确认范围的只读滑杆 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把已经确认范围的非 Free 滑杆放进精修侧栏，并把用户批准用市面惯例填上的空缺写进记录。

**Architecture:** 只改精修侧栏里「色彩调节」那一组。滤镜程度、色温、色调和影调各滑杆使用最小值 -100、最大值 100、步进 1、默认 0，控件保持禁用。创意色彩按市面惯例只加一支「程度」，范围 0 到 100，默认 0。带 Free 的分组仍是纯文字。色阶步进和输出黑白只写入记录，不做成控件。

**Tech Stack:** C++17、Qt 6 Widgets、现有 `pixcake_app_shell` smoke。

## Global Constraints

- 分支保持 `codex/desktop-official-layout`。
- 架构第 17 节带 Free 的分组不做成滑杆：AI 校准、色阶、曲线、HSL、色彩编辑器、颜色分级、可选颜色、细节、颗粒、镜头调整、透视矫正、校准。
- 色温到饱和度的 -100～100 来自用户对双向拖动的确认，不是截图取样。
- 创意色彩的「程度」、色阶整数步进 1、灰场步进 0.01、输出黑场和白场 0～255，来源标明为市面惯例，且用户只批准用在这些空缺上。
- 滑杆禁用。不连接信号，不写内核。
- 不改登录、导入、渲染、AI、导出。

---

### Task 1: 把空缺写进记录

**Files:**
- Modify: `docs/superpowers/evidence/2026-11-05-refine-color-slider-bounds.md`
- Modify: `docs/superpowers/evidence/2026-11-05-visual-tokens-unclear.md`

- [x] **Step 1: 写入市面惯例**

色阶黑场、白场步进写 1。灰场步进写 0.01。输出色阶黑场范围 0～255、默认 0、步进 1。输出色阶白场范围 0～255、默认 255、步进 1。创意色彩增加滑杆「程度」，范围 0～100、步进 1、默认 0。视觉记录的空位填上深色修图软件常见的滑杆尺寸，并注明可再改。

### Task 2: 放置禁用滑杆

**Files:**
- Modify: `apps/shell/main.cpp`
- Modify: `apps/shell/pixcake_theme.hpp`

- [x] **Step 1: 只给非 Free 且已确认范围的名称加滑杆**

「程度」「色温」「色调」「曝光」「对比度」「亮度」「高光」「阴影」「白色」「黑色」「纹理」「清晰度」「祛雾」「鲜艳度」「饱和度」使用 `QSlider`，`setRange(-100, 100)`，`setSingleStep(1)`，`setValue(0)`，`setEnabled(false)`。无障碍名写明范围、步进和默认值。旁边用禁用文字显示默认值 0。

「创意色彩」标题下再加一支「程度」，对象名 `refine-creative-color-amount`，范围 0 到 100，默认 0，无障碍名「创意色彩程度，范围 0 到 100，步进 1，默认 0」。来源备注为市面惯例。

其余名称保持禁用文字。

- [x] **Step 2: 给滑杆补深色外观**

在现有样式表增加 `QSlider` 规则：槽高 4px，颜色 `#4a4a4a`；已走过的部分 `#d0d0d0`；手柄 14×14px，颜色 `#d9d9d9`。这些写进视觉待调整记录，方便以后改数。

- [x] **Step 3: 跑精修 smoke**

Run: `cmake --build build-desktop -j2 --target pixcake_app_shell && QT_QPA_PLATFORM=offscreen build-desktop/apps/shell/pixcake_app_shell.app/Contents/MacOS/pixcake_app_shell --page refine --smoke`

Expected: 退出码 0。

### Task 3: 本地提交

- [x] **Step 1: 只提交本计划改到的文件**

```text
feat(desktop): show confirmed refine slider ranges
```

不推送。未编出的无关测试不修改。
