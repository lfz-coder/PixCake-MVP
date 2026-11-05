# 官方桌面视觉壳层继续执行记录（第二轮）

日期：2026-11-05  
计划：`docs/superpowers/plans/2026-11-05-next-official-desktop-implementation.md`  
分支：`codex/desktop-official-layout`

## 本轮修正

- 主页照片导入卡片移除源码中无法由官方主页图确认的“导入照片”按钮，只保留官方可见的加号、标题和拖拽提示。
- 主页三个功能卡片改用统一的 28×28 `pending-icon` 组件，并为卡片和待补图标补充稳定的 AX 名称。
- 精修页“返回主页面”、图库页“首页”和创意页“首页”恢复为可用的壳层导航；它们只切换已实现的页面，不触发业务导入、网络或文件操作。
- 登录页微信和商家账号入口改用独立对象名和登录页专用样式，继续保持禁用状态；这样后续补充官方图标时不会覆盖通用按钮样式。
- 新增 `apps/shell/assets/README.md`，固定用户可手动补充的官方独立资源文件名、用途和建议画布尺寸。

## Jarvis AX 复测

Jarvis executor `0.1.586`，Accessibility 和 Screen Recording 可用。当前本地 bundle 能启动并读取五页窗口；本轮实际复测读取了登录页、主页、图库、精修和创意页的关键结构。主页读取到七项导航、项目搜索、照片导入、常用卡片、快捷功能、项目列表和空状态；图库读取到显示状态、全效果模式、导入/筛选/排序、批量删除、三种视图和 AI 挑图/预设/元数据；精修读取到返回主页面、顶部切换、工具条、画布、查看对比、功能区和缩略区；创意读取到 12 个手动工具、画布空状态、AI 工具箱、侧边栏、图层面板和底部缩略区。

登录页当前 AX 关键对象：

```text
手机验证码登录
账号输入框 / 输入手机号
验证码输入框 / 输入短信中的验证码 / maxLength=6
获取验证码（disabled）
登录（disabled）
已阅读并同意像素蛋糕《用户使用协议》《隐私政策》
or
微信登录（disabled）
商家账号（disabled）
帮助（disabled）
```

导航按钮恢复可用只影响已实现页面切换；本轮没有产生文件、网络、登录、GPU、AI、导出或项目库副作用。

## 验证

```text
cmake --build build-desktop -j2 --target pixcake_app_shell architecture_gate  PASS
ctest --test-dir build-desktop --output-on-failure                         29/29 PASS
git diff --check                                                            PASS
```

## 仍为 pending

- 官方独立 Logo、微信图标、帮助图标、主页卡片图标、精修/创意工具图标、全局功能图标、官方字体和无标注原图仍未取得。
- 当前运行时继续使用 `pending-icon`，没有用 Unicode、系统图标或相似图片冒充官方资源。
- 真实登录、照片导入、照片解码、GPU 渲染、AI、图层编辑、导出、云空间、工作流、选片交付和 F01–F13 继续延期。
