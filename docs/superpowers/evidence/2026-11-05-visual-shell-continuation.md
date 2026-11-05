# 官方桌面视觉壳层继续执行记录

日期：2026-11-05
计划：`docs/superpowers/plans/2026-11-05-next-official-desktop-implementation.md`
分支：`codex/desktop-official-layout`

## 本轮完成

本轮继续处理上一轮未完成的共享视觉层和登录页大区比例：

- `PixCakeTheme` 增加窗口、控件、圆角、工具栏和侧栏尺寸 token，并把待补充官方图标统一标记为 `pending-icon`。
- `PixCakeToolbar`、`PixCakeStatusStrip`、`PixCakeEmptyState`、`PixCakeField`、`PixCakePanel` 和 `pendingIcon` 统一提供布局、对象名和 AX 名称。
- 登录页外边距和表单布局调整为官方截图可确认的黑色工作区、左人物图、右侧垂直居中表单；登录、验证码、协议、微信/商家账号和帮助入口继续保持只读/禁用。
- 精修工具标签修正为官方手册可确认的 12 项；创意手动工具栏保留 12 项；图库状态条和各页空状态使用共享组件。
- 精修侧栏宽度、图库侧栏宽度、画布和空状态的面板层级统一到主题 token。

## 自动化验证

以下检查在本轮代码上通过：

```text
cmake --build build-desktop -j2 --target pixcake_app_shell architecture_gate
QT_QPA_PLATFORM=offscreen ctest --test-dir build-desktop --output-on-failure
git diff --check
```

结果：`29/29` CTest 通过，`architecture_gate` 通过，差异格式检查通过。五页 smoke（login/home/library/refine/creative）均通过。

## Jarvis 验证

Jarvis executor `0.1.586`，Accessibility 和 Screen Recording 权限可用。屏幕锁定前，本地 bundle 的登录页已验证：

- 窗口标题“像素蛋糕”，窗口约 `1386×832`；
- 人物图 AXImage 可读，表单区域位于窗口中部；
- `手机验证码登录`、账号输入框、验证码输入框、`获取验证码`、禁用登录按钮、协议复选框、`or`、微信登录、商家账号和帮助入口均可读；
- 验证码字段的 UI 约束仍为 `maxLength=6`；
- 未产生文件、网络、登录或业务副作用。

本轮尝试继续启动其他页面时，Jarvis 返回 `screen_locked`，因此没有把锁屏状态下的页面截图或 AX 结果写成通过。解锁执行机后需要重新验证 home/library/refine/creative 的可见窗口和 AX。

## 仍为 pending

- 官方独立 Logo、微信图标、帮助图标、工具图标、官方字体和无标注原图仍未取得；运行时保留 `pending-icon`，不使用相似图片或 Unicode 冒充。
- 官方截图中的项目卡片、照片缩略图、会员数字、AI 结果、图层和预设不复制为业务数据。
- 真实登录、短信、微信、商家账号、照片导入、照片解码、GPU 渲染、AI、图层编辑、导出、云空间、工作流、选片交付和 F01–F13 继续延期。
- 官方独立资源未补齐前，本计划不能宣称“像素级完全一致”；当前结论是“可证实视觉壳层继续完成，独立资源和真实业务 pending”。
