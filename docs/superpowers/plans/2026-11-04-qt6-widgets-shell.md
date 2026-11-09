# Qt 6 Widgets 最小真实桌面壳层实施计划

日期：2026-11-04  
状态：本批次已完成（Qt 6 Widgets 最小真实壳层）；F01–F13 未完成、延期处理

## 目标

把已完成的 `ui/shell` 展示契约接入一个真实的 Qt 6 Widgets 主窗口，形成 `pixcake_app_shell`，并提供登录、主页、图库、精修、创意的只读页面结构与已确认的页面导航。

## 已确认决策

- Qt 6.12.0，使用 Widgets 桌面 UI。
- 初始模块只链接 `Qt6::Core`、`Qt6::Gui`、`Qt6::Widgets`。
- 使用 `QMainWindow`、`QStackedWidget`、普通 `QWidget`、菜单/按钮/对话框；不使用 QML、Qt Quick、Qt WebEngine、Qt SQL、Qt Multimedia 或 `QOpenGLWidget`。
- 目标名为 `pixcake_app_shell`，目录为 `apps/shell`。
- `ui/shell` 保持无 Qt，仅提供页面身份、区域和条目数据。
- 画布先是普通占位控件；渲染进程和共享内存帧另行计划。

## 证据与范围

- 需求与架构文档规定 Qt 壳负责窗口、菜单、登录、导航和只读模型；内核不依赖 Qt。
- 官方工作区手册确认工作区页面及其区域和条目；Jarvis 已确认对照应用 `/Applications/pixcake-test.app` 9.3.0 的精修页结构。
- 本批次不实现登录请求、项目库、导入解码、导出提交、账本扣费、源文件删除、历史写盘、GPU、像素渲染和单实例锁。
- F01–F13 继续标记为“未完成、延期处理”。

## 步骤

1. 增加 `apps/shell` 的 Qt 6 CMake 目标和 `QMainWindow` 实现，读取 `ui::shell::ShellDisplay` 的区域模型。
2. 将区域渲染为只读分组、标签和按钮；为已确认的页面切换、首页项目入口和本地项目页接入导航。
3. 提供一个不改变产品行为的 `--page login|home|local-projects|library|refine|creative` 启动参数，用于验收不同页面的真实窗口布局；默认仍显示登录页。
4. 增加构建 smoke test，验证目标可链接 Qt 6；保留现有无 Qt 单元测试和架构门禁。
5. 用 Jarvis 启动本地壳层，读取 AX 树确认窗口标题和页面区域；更新计划和延期清单。

## 完成条件

- `pixcake_app_shell` 使用 Qt 6 Widgets 构建并能启动真实窗口。
- 默认登录页与工作区页面契约一致；启动参数可切换只读展示页面。
- `ui/` 不包含 GPU 后端依赖，`kernel/` 不包含 Qt。
- 全量 CTest、架构门禁和 Jarvis AX 检查通过。

## 本批次结果

- 已完成：`apps/shell`、`pixcake_app_shell`、Qt 6 Widgets `.app` bundle、只读页面渲染、首页与本地项目页入口、已确认的项目内页面切换、`--page` 验收参数。
- 已验证：Qt 6.12.0 构建成功；Jarvis AX 读取到真实窗口标题和登录页区域；架构门禁通过。
- 延期处理：真实登录、项目库、导入解码、导出提交、账本、源文件删除、历史写盘、GPU/共享帧、单实例锁，以及 F01–F13 审核修复。

后续桌面布局工作必须在专用桌面分支进行，不能继续使用 `kernel-project-library`；详见 [官方工作区视觉与布局对齐计划](2026-11-04-official-workspace-visual-alignment.md)。
