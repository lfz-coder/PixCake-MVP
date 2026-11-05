# 官方五页展示差异收口 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 按官网「工作区页面介绍」和已冻结页面图，收口当前五页壳层里已经能证明的结构差异。

**Architecture:** 只改 `apps/shell` 的页面组合。展示契约仍在 `ui/shell`，本计划不新增业务区域。颜色、字号、间距没有新的测量值，继续用现有 token，不把截图取样写成最终设计值。

**Tech Stack:** C++17、Qt 6 Widgets、现有 `pixcake_app_shell` smoke、CTest、架构门禁。

## Global Constraints

- 展示以 `https://www.pixcakeai.com/guide/32445` 这一页的正文和页面图为准；页面图上的黄色编号线不是产品界面。
- 分支保持 `codex/desktop-official-layout`。内核、导入、渲染、登录、AI、导出、云空间、工作流、选片交付不在本计划。
- 已放进 `apps/shell/assets/` 的 SVG 继续按文件名使用。本计划不新增图标文件，也不用 Unicode 补图标。
- 未在该页正文或已冻结页面图中读清的控件名称，不加入界面。
- 真实项目、照片、会员数字、滑杆当前值不写入运行时。
- 禁用控件保持禁用。本计划只改可见结构，不打开业务行为。

---

## 已证实的差异

这些差异同时来自 2026-11-05 打开的官网页面，以及 `docs/superpowers/evidence/assets/` 里的页面图。

| 位置 | 官网展示 | 当前壳层 |
| --- | --- | --- |
| 主页搜索 | `home.png` 上放大镜在输入框内部 | `main.cpp` 把 `search.svg` 放在输入框外侧 |
| 主页常用功能 | 正文第 5 项是「常用功能」 | `common_title->setVisible(false)`，标题被隐藏 |
| 图库左上 | `library.png` 是「显示状态」和「全效果模式」两个按钮 | 三个显示项、全效果、导入、筛选、排序排成同一行文字按钮 |
| 图库网格 | 正文写明滑杆范围是每行 1 到 7 张，网格视图是默认视图 | 没有滑杆 |
| 创意顶栏 | 「创意页面概述」和 `creative.png` 左上没有返回房子 | `render_creative()` 有「首页」按钮 |
| 精修右侧 | `refine.png` 右侧竖栏有模块名 | 只有「功能区」和「暂无照片」 |

主页正文的全局功能区没有单列「会员续费」，但 `home.png` 上有这个按钮。页面图保留它，不删除。

`refine.png` 竖栏里能和设计文档第 8.1 节、Jarvis 页面图 `Pixcake_mac` 9.3.0 对上的名称是：AI色彩、调色、局部、人像、图像、衣物、宠物、后调色、裁剪、历史、联机。图像描述里另有两个模糊字样，本计划不加入。人像面板里的「女 / 男 / 祛除瑕疵」等子行本计划不加入，避免把未逐字核对的参数区写进界面。

## 不在本计划

- 修改 `pixcake_theme.hpp` 里的颜色、字号、间距。
- 登录、获取验证码、查看对比、批量删除的业务。
- 筛选条件弹层、排序字段、星级旗标色标的实际标记。
- 精修滑杆、默认数值、性别年龄按钮。
- 云空间、工作流、选片交付、回收站页面。
- F01–F13 和内核写入边界。

## 文件

- 修改：`apps/shell/main.cpp`
- 不改：`ui/shell/src/shell_display.cpp`。图库三个显示项、创意没有首页，都已经和契约一致；本计划只改 Qt 呈现。
- 新增验收：`docs/superpowers/evidence/2026-11-05-display-gap-closure.md`

---

### Task 1: 主页搜索图标放进输入框

**Files:**
- Modify: `apps/shell/main.cpp` 的 `render_home()`

**Interfaces:**
- Consumes: 已存在的 `workspace-global/search.svg`，由 `loadAssetPixmap` 读取。
- Produces: 对象名仍为 `home-search`，无障碍名仍为「项目搜索」。

- [x] **Step 1: 去掉输入框外侧的图标**

删除 `search_wrap` 里单独的 `assetIcon("workspace-global/search.svg")`。搜索框直接放回顶部布局，占位文字仍是「搜索所有项目」。

- [x] **Step 2: 把图标设为输入框前导动作**

在创建 `search` 之后加入：

```cpp
const auto search_icon = pixcake::shell::loadAssetPixmap(QStringLiteral("workspace-global/search.svg"), 18);
if (!search_icon.isNull()) {
  search->addAction(QIcon(search_icon), QLineEdit::LeadingPosition);
}
```

`QLineEdit` 和 `QIcon` 已由现有头文件可见。图标缺失时不添加动作，也不画别的符号。

- [x] **Step 3: 编译并跑主页 smoke**

Run: `cmake --build build-desktop -j2 --target pixcake_app_shell && QT_QPA_PLATFORM=offscreen build-desktop/apps/shell/pixcake_app_shell.app/Contents/MacOS/pixcake_app_shell --page home --smoke`

Expected: 退出码 0。

### Task 2: 显示主页「常用功能」空状态

**Files:**
- Modify: `apps/shell/main.cpp` 的 `render_home()`

**Interfaces:**
- Consumes: 已有对象名 `home-section-title`。
- Produces: 可见标题「常用功能」，下面一块空状态，文案使用已经在用的「暂无内容」。

- [x] **Step 1: 取消隐藏**

删除 `common_title->setVisible(false)`。

- [x] **Step 2: 在标题下加空状态**

紧接 `main->addWidget(common_title)` 增加一个 `home-card` 框，里面只有居中的「暂无内容」。不要写入截图里的示例功能名。

- [x] **Step 3: 再跑主页 smoke**

Run: `QT_QPA_PLATFORM=offscreen build-desktop/apps/shell/pixcake_app_shell.app/Contents/MacOS/pixcake_app_shell --page home --smoke`

Expected: 退出码 0。

### Task 3: 图库左上收成两个按钮

**Files:**
- Modify: `apps/shell/main.cpp` 的 `render_library()`

**Interfaces:**
- Consumes: 契约区域 `library-status`、`library-effect`、`library-tools`、`library-delete`。
- Produces: 可见按钮「显示状态」「全效果模式」「导入新图片」「筛选」「排序」「批量删除」。三个具体显示项留在「显示状态」的无障碍名里。

- [x] **Step 1: 替换当前那一排七个并列按钮**

`render_library()` 里 `controls` 的循环现在包含「显示RAW格式」「显示修图状态」「显示导出状态」「全效果模式」「导入新图片」「筛选」「排序」。改成下面六个控件，全部禁用：

```cpp
auto* status = action(QStringLiteral("显示状态"), controls);
status->setEnabled(false);
status->setAccessibleName(QStringLiteral("显示状态：显示RAW格式、显示修图状态、显示导出状态"));
controls_layout->addWidget(status);
controls_layout->addWidget(icon_action(QStringLiteral("全效果模式"), controls));
controls_layout->addWidget(icon_action(QStringLiteral("导入新图片"), controls));
controls_layout->addWidget(icon_action(QStringLiteral("筛选"), controls));
controls_layout->addWidget(icon_action(QStringLiteral("排序"), controls));
```

「全效果模式」「导入新图片」没有对应 SVG，`icon_action` 找不到文件时保持纯文字。筛选和排序继续使用已有图标。批量删除按钮保留在这一排右侧，文案不变。

- [x] **Step 2: 不打开这三项，也不写默认开关**

不要调用 `setChecked`，不要保存 RAW、修图、导出的开关状态。官网正文只说明它们属于「显示状态」，本计划不判定当前是开还是关。

- [x] **Step 3: 跑图库 smoke**

Run: `QT_QPA_PLATFORM=offscreen build-desktop/apps/shell/pixcake_app_shell.app/Contents/MacOS/pixcake_app_shell --page library --smoke`

Expected: 退出码 0。

### Task 4: 图库网格滑杆只表达 1 到 7

**Files:**
- Modify: `apps/shell/main.cpp` 的 `render_library()`
- 需要在文件顶部确认已包含 `<QSlider>`。没有则加入。

**Interfaces:**
- Consumes: 契约里的「网格视图」。
- Produces: 对象名 `library-grid-span`，无障碍名「网格每行张数，范围 1 到 7，默认位置未确认」。

- [x] **Step 1: 在三个视图按钮左侧放禁用滑杆**

```cpp
auto* span = new QSlider(Qt::Horizontal, library_views);
span->setObjectName(QStringLiteral("library-grid-span"));
span->setAccessibleName(QStringLiteral("网格每行张数，范围 1 到 7，默认位置未确认"));
span->setRange(1, 7);
span->setEnabled(false);
span->setFixedWidth(120);
library_views_layout->insertWidget(0, span);
```

不要 `setValue`。不要在界面上写「当前每行 N 张」。Qt 可能把滑块钳在最小值，验收记录里写明这不是官方默认值。

- [x] **Step 2: 跑图库 smoke**

Run: `QT_QPA_PLATFORM=offscreen build-desktop/apps/shell/pixcake_app_shell.app/Contents/MacOS/pixcake_app_shell --page library --smoke`

Expected: 退出码 0。

### Task 5: 去掉创意页官方图上没有的「首页」

**Files:**
- Modify: `apps/shell/main.cpp` 的 `render_creative()`

**Interfaces:**
- Consumes: `Page::Creative` 的现有顶栏。
- Produces: 创意顶栏从页签「图库 / 精修 / 创意」开始。精修的「返回」和图库的「首页」不动。

- [x] **Step 1: 删除创意页首页按钮**

删除 `render_creative()` 里创建「首页」按钮、设置无障碍名、连接 `navigate(Page::Home)` 和 `addWidget(home)` 的那一段。后面的页签、AI工具、历史、批量、帮助、导出保持原样。

- [x] **Step 2: 跑创意 smoke**

Run: `QT_QPA_PLATFORM=offscreen build-desktop/apps/shell/pixcake_app_shell.app/Contents/MacOS/pixcake_app_shell --page creative --smoke`

Expected: 退出码 0。

### Task 6: 精修右侧只放已核对的模块名

**Files:**
- Modify: `apps/shell/main.cpp` 的 `render_workspace()`

**Interfaces:**
- Consumes: 区域名「功能区」。
- Produces: 11 个禁用按钮，无障碍名等于下列文字。没有数值、没有子行。

- [x] **Step 1: 用模块竖栏替换空文案**

在 `modules_layout` 中保留标题「功能区」。删除 `emptyState("暂无照片", ...)`。按这个顺序添加禁用按钮，不调用 `icon_action`，因为这些模块没有对应 SVG：

```cpp
const QStringList modules = {
    QStringLiteral("AI色彩"), QStringLiteral("调色"), QStringLiteral("局部"),
    QStringLiteral("人像"), QStringLiteral("图像"), QStringLiteral("衣物"),
    QStringLiteral("宠物"), QStringLiteral("后调色"), QStringLiteral("裁剪"),
    QStringLiteral("历史"), QStringLiteral("联机")};
for (const auto& label : modules) {
  auto* button = action(label, modules_frame);
  button->setEnabled(false);
  modules_layout->addWidget(button);
}
```

这里的父控件使用已经存在的 `modules` 框。不要加「收下巴」「自定义」，也不要加「女」「男」「祛除瑕疵」。

- [x] **Step 2: 跑精修 smoke**

Run: `QT_QPA_PLATFORM=offscreen build-desktop/apps/shell/pixcake_app_shell.app/Contents/MacOS/pixcake_app_shell --page refine --smoke`

Expected: 退出码 0。

### Task 7: 验收

**Files:**
- Create: `docs/superpowers/evidence/2026-11-05-display-gap-closure.md`

- [x] **Step 1: 跑完整验证**

```sh
ctest --test-dir build-desktop --output-on-failure
cmake --build build-desktop --target architecture_gate
git diff --check
```

Expected: CTest 全部通过，架构门禁通过，`git diff --check` 无输出。

- [x] **Step 2: 写验收记录**

记录这六项的结果：搜索图标在框内、常用功能可见且为空、图库左上两个主按钮、滑杆范围 1 到 7 且默认位置未确认、创意顶栏没有首页、精修竖栏只有上述 11 个名字。同时写明仍然延期的登录、导入、渲染、AI、导出和 F01–F13。

- [x] **Step 3: 本地提交**

只提交本计划改到的 `apps/shell/main.cpp`、本计划文件和验收记录。提交说明：

```text
fix(desktop): close evidenced official display gaps
```

不推送，不创建 PR。
