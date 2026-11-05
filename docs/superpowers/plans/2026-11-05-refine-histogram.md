# 精修 RGB 分布直方图 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在精修调色区顶部放上手册描述的直方图空框，横轴 0 到 255，不编造像素分布。

**Architecture:** 新增只绘制坐标轴的 `PixCakeHistogram`。没有已加载照片时不画柱。直方图下方保留 RGB 和 Lab 两个读数位，不填数字。高度使用现有控件高度的两倍，并记入视觉待调整记录。

**Tech Stack:** C++17、Qt 6 Widgets、现有精修 smoke。

## Global Constraints

- 依据是 https://www.pixcakeai.com/guide/32455 的「直方图」一节。
- 横轴是亮度 0 到 255，纵轴是像素数量。没有照片时纵轴没有数据。
- 不从窗口像素反推 RGB 或 Lab。
- 不实现右键菜单和个人中心设置的保存。
- 分支保持 `codex/desktop-official-layout`。

---

### Task 1: 画出空直方图

**Files:**
- Modify: `apps/shell/pixcake_widgets.hpp`
- Modify: `apps/shell/main.cpp`
- Modify: `docs/superpowers/evidence/2026-11-05-visual-tokens-unclear.md`

- [x] **Step 1: 增加直方图控件**

对象名 `refine-histogram`，无障碍名「直方图」。只画底边和文字 0、255。

- [x] **Step 2: 放在色彩调节组的第一项**

下面加两个禁用读数位，可见文字是 RGB、Lab。无障碍名分别是「RGB读数，暂无已加载照片」「Lab读数，暂无已加载照片」。

- [x] **Step 3: 跑精修 smoke**

Run: `cmake --build build-desktop -j2 --target pixcake_app_shell && QT_QPA_PLATFORM=offscreen build-desktop/apps/shell/pixcake_app_shell.app/Contents/MacOS/pixcake_app_shell --page refine --smoke`

Expected: 退出码 0。

### Task 2: 本地提交

- [x] **Step 1: 只提交本计划改到的文件**

```text
feat(desktop): show empty refine histogram
```

不推送。
