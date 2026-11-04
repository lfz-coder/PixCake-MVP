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
