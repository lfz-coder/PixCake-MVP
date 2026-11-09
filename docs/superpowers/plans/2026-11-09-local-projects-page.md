# 本地项目页入口闭环计划

日期：2026-11-09  
状态：已实现

## 背景

产品规格已经定义左侧“本地项目”入口，并规定本地项目页只展示本机创建的项目和从云空间下载到本机的项目。此前壳层只画出了导航文字，按钮被禁用；首页下方的项目列表虽然能双击打开项目，却不能替代独立的本地项目页。

## 设计

- `ui/shell::Page` 增加 `LocalProjects`，页面不属于项目工作区，允许从首页进入并返回首页。
- `ShellController::projects()` 继续从 `ProjectSession` 读取完整项目行，同时向壳层传递 `origin` 和 `cloud_marked`，页面过滤 `本地` 与 `云下载`。
- 页面提供返回首页、项目名搜索、项目列表、双击打开和右键删除；双击沿用现有 `request_open_project`，按项目上次保存的位置进入图库或精修。
- 首页项目卡片继续保持官方实测行为：单击选中，双击打开。过时的“点击进入”注释改为“双击打开”。
- AI 工具箱、云空间、工作流和选片交付仍保持禁用，因为它们需要各自的服务端页面，不在本条本地项目链路内。

## 实现文件

- `ui/shell/include/pixcake/ui/shell/shell_display.hpp`
- `ui/shell/src/shell_display.cpp`
- `ui/shell/src/page_copy.cpp`
- `apps/shell/local_projects_page.cpp`
- `apps/shell/home_page.cpp`
- `apps/shell/shell_window.cpp`
- `apps/shell/shell_controller.cpp`

## 验证

- `test_shell_display` 覆盖首页与本地项目页的状态机、区域和文案契约。
- `test_shell_sources` 保持页面文件、文案来源和页面只绘制门禁。
- 开发构建增加 `--page local-projects` 和对应 smoke 目标。
