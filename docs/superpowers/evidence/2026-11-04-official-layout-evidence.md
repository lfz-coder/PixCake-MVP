# 官方工作区布局证据记录

日期：2026-11-04  
分支：`codex/desktop-official-layout`  
官方手册：[工作区页面介绍](https://www.pixcakeai.com/guide/32445)（更新日期：2026-09-17）  
Jarvis 对照应用：`/Applications/pixcake-test.app`，`Pixcake_mac`，版本 `9.3.0`

## 证据边界

- `official-text` 用于页面名称、区域文字和功能范围。
- `jarvis-ax` 用于确认当前版本页面图谱、窗口可达性和可见标签。
- 官方页面图资源已在计划中登记，但本次未把网络图像作为像素测量来源；颜色、字体、精确间距、圆角、图标资源和默认业务数据保持未确认。
- 所有实现均为只读壳层展示或页面导航，不触发登录、文件、网络、AI、导出或计费行为。

## 页面记录

### 登录页

| 区域 ID | 位置/层级 | 官方文字 | 交互状态 | 证据 | 当前实现 | 状态 |
|---|---|---|---|---|---|---|
| login-account | 登录卡片表单 | 账号输入框 | 只读输入占位 | official-text §登录页面概述；jarvis-ax 页面图谱无登录提交动作 | `QLineEdit`，账号占位 | confirmed |
| login-code | 账号下方 | 验证码输入框、获取验证码、6 位数字 | 获取验证码禁用；输入仅限 6 位 | official-text §登录页面概述 | `QLineEdit(maxLength=6)` + 禁用按钮 | confirmed |
| login-methods | 表单下方 | 手机验证码登录、微信登录、商家账号登录 | 展示/禁用 | official-text §登录页面概述 | 三个禁用按钮 | confirmed |
| login-help | 登录卡片下方 | 联系售后、查看快捷键 | 展示/禁用 | official-text §登录页面概述 | 两个禁用按钮 | confirmed |

未确认：官方图的精确背景、字体、边距、圆角、图标和登录卡片像素尺寸。

### 主页

| 区域 ID | 位置/层级 | 官方文字 | 交互状态 | 证据 | 当前实现 | 状态 |
|---|---|---|---|---|---|---|
| home-nav | 左侧 | 主页、AI工具箱、本地项目、云空间、工作流、选片交付、回收站 | 页面入口展示 | official-text §主页面概述；jarvis-ax `home` 页面 | 左侧导航组 | confirmed |
| home-search | 顶部 | 搜索 | 只读输入占位 | official-text §主页面概述 | 顶部搜索框 | confirmed |
| home-global | 顶部右侧 | 技术支持、个人中心、消息中心、传输列表、导出列表 | 展示/禁用 | official-text §主页面概述 | 顶部功能按钮 | confirmed |
| home-import | 中央主要区域 | 导入照片创建新项目 | 展示/禁用 | official-text §主页面概述 | 空状态卡片 | confirmed |
| home-common | 中央 | 常用功能 | 空状态 | official-text §主页面概述 | 空状态卡片 | confirmed |
| home-quick | 中央 | 快捷功能 | 空状态 | official-text §主页面概述 | 空状态卡片 | confirmed |
| home-projects | 底部/主要内容 | 项目列表 | 空状态 | official-text §主页面概述 | “暂无内容” | confirmed |

未确认：项目数量、缩略图、云标识、用户头像、默认筛选/排序值和精确视觉参数。

### 精修页

| 区域 ID | 位置/层级 | 官方文字 | 交互状态 | 证据 | 当前实现 | 状态 |
|---|---|---|---|---|---|---|
| refine-top | 顶部 | 返回主页面、搜索、像素助手 | 返回可导航，其余展示/禁用 | official-text §精修页面概述；jarvis-ax `refine` | 顶部工具行 | confirmed |
| refine-switch | 顶部次行 | 图库、精修、创意 | 页面导航 | official-text §精修页面概述；jarvis-ax `refine` | 三个导航按钮 | confirmed |
| refine-global | 顶部右侧 | 技术支持、个人中心、消息中心、传输列表、导出列表、导出 | 展示/禁用 | official-text §精修页面概述 | 顶部功能按钮 | confirmed |
| refine-toolbar | 左侧 | 12 项工具栏 | 展示/禁用 | official-text §精修页面概述 | 左侧工具组 | confirmed |
| refine-canvas | 中央 | 画布、查看对比 | 预览空状态 | official-text §精修页面概述 | 普通 QWidget 占位 | confirmed |
| refine-thumbs | 底部 | +、筛选、排序、去挑图、星标、旗标、色标、单视图、对比视图、参考视图 | 展示/禁用 | official-text §精修页面概述 | 底部空状态组 | confirmed |
| refine-modules | 右侧 | 功能区 | 空状态 | official-text §精修页面概述 | 空状态组 | confirmed |

未确认：具体图标、画布比例、真实图片、默认工具状态、精确视觉参数。

### 图库页

| 区域 ID | 位置/层级 | 官方文字 | 交互状态 | 证据 | 当前实现 | 状态 |
|---|---|---|---|---|---|---|
| library-nav | 顶部 | 图库、精修、创意等项目内导航 | 页面导航 | official-text §图库页面概述；jarvis-ax `gallery` | 顶部导航组 | confirmed |
| library-status | 左侧 | 显示RAW格式、显示修图状态、显示导出状态 | 展示/禁用 | official-text §图库页面概述 | 左侧状态组 | confirmed |
| library-effect | 左侧 | 全效果模式 | 展示/禁用 | official-text §图库页面概述 | 左侧空状态组 | confirmed |
| library-thumbs | 左侧/底部 | RAW图、已导出、已编辑、旗标、星标、色标 | 标识说明 | official-text §图库页面概述 | 标识组 | confirmed |
| library-delete | 左侧 | 批量删除 | 禁用展示 | official-text §图库页面概述 | 禁用按钮 | confirmed |
| library-side | 右侧 | AI挑图、预设、元数据 | 展示/禁用 | official-text §图库页面概述；jarvis-ax `gallery_ai_culling` 等 | 侧边栏组 | confirmed |
| library-tools | 底部/工具区 | 导入新图片、筛选、排序 | 禁用展示 | official-text §图库页面概述 | 工具组 | confirmed |
| library-views | 网格底部 | 网格视图、单视图、筛选视图 | 禁用展示 | official-text §图库页面概述；jarvis-ax `gallery_*_view` | 视图组 | confirmed |

未确认：默认网格列数（仅确认范围 1–7）、缩略图素材、标识默认值、筛选/排序默认值和精确视觉参数。

### 创意页

| 区域 ID | 位置/层级 | 官方文字 | 交互状态 | 证据 | 当前实现 | 状态 |
|---|---|---|---|---|---|---|
| creative-tools | 左侧 | 手动工具栏 12 项 | 展示/禁用 | official-text §创意页面概述；jarvis-ax `creative` | 左侧工具组 | confirmed |
| creative-canvas | 中央 | 画布 | 预览空状态 | official-text §创意页面概述 | 普通 QWidget 占位 | confirmed |
| creative-thumbs | 底部 | 导入新图片、筛选、排序 | 禁用展示 | official-text §创意页面概述 | 底部工具组 | confirmed |
| creative-toolbox | 右侧 | AI工具箱 | 空状态 | official-text §创意页面概述 | 空状态组 | confirmed |
| creative-side | 右侧 | AI工具、历史记录、批量设计、帮助、新手引导、反馈 | 展示/禁用 | official-text §创意页面概述 | 侧边栏组 | confirmed |
| creative-layers | 右侧/底部 | 图层面板 | 空状态 | official-text §创意页面概述 | 空状态组 | confirmed |

未确认：AI 工具箱具体工具列表、图层名称/数量、真实图片和精确视觉参数。

## Jarvis AX 最小复现记录

```text
app: Pixcake_mac
path: /Applications/pixcake-test.app
version: 9.3.0
page_graph: true
axbridge: false
pages: home, gallery, refine, creative
home aliases: 主页, 首页, 工作台, 主界面
gallery aliases: 图库, 图库页, 图库tab, 挑图页
refine aliases: 精修, 精修端, 精修页, 精修模块, 项目编辑页
creative aliases: 创意, 创意端, 创意页, 创意模块, 创意工作区
```

AX 仅用于验证当前版本路径与可见结构，不能覆盖官方手册对布局和功能范围的约束。
