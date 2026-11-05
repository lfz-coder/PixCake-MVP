# 精修裁剪旋转只读名称 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 按官网桌面手册把裁剪、旋转的名称放进精修侧栏，控件保持禁用。

**Architecture:** 只改 `apps/shell/main.cpp` 的精修功能区。名称顺序和用词跟手册标题一致。角度只有范围、没有默认值和步进，所以只放禁用文字，不放滑杆，也不写当前值。

**Tech Stack:** C++17、Qt 6 Widgets、现有精修 smoke。

## Global Constraints

- 名称和顺序来自 https://www.pixcakeai.com/guide/32478 ，更新日期 2026-09-22，位于「桌面版使用手册 / 裁剪旋转」。这一页高于设计文档第 12.6 节的用词。
- 可见用词用手册原文：`长*宽*分辨率`、`AI智能裁剪`、`AI水平矫正`、`角度滑杆`。不用设计文档里的 `长×宽×分辨率`、`AI 智能裁剪`、`AI 水平矫正`。
- 角度范围是 -45 到 45。手册没写默认值和步进，不设 `QSlider` 当前值。
- 分格数没有数字范围，不做成滑杆。
- 不勾选几何中心十字线或 AI水平矫正。
- 不加入创意手册里的「删除裁剪的像素」。那一句在 https://www.pixcakeai.com/guide/158252 ，不是这一页。
- 控件禁用。不连接信号，不写内核，不导入照片。
- 分支保持 `codex/desktop-official-layout`。
- Jarvis 页面图有 `refine_crop`，但该节点没有 features。执行时页面状态是 unknown，没有为了读名称去点测试版。知识库本次未连接。

---

### Task 1: 放入禁用名称

**Files:**
- Modify: `apps/shell/main.cpp`

- [x] **Step 1: 在宠物名称之后加入裁剪、旋转**

对象名 `refine-crop-labels`，无障碍名「裁剪、旋转」。可见文字按手册顺序：

裁剪、原始尺寸、原照设置、固定比例、长*宽*分辨率、自由尺寸、证件照尺寸、我的裁剪预设、AI智能裁剪、网格线、分格数、几何中心十字线、旋转、逆时针、顺时针、水平翻转、垂直翻转、角度滑杆、AI水平矫正。

「角度滑杆」的无障碍名是「角度滑杆，范围 -45 到 45，默认和步进手册未写」。每一项 `setEnabled(false)`。

- [x] **Step 2: 精修 smoke 确认这块存在**

精修 `--smoke` 时若找不到对象名 `refine-crop-labels`，或 19 条可见文字顺序不对，或其中有启用项，返回 3。

- [x] **Step 3: 跑精修 smoke**

Run: `cmake --build build-desktop -j2 --target pixcake_app_shell && QT_QPA_PLATFORM=offscreen build-desktop/apps/shell/pixcake_app_shell.app/Contents/MacOS/pixcake_app_shell --page refine --smoke`

Expected: 退出码 0。结果为退出码 0。
