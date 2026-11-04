# 官方工作区布局验收记录

日期：2026-11-04  
分支：`codex/desktop-official-layout`  
基线：`49a2c7ddc0c8edfa1726d621a5656e0484ee0fc5`

## 代码与自动化验收

| 检查项 | 结果 | 证据 |
|---|---|---|
| 五页 Qt 壳层构建 | 通过 | `build-desktop/apps/shell/pixcake_app_shell.app` |
| CTest | 通过，29/29 | `ctest --test-dir build-desktop --output-on-failure` |
| 架构门禁 | 通过 | `architecture_gate`、`check_dependencies.cmake`、`check_capability_ownership.cmake` |
| 页面 smoke | 通过 | CTest 中 5 个 `test_shell_app_smoke_*` 均通过；`--page login/home/library/refine/creative --smoke` 均退出码 0 |
| 空状态约束 | 通过 | 未生成项目、照片、图层、AI 工具或业务结果 |
| Qt 边界 | 通过 | Qt 仅在 `apps/shell`；`ui/shell` 保持无 Qt |
| 空白检查 | 通过 | `git diff --check` |

## 页面验收

| 页面 | 区域关系 | 关键文字 | 业务副作用 | 结果 |
|---|---|---|---|---|
| 登录 | 表单居中、帮助区位于底部 | 账号输入框、验证码输入框、6 位验证码、三种登录方式、联系售后、查看快捷键 | 无 | 通过 |
| 主页 | 左侧导航、顶部搜索/全局区、中央导入区、常用/快捷空区、项目列表 | 手册确认的七项导航和全局功能区 | 无 | 通过 |
| 精修 | 顶部返回/搜索/助手和页面切换，左工具栏，中画布，右功能区，底部缩略区 | 12 项工具、查看对比和缩略图区入口 | 无 | 通过 |
| 图库 | 顶部项目内导航，左状态/效果/标识区，中空网格，右侧栏，底部工具和视图入口 | RAW/修图/导出状态、AI挑图/预设/元数据、三种视图 | 无 | 通过 |
| 创意 | 左手动工具栏，中画布，右 AI/侧栏/图层，底部缩略区 | 手动工具、AI工具箱、六项侧栏、图层面板 | 无 | 通过 |

## Jarvis 状态

对照应用 Jarvis 已验证：

```text
app: /Applications/pixcake-test.app
version: 9.3.0
app_key: Pixcake_mac
page_graph: true
axbridge: false
accessibility: true
screen_recording: true
```

本地构建壳层的 Jarvis AX 注册验收仍为阻塞项：当前构建产物未注册到 Jarvis 页面图谱，且计划要求本地应用若无法注册不得宣称 AX 验收通过。因此本批次判定为：**代码布局完成，官方证据和自动化验收完成，Jarvis 本地壳层 AX 验收阻塞**。

## 未确认和延期

- 官方页面图的精确颜色、字体、间距、圆角和图标资源未确认，代码未写死这些值。
- 项目、照片、缩略图、图层、AI 工具和网络状态均保持空状态。
- F01–F13、真实登录、导入、渲染、AI、导出、云空间、账本和历史持久化继续延期。
- 工作区仍含用户既有内核未提交修改和生成目录；本批次未重置、未覆盖、未混入提交。
