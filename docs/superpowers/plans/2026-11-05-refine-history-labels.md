# 精修历史记录空状态 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在精修侧栏放上手册里的历史记录分区，没有照片时不列出步骤。

**Architecture:** 只改 `apps/shell/main.cpp`。分区名用官网标题。空状态沿用壳层已有的「暂无已加载照片」。不把样片截图里的步骤写成运行时数据。

**Tech Stack:** C++17、Qt 6 Widgets、现有精修 smoke。

## Global Constraints

- 正文来自 https://www.pixcakeai.com/guide/47209 ，面包屑是「桌面版使用手册 / 历史记录」，更新日期 2026-05-23。
- 配图是 `https://download.pixcakeai.com/ui/baklib/dcab6bf8aca84a3e13a33b5f79861c86.png`。图里能确认的固定标题是「历史记录」。下面的「女：祛副乳」「预设 全家福」「重置所有效果」和右侧数字是这一张样片的步骤，不写入界面。
- 正文用引号写出的合并名称是「上一次保存」。它只在关闭软件后再次打开时替换先前步骤。当前没有已加载照片，所以不把它显示成一条历史。
- 不在历史区放撤销按钮。正文里的 Ctrl + Z / Command + Z 不是这个分区的可见控件。
- 控件禁用。不连接信号，不写内核，不造步骤。
- 分支保持 `codex/desktop-official-layout`。
- Jarvis 页面图有 `refine_history`，没有 features。「上一次保存」不是页面图里的功能名。测试版当前页面是 unknown，辅助功能树里没有这些文字。没有为了读历史去打开项目。知识库本次未连接。

---

### Task 1: 放入空的历史记录

**Files:**
- Modify: `apps/shell/main.cpp`

- [x] **Step 1: 在裁剪名称之后加入历史记录**

对象名 `refine-history-labels`，无障碍名「历史记录」。可见文字两条，都禁用：

1. 历史记录
2. 暂无已加载照片

第二条的无障碍名是「暂无已加载照片。关闭软件后再次打开时，先前步骤合并为上一次保存。当前不列出步骤。」

- [x] **Step 2: 精修 smoke 确认这块存在**

精修 `--smoke` 时，若找不到 `refine-history-labels`，或两条文字顺序不对，或其中有启用项，返回 3。已有的裁剪检查保持不变。

- [x] **Step 3: 跑精修 smoke**

Run: `cmake --build build-desktop -j2 --target pixcake_app_shell && QT_QPA_PLATFORM=offscreen build-desktop/apps/shell/pixcake_app_shell.app/Contents/MacOS/pixcake_app_shell --page refine --smoke`

Expected: 退出码 0。结果为退出码 0。

### Task 2: 本地提交

- [x] **Step 1: 只提交本计划改到的文件**

```text
feat(desktop): show empty refine history
```

不推送。不加入内核和其他未提交文件。
