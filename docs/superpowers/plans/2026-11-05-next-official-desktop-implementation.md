# PixCake 下一阶段官方桌面实现计划

日期：2026-11-05  
状态：已编写，待执行  
适用分支：`codex/desktop-official-layout`  
计划类型：Superpowers detailed implementation plan  
官方优先依据：<https://www.pixcakeai.com/guide/32445>

补充（2026-11-09）：本地项目页入口已按本计划的主页导航要求补齐，独立设计、过滤规则和验证项见 `docs/superpowers/plans/2026-11-09-local-projects-page.md`。本文中“五页”仍指原视觉壳层范围，不代表本地项目页当前不可用。

## 0. 计划结论与前置判断

本计划不是“从零开始重做”，也不把上一批局部完成写成项目完成。上一批结果按范围拆分如下：

| 范围 | 当前结论 | 证据 |
|---|---|---|
| `ui/shell` 五页展示契约、页面身份、区域模型和导航状态机 | 已完成（限定范围） | `docs/superpowers/plans/2026-11-04-desktop-shell-display-contract.md`、`tests/ui/test_shell_display.cpp` |
| Qt 6 Widgets 真实窗口、`--page`、五页只读壳层和 smoke | 已完成（限定范围） | `docs/superpowers/plans/2026-11-04-qt6-widgets-shell.md`、`apps/shell/`、5 个 `test_shell_app_smoke_*` |
| 五页页面骨架、空状态、Jarvis AX 可读性 | 已完成（限定范围） | `docs/superpowers/evidence/2026-11-04-official-layout-acceptance.md`、`docs/superpowers/evidence/2026-11-04-desktop-visual-acceptance.md` |
| 官方页面图级像素视觉复刻 | 未完成 | `docs/superpowers/plans/2026-11-04-official-pixel-visual-replica.md` 仍为“已编写，待执行”；当前实现仍是骨架和占位组件 |
| 内核审核中的 F01–F13、导入、渲染、生成、IPC、配置、kernel-cli、单实例、完整日志/指标 | 未完成或延期 | `docs/superpowers/specs/review/2026-11-04-pixcake-current-review.md` 与 `2026-11-04-pixcake-remediation-review.md` |
| 真实登录、短信、微信、商家账号、项目库业务导入、AI 生成、导出和云服务 | 未完成或明确延期 | 需求/架构的阶段边界与现有壳层禁用状态 |

因此，不能报告“前面所有计划已经完整实现”。本计划先完成上一份官方视觉计划的未完成部分，再建立下一阶段可实施的用户交互边界；不会把未确认业务写成已经完成。

## 1. 当前事实冻结

### 1.1 分支、代码和工作区

- 当前分支必须保持为 `codex/desktop-official-layout`。
- 本计划只允许写桌面壳层、视觉证据、测试和计划/验收文档；不得把内核分支的既有未提交修改重置、覆盖或混入。
- 当前工作区存在与本计划无关的未提交内核修改、构建目录和 `graphify-out/`。执行前记录 `git status --short`；每次提交只添加本计划涉及的路径。
- 当前 Qt 壳层入口为 `apps/shell/main.cpp`，样式集中在 `apps/shell/pixcake_theme.hpp`，展示契约在 `ui/shell`，内核不应引入 Qt。

### 1.2 官方手册证据

通过网页读取确认：官方页面 `https://www.pixcakeai.com/guide/32445` 当前抓取版本更新日期为 2026-09-17。页面正文明确：

- 登录：账号、6 位短信验证码、微信登录、商家账号登录、帮助。
- 主页：主页、AI 工具箱、本地项目、云空间、工作流、选片交付、回收站；照片导入；搜索；技术支持、个人中心、消息中心、传输列表、导出列表；常用功能、快捷功能、项目列表。
- 精修：返回/搜索/像素助手；图库/精修/创意切换；全局功能区；12 个工具条目；画布和查看对比；缩略图区；功能区。
- 图库：显示 RAW/修图/导出状态；全效果模式；最多六种缩略图标识；批量删除；AI 挑图/预设/元数据；导入/筛选/排序；网格、单视图、筛选视图；网格每行最多 7 张，最少可退到 1 张。
- 创意：手动工具栏；画布和图层；缩略图区；AI 工具箱；AI 工具、历史记录、批量设计、帮助、新手引导、反馈；图层面板。

与页面图关联的子手册也已核对：创建项目页更新于 2026-10-12，明确“照片导入/拖拽”和精修页导入的差异；AI 挑图页更新于 2026-06-03，明确入口、≥150 张提示、场景和检测规则；AI 工具箱页更新于 2026-09-18，明确工具包、搜索、订阅/点数和排队语义。这些内容只用于本计划的范围边界和后续能力分层，不授权当前壳层伪造业务结果。

### 1.3 官方视觉资产证据

已冻结到 `docs/superpowers/evidence/assets/`：

| 资源 | 尺寸 | 用途 |
|---|---:|---|
| `login.png` | 2048×1353 | 登录页面基准，含说明编号线 |
| `home.png` | 2160×1486 | 主页页面基准，含说明编号线 |
| `refine.png` | 2160×1486 | 精修页面基准，含说明编号线 |
| `library.png` | 2160×1486 | 图库页面基准，含说明编号线 |
| `preset-library.png` | 2048×1244 | 图库预设/缩略图状态补充基准 |
| `creative.png` | 2160×1486 | 创意页面基准，含说明编号线 |

官方页面中的图片链接本次仍由网页工具报告超时，不能作为本地可读取原图。现有附件是用户主动提供的官方页面截图，作为当前视觉比较的最高可用证据。黄色编号线属于说明层，运行时不得绘制。

### 1.4 Jarvis 事实

- 对照应用：`/Applications/pixcake-test.app`，Jarvis 识别为 `Pixcake_mac`，版本 `9.3.0`，页面图可用，AXBridge 不可用。
- 本地临时壳层：`/private/tmp/pixcake-creative.app`，Jarvis 已成功读取 `PixCake Desktop Shell`，窗口约 1386×832，创意页 AX 包含顶部切换、12 个手动工具、画布空状态、AI 工具箱/侧边栏/图层面板空状态和底部缩略区。
- Jarvis 读取到的对照应用精修页截图显示：深色窗口、顶部三段页面切换、左侧工具栏、黑色画布区域、右侧功能面板、底部缩略区。对照应用的真实照片和参数不能复制成产品数据；仅能作为 AX/结构和状态验证证据。
- 系统状态：Jarvis executor `0.1.586`，无更新阻塞；Accessibility 与 Screen Recording 可用。

## 2. 目标与完成定义

本计划的目标是完成“官方桌面视觉壳层第二阶段”，具体是：

1. 以已冻结的五页用户提供截图为视觉基准，完成共享 PixCake 主题、面板、按钮、输入框、工具栏、图标占位和空状态组件的统一化。
2. 完成登录页、主页、精修页、图库页、创意页的视觉对齐；所有未确认图片、业务数据、字体、图标和默认值保持空状态或显式待确认。
3. 为每页保存固定窗口截图、本地图、差异记录和未确认项；页面切换、禁用状态和 AX 文字可重复验证。
4. 让“当前实现是什么”和“官方图展示什么”可以逐项对照，避免把黄色说明线、示例照片、会员数据和截图中的项目记录当作运行时数据。
5. 继续保留真实登录、真实导入、照片解码、GPU/渲染、AI 生成、账本扣费、图层编辑、导出、云空间、工作流、选片交付和 F01–F13 的明确延期标记。

只有同时满足以下条件，才可把本计划标为完成：

- 五页均有可追溯的本地官方视觉基准；
- 共用视觉组件被五页实际复用；
- 五页大区比例、背景、面板层级、控件密度、空状态和 AX 文字均完成记录；
- 登录页具备官方截图要求的全部只读/禁用展示项；
- Jarvis 可读取本地五页窗口；
- CTest、架构门禁、视觉 smoke 和 `git diff --check` 通过；
- 验收文档明确列出仍延期的业务和 F01–F13；
- 任何未确认视觉值均标记为 `pending`，未用相似图片或猜测替代。

## 3. 明确不在本计划范围

以下内容不因为本计划完成而改变状态：

- 手机验证码发送、账号校验、微信 OAuth、商家账号认证、真实会话；
- 创建项目、文件夹/照片导入、拖拽导入、EXIF/RAW 解码、缩略图生成；
- 真实画布、GPU 预览、全分辨率导出、图层像素编辑；
- AI 挑图、AI 工具箱搜索/购买/点数扣除/排队/生成/取消；
- 预设写盘、历史持久化、筛选排序真实数据、回收站业务、云空间、工作流、选片交付；
- F01–F13、配置服务、IPC、renderd/exportd/generated、kernel-cli、单实例、完整本机日志/指标；
- 任何官方截图中出现但没有可追溯原始资源的照片、缩略图、Logo、图标、字体或会员数字。

## 4. 实施前门禁与工作记录

开始任何代码修改前，必须在本计划或配套证据文件记录：

```text
branch: codex/desktop-official-layout
head: <git rev-parse HEAD>
worktree: normal checkout; unrelated changes preserved
qt: <qtpaths --qt-version>
cmake: <cmake --version first line>
jarvis: executor 0.1.586; AX available; local shell readable
official-assets: local user-provided screenshots available; remote originals timeout
current-phase: 0
pending: exact colors, typography, icon assets, project/image business data
```

门禁规则：

- 如果分支不是 `codex/desktop-official-layout`，停止桌面代码修改，只更新事实记录。
- 如果用户没有提供新的官方原图，不再请求同一批资源；现有六张附件已足够进行当前视觉计划，远程原图阻塞单独记录。
- 如果某个视觉值只能通过猜测获得，使用临时 token 并写入 `pending`，不能写成最终设计常量。
- 每次运行 Jarvis 前先 `mac_list_apps`/`mac_use_app`，每个坐标操作前先重新读取 AX；默认只用 AX，不使用真实鼠标。

## 5. 任务分解

### Task 0 — 证据和基线冻结

**目标：** 把本计划执行所需的证据和可重复基线固定下来。

**修改：**

- 更新 `docs/superpowers/evidence/2026-11-04-official-visual-assets.md`，写明本地附件尺寸、用户提供来源、官方 URL 超时事实、黄色说明线不属于产品 UI。
- 新建 `docs/superpowers/evidence/2026-11-05-official-visual-baseline.md`，记录五页比较窗口、截图命名、运行命令、截图来源和 `pending` 字段。
- 不修改产品代码，不复制示例项目图片到 `apps/shell`。

**验证：**

```sh
git branch --show-current
git rev-parse HEAD
python3 - <<'PY'
from PIL import Image
from pathlib import Path
for p in sorted(Path('docs/superpowers/evidence/assets').glob('*.png')):
    print(p, Image.open(p).size)
PY
```

**完成证据：** 新证据文件存在；每张图尺寸与本地文件一致；远程超时和待确认项写清楚。

### Task 1 — 共享视觉系统整理

**目标：** 让五页使用同一套可追溯的 PixCake 视觉 token 和组件变体。

**修改范围：** `apps/shell/pixcake_theme.hpp`、必要时新增 `apps/shell/pixcake_widgets.hpp/.cpp`；不向 `ui/shell` 或 `kernel` 引入 Qt。

**步骤：**

1. 把已有背景、前景、边框、主色、禁用色、间距、圆角、按钮高度和面板层级收进命名 token；每个 token 在证据文件标注 `confirmed` 或 `pending`。
2. 抽出可复用的 `PixCakePanel`、`PixCakeButton`、`PixCakeField`、`PixCakeToolbar`、`PixCakeEmptyState` 或等价 Widgets 组件；页面只传入变体和文本。
3. 保留官方未确认图标的明确占位，禁止以 Emoji 或任意 Unicode 符号冒充最终官方图标；如果当前占位保留，验收表标记为 `pending-icon`。
4. 为统一组件设置稳定 `objectName`、`accessibleName` 和禁用状态，确保 Jarvis 仍可读取。
5. 删除五页中重复的相同 QSS 和间距硬编码；禁止借重构机会修改内核模型。

**验证：**

```sh
cmake --build build-desktop -j2
ctest --test-dir build-desktop -R 'test_shell_app_smoke|test_shell_display' --output-on-failure
cmake --build build-desktop --target architecture_gate
cmake -DSOURCE_DIR="$PWD" -P tests/arch/check_dependencies.cmake
cmake -DSOURCE_DIR="$PWD" -P tests/arch/check_capability_ownership.cmake
git diff --check
```

**完成证据：** `rg` 能证明颜色/圆角/控件状态的主定义集中；`ui/shell` 与 `kernel` 没有 Qt include；五页 smoke 仍通过。

### Task 2 — 登录页视觉对齐

**目标：** 按 `assets/login.png` 完成只读登录页的可见结构，不做真实登录。

**步骤：**

1. 固定窗口内容比例：黑色外层工作区、左侧人物裁剪区、右侧表单区；使用现有 `apps/shell/login-hero.png`，不重新生成相似人物图。
2. 按证据中可见顺序实现标题“手机验证码登录”、手机号输入框、6 位验证码输入框、获取验证码、禁用登录按钮、协议勾选行、`or` 分割线、微信入口、商家账号入口和右下帮助入口。
3. 协议行只展示“已阅读并同意像素蛋糕《用户使用协议》《隐私政策》”之类已在截图/手册中确认的文本；不勾选、不发送网络请求。
4. 登录方式和帮助按钮使用统一组件，禁用状态必须可读并可由 AX 识别。
5. 为账号、验证码字段设置 `accessibleName`、`maxLength=6` 和只读/禁用语义；不要把输入值保存到磁盘或日志。

**验证：**

```sh
cmake --build build-desktop -j2
QT_QPA_PLATFORM=offscreen build-desktop/apps/shell/pixcake_app_shell.app/Contents/MacOS/pixcake_app_shell --page login --smoke
```

Jarvis：启动本地 bundle，读取窗口标题、标题文本、两个输入框、验证码限制、协议行、微信/商家账号和帮助入口；确认登录/获取验证码没有网络或文件副作用。

**完成证据：** `docs/superpowers/evidence/2026-11-05-login-visual-acceptance.md`，包含官方图、本地截图、区域差异表和 AX 结果。

### Task 3 — 主页视觉对齐

**目标：** 对齐 `assets/home.png` 的大区比例和层级，同时保持无项目空状态。

**步骤：**

1. 左侧导航按官方正文顺序显示七项：主页、AI 工具箱、本地项目、云空间、工作流、选片交付、回收站；本地项目进入独立的本地项目列表页。
2. 顶部搜索和全局功能区显示技术支持、个人中心、消息中心、传输列表、导出列表；未知图标只用 `pending-icon` 占位。
3. 中央导入照片区域、常用功能、快捷功能和项目列表按官方截图层级排布；项目卡片区域保持“暂无项目”空状态，不伪造截图中的项目名、照片数量、云标识或会员内容。
4. 主页导航只允许已确认的壳层页面切换；本地项目进入本地项目列表页；AI 工具箱、云空间、工作流、选片交付如果没有业务实现，显示明确的后续阶段/空状态，不做无反馈死按钮。

**验证：**

- 固定窗口运行 `--page home`，保存本地截图。
- Jarvis 读取七项导航、搜索、导入区域、全局功能区、常用功能、快捷功能、项目列表和空状态。
- 检查没有创建项目、写入数据库、读取用户文件或网络调用。

**完成证据：** `docs/superpowers/evidence/2026-11-05-home-visual-acceptance.md`。

### Task 4 — 精修页视觉对齐

**目标：** 对齐 `assets/refine.png` 和官方手册精修页的工具、画布、侧栏和底部区。

**步骤：**

1. 顶部固定返回主页面、搜索、像素助手和图库/精修/创意切换；全局功能区显示技术支持、个人中心、消息中心、传输列表、导出列表、导出。
2. 工具条以官方手册顺序展示：显示比例调整、抓手、祛边、参考线、液化、污点修复、修补、仿制图章、智能消除、AI 点修、重置效果、预设。当前源码若保留 13 个文字条目，必须消除重复/说明性误差并在差异表中记录。
3. 中央画布使用空预览占位，右下角保留禁用“查看对比”；不加载照片、不调用 GPU。
4. 底部缩略图区展示 `+`、筛选、排序、去挑图、星标、旗标、色标、单视图/对比视图/参考视图等已确认入口；无缩略图时显示空状态。
5. 右侧功能区显示“功能区/暂无照片”，不编造人像滑杆、数值或 AI 结果。

**验证：**

- Jarvis 读取顶部入口、工具条、画布、查看对比、功能区和底部入口。
- 页面切换至图库/创意后能返回，且不产生业务副作用。
- `--page refine --smoke` 与全量 CTest 通过。

**完成证据：** `docs/superpowers/evidence/2026-11-05-refine-visual-acceptance.md`。

### Task 5 — 图库页视觉对齐

**目标：** 对齐 `assets/library.png`、`assets/preset-library.png` 和手册的图库契约，保持无照片空状态。

**步骤：**

1. 显示状态区提供“显示 RAW 格式、显示修图状态、显示导出状态”三个可观察但无业务副作用的状态入口；若默认值没有官方证据，默认保持 pending，不擅自开启/关闭。
2. 提供“全效果模式”、导入新图片、筛选、排序和批量删除的禁用/空状态展示。
3. 中央区域使用空网格；不复制官方截图中的照片、编号、AI 精选标签、旗标、星标、色标或 X 关闭按钮作为运行时数据。
4. 右侧栏明确显示 AI 挑图、预设、元数据；补充 `preset-library.png` 只用于视觉比较，不加载成业务预设。
5. 底部显示网格视图、单视图、筛选视图。若实现滑杆，只允许手册确认的 1–7 行范围，并将默认值标记为未确认；当前空壳可继续禁用。

**验证：**

- Jarvis 读取显示状态、全效果模式、导入/筛选/排序、批量删除、侧栏和三种视图。
- 检查页面切换、空状态和禁用状态；不写项目库、不导入文件、不生成缩略图。
- 保存视觉差异表和 `pending` 列表。

**完成证据：** `docs/superpowers/evidence/2026-11-05-library-visual-acceptance.md`。

### Task 6 — 创意页视觉对齐

**目标：** 对齐 `assets/creative.png` 和官方创意页面正文，只展示结构和空状态。

**步骤：**

1. 手动工具栏按正文分组显示：显示比例调整、更改屏幕模式、抓手、移动图层、缩放工具、液化、污点修复画笔、修补工具、仿制图章、矩形/椭圆选区、套索/选区画笔、裁切。
2. 中央画布显示“暂无图片或图层”空状态；不接受拖拽、不导入图片、不生成图层。
3. 右侧区拆成 AI 工具箱、侧边栏和图层面板；侧边栏可见 AI 工具、历史记录、批量设计、帮助、新手引导、反馈的结构，但业务入口保持明确禁用/后续阶段。
4. 底部缩略图区复用精修组件，显示导入、筛选、排序的空状态，不伪造照片。
5. 统一顶部页面切换和全局功能区，保持 AX 文本稳定。

**验证：**

- Jarvis 读取所有手动工具、画布、AI 工具箱、侧边栏、图层面板和底部入口。
- 逐项检查没有网络、文件、GPU、AI、账本或导出副作用。
- 保存本地截图和差异表。

**完成证据：** `docs/superpowers/evidence/2026-11-05-creative-visual-acceptance.md`。

### Task 7 — 五页视觉比较与 AX 回归

**目标：** 把视觉和结构验收变成可重复的证据，而不是一次性口头判断。

**步骤：**

1. 为五页固定窗口尺寸（以本地实际窗口和官方截图内容区域记录为准），保存 `official-<page>.png`、`local-<page>.png` 和差异说明；官方截图中的编号线不进入本地产品截图。
2. 差异表按固定顺序检查：窗口边界、背景、大区域比例、图片裁切、面板位置、控件尺寸、文字层级、图标、空状态。
3. 为每页记录：通过项、pending 项、阻塞项、当前禁用项、业务副作用检查。
4. 用 Jarvis 重新验证五页 AX；若窗口无法暴露，状态只能写“视觉代码完成，Jarvis 验收阻塞”。
5. 更新 `docs/superpowers/evidence/2026-11-04-desktop-visual-acceptance.md` 或新增总验收文件，禁止把未确认项写成通过。

### Task 8 — 全量验证与本地提交

**目标：** 完成计划级自动化验证，并只提交本计划文件。

```sh
cmake -S . -B build-desktop \
  -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qtbase \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build-desktop -j2
ctest --test-dir build-desktop --output-on-failure
cmake --build build-desktop --target architecture_gate
cmake -DSOURCE_DIR="$PWD" -P tests/arch/check_dependencies.cmake
cmake -DSOURCE_DIR="$PWD" -P tests/arch/check_capability_ownership.cmake
git diff --check
```

提交前检查：

- `git status --short` 中无关内核修改仍保持原样；
- `git diff --cached --name-only` 只包含本计划涉及的桌面代码、证据、验收和计划文件；
- 提交信息建议：`feat(desktop): continue official visual shell alignment`；
- 本地提交即可，不推送、不创建 PR。

## 6. 失败与阻塞处理

- 官方远程图片仍超时：继续使用用户已提供且已冻结的本地官方截图；在证据中保留远程阻塞，不自行下载相似图。
- 缺少独立 Logo、微信图标、帮助图标或字体：保持 `pending-icon`/`pending-font` 占位，并在验收报告中列出，不把 Unicode 或系统图标包装成最终官方资源。
- Jarvis 无法读取某页：先确认 app 路径、签名和窗口暴露；仍无法读取时停止该项，并把该页标为“Jarvis 验收阻塞”。
- CTest/架构门禁失败：先定位根因并修复；不得删测试、放宽门禁、修改需求/架构文档来迁就实现。
- 发现当前页面需要真实业务才能复刻某个区域：只实现可证实的外壳和空状态，并把业务功能列入延期清单。

## 7. 计划完成后的报告格式

最终报告必须包含：

1. 本计划是否完成，以及完成的是哪一限定范围；
2. 五页逐页成果和对应证据文件；
3. 测试命令及结果；
4. Jarvis 读取到的页面、窗口标题、关键 AX 文本和未读到的控件；
5. 仍为 `pending` 的视觉值和未取得的官方独立资源；
6. F01–F13、真实登录、导入、渲染、AI、导出、云/工作流/选片交付仍延期的事实；
7. 本地提交 hash；
8. 下一步只提出有证据的候选计划，不把猜测写成需求。

## 8. 下一份计划的候选入口（仅候选，不在本计划执行）

当前视觉壳层完成后，下一份计划必须从用户明确选择且证据充分的方向开始：

- 方向 A：导入与项目创建的真实本地路径（需先按创建项目手册固定文件选择、文件夹命名、异常和索引契约）；
- 方向 B：精修参数模式与只读面板生成（需先逐页用 Jarvis 读取 9.3.0 控件、滑杆范围、默认值和 Free 规则）；
- 方向 C：渲染预览进程与 IPC（需先冻结架构消息、进程生命周期、取消和帧格式）；
- 方向 D：导出队列端到端接线（需先完成 F01–F07 复审和文件身份/恢复测试）。

在没有新证据之前，不得擅自选择 A–D 之一并假设用户要真实业务、GPU 或网络能力。
