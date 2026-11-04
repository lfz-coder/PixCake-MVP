# 桌面视觉壳层验收记录

日期：2026-11-04
分支：`codex/desktop-official-layout`
提交：`6c95b4d feat(desktop): align workspace page visual shells`

## 已完成

- 登录页：黑色工作区、人物图裁剪、验证码表单、协议行、微信/商家入口、帮助入口。
- 主页：左侧导航、顶部搜索/全局功能、导入区域、常用功能、快捷功能、项目列表空状态。
- 精修：顶部页面切换、全局功能、工具栏、中央画布占位、右侧功能区、底部缩略图区。
- 图库：顶部状态控制、导入/筛选/排序/批量删除、空网格、AI挑图/预设/元数据侧边栏、底部缩略图区。
- 创意：顶部切换、手动工具栏、中央画布占位、AI工具箱、侧边栏、图层面板、底部缩略图区。

## 自动化验证

```text
cmake --build build-desktop -j2                 PASS
ctest --test-dir build-desktop --output-on-failure 29/29 PASS
cmake --build build-desktop --target architecture_gate -j2 PASS
check_dependencies.cmake                         PASS
check_capability_ownership.cmake                PASS
git diff --check                              PASS
QT_QPA_PLATFORM=offscreen --page login --smoke   PASS
QT_QPA_PLATFORM=offscreen --page home --smoke    PASS
QT_QPA_PLATFORM=offscreen --page library --smoke PASS
QT_QPA_PLATFORM=offscreen --page refine --smoke  PASS
QT_QPA_PLATFORM=offscreen --page creative --smoke PASS
```

## 当前阻塞

Jarvis 对本地壳层的 AX 验收仍未完成。Jarvis 启动 `build-desktop/apps/shell/pixcake_app_shell.app` 时返回 `window_not_exposed_after_8s`。构建 bundle 含 `Contents/MacOS/pixcake_app_shell` 和 `Info.plist`，但当前 macOS 环境没有向 Jarvis 暴露窗口。未把此项写成通过，也没有通过截图猜测 AX 结果。

## 明确延期

真实登录、短信、微信、商家账号、项目库、照片导入、缩略图生成、GPU/画布渲染、AI、图层编辑、导出、云空间、账本和 F01–F13 继续延期。

## Jarvis AX 验收（已完成）

Jarvis 通过临时验收 bundle 读取到五页窗口，窗口标题均为“像素蛋糕”，窗口矩形约为 1386×832：

- 登录：读取到“手机验证码登录”、账号输入框、验证码输入框、获取验证码、登录、协议勾选、微信登录、商家账号和帮助；验证码输入框具有 `maxLength=6` 对应的代码约束，登录和登录方式按钮保持禁用。
- 主页：读取到主页、AI 工具箱、本地项目、云空间、工作流、选片交付、回收站、搜索所有项目、会员续费/全局功能区、照片导入、常用功能、快捷功能、项目列表和“暂无项目”。
- 精修：读取到图库/精修/创意切换、返回主页面、搜索、像素助手、技术支持/个人中心/消息中心/传输列表/导出列表/导出、13 个工具条目、预览区域、查看对比、功能区和底部筛选/排序/去挑图/标记入口。
- 图库：读取到图库/精修/创意切换、显示状态、全效果模式、导入新图片、筛选、排序、批量删除、空网格、AI挑图/预设/元数据侧边栏、底部缩略图区。
- 创意：读取到图库/精修/创意切换、AI工具/历史/批量/帮助/导出、12 个手动工具、画布、AI工具箱、侧边栏、图层面板和底部缩略图区。

所有 AX 页面均没有触发网络、登录、文件导入、渲染、AI 或导出副作用。临时验收 bundle 位于 `/tmp/pixcake-*.app`，未加入仓库。
