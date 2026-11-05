# 补上已有名称但尚未摆出的控件 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把页面图和手册里已经有名字、但精修侧栏还没显示的项补成禁用文字。

**Architecture:** 只改 `apps/shell/main.cpp` 的色彩调节分组。没有范围的项不加滑杆，也不勾选开关。

**Tech Stack:** C++17、Qt 6 Widgets、现有精修 smoke。

## Global Constraints

- 曲线通道名来自页面图：RGB、亮度、红、绿、蓝。
- 色彩平滑、亮度平滑、显示所选色彩范围、反转色彩范围来自 https://www.pixcakeai.com/guide/32455 。
- 相对、绝对来自同一页的可选颜色。不勾选其中任何一个。
- 颗粒颜色、颗粒风格、镜头和透视的开关名来自页面图。不调用 `setChecked`。
- 直方图设置页这次没有打开，不编勾选项。

---

### Task 1: 补名称

**Files:**
- Modify: `apps/shell/main.cpp`

- [x] **Step 1: 在对应分组下加入禁用文字**

- [x] **Step 2: 跑精修 smoke**

Run: `cmake --build build-desktop -j2 --target pixcake_app_shell && QT_QPA_PLATFORM=offscreen build-desktop/apps/shell/pixcake_app_shell.app/Contents/MacOS/pixcake_app_shell --page refine --smoke`

Expected: 退出码 0。结果为退出码 0。

### Task 2: 本地提交

- [x] **Step 1: 提交本计划改到的文件**

```text
feat(desktop): show remaining named refine controls
```

不推送。
