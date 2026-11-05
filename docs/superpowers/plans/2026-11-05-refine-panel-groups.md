# 补齐精修侧栏剩余分组 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把先前只留成标题的调色分组、局部蒙版名和后调色入口放进精修侧栏。

**Architecture:** 只改 `apps/shell/main.cpp`。滑杆保持禁用。范围使用已经写进记录的数字。不处理登录、导入、渲染、AI 和导出。

**Tech Stack:** C++17、Qt 6 Widgets、现有精修 smoke。

## Global Constraints

- 分支保持 `codex/desktop-official-layout`。
- HSL 八个颜色按截图顺序：红色、橙色、黄色、绿色、浅绿色、蓝色、紫色、洋红色。
- 可选颜色使用「中间色（灰色）」。
- 色阶黑场、白场、输出黑场、输出白场为 0～255；灰场为 -1.0～1.0，界面用 -100～100 表示，读数显示 0.00。
- 后调色只列全图调整、局部调整，以及与调色相同的分组标题。
- 局部名称来自 https://www.pixcakeai.com/guide/32454 。羽化为 0～100，默认 0。

---

### Task 1: 展开分组并加入局部与后调色

**Files:**
- Modify: `apps/shell/main.cpp`

- [x] **Step 1: 把有范围的分组做成禁用滑杆**

色阶、曲线里的数值项、HSL、色彩编辑器的三项偏移、颜色分级、可选颜色、细节、颗粒、镜头调整、透视矫正、校准都使用已记录的范围。创意色彩程度保持 0～100。

- [x] **Step 2: 加入局部调色和后期调色**

局部调色使用手册中的蒙版名和羽化滑杆。后期调色使用全图调整、局部调整和同一套分组标题。

- [x] **Step 3: 跑精修 smoke**

Run: `cmake --build build-desktop -j2 --target pixcake_app_shell && QT_QPA_PLATFORM=offscreen build-desktop/apps/shell/pixcake_app_shell.app/Contents/MacOS/pixcake_app_shell --page refine --smoke`

Expected: 退出码 0。结果为退出码 0。

### Task 2: 本地提交

- [x] **Step 1: 只提交本计划改到的文件**

```text
feat(desktop): fill remaining refine panel groups
```

不推送。
