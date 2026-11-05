# 精修衣物只读名称 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把页面图里的 13 个衣物名称放进精修侧栏，不放滑杆和数值。

**Architecture:** 只改 `apps/shell/main.cpp` 的精修侧栏。在已有调色名称组之后、`addStretch()` 之前增加一组禁用标签。可见名是页面图全名去掉共同前缀「衣物美化-衣服调整-」，无障碍名是页面图全名。

**Tech Stack:** C++17、Qt 6 Widgets、现有 `pixcake_app_shell` smoke、CTest。

## Global Constraints

- 名称以 `docs/superpowers/evidence/2026-11-05-refine-clothing-names.md` 的十三项为准，顺序不变。
- 分支保持 `codex/desktop-official-layout`。
- 不调用 `setValue`、`setRange`、`setChecked`。
- 不写范围、步进、默认值。
- 不加入衣服边缘修复、涂抹笔，以及表外的任何名称。
- 不改调色名称组、模块页签和主题。
- 登录、导入、渲染、AI、导出不在本计划。

---

### Task 1: 放入衣物只读名称

**Files:**
- Modify: `apps/shell/main.cpp` 的 `render_workspace()`

**Interfaces:**
- Consumes: 已有的 `modules`、`modules_layout`、`text()`。
- Produces: 对象名 `refine-clothing-labels`，无障碍名「衣服调整」。

- [x] **Step 1: 在调色名称组之后加入衣物名称组**

紧挨 `modules_layout->addWidget(color_labels);` 之后、`modules_layout->addStretch();` 之前加入：

```cpp
auto* clothing_labels = new QFrame(modules);
clothing_labels->setObjectName(QStringLiteral("refine-clothing-labels"));
clothing_labels->setAccessibleName(QStringLiteral("衣服调整"));
auto* clothing_layout = new QVBoxLayout(clothing_labels);
clothing_layout->setContentsMargins(0, 0, 0, 0);
const QStringList clothing_visible = {
    QStringLiteral("上衣祛褶皱"), QStringLiteral("上衣祛褶皱-祛粗褶皱"),
    QStringLiteral("上衣祛褶皱-祛细褶皱"), QStringLiteral("下装祛褶皱"),
    QStringLiteral("下装祛褶皱-祛粗褶皱"), QStringLiteral("下装祛褶皱-祛细褶皱"),
    QStringLiteral("衣服祛汗渍"), QStringLiteral("衣服祛瑕疵"),
    QStringLiteral("衣服祛褶皱"), QStringLiteral("衣服祛褶皱-祛粗褶皱"),
    QStringLiteral("衣服祛褶皱-祛细褶皱"), QStringLiteral("衣服边缘平整"),
    QStringLiteral("道具保护")};
const QStringList clothing_full = {
    QStringLiteral("衣物美化-衣服调整-上衣祛褶皱"),
    QStringLiteral("衣物美化-衣服调整-上衣祛褶皱-祛粗褶皱"),
    QStringLiteral("衣物美化-衣服调整-上衣祛褶皱-祛细褶皱"),
    QStringLiteral("衣物美化-衣服调整-下装祛褶皱"),
    QStringLiteral("衣物美化-衣服调整-下装祛褶皱-祛粗褶皱"),
    QStringLiteral("衣物美化-衣服调整-下装祛褶皱-祛细褶皱"),
    QStringLiteral("衣物美化-衣服调整-衣服祛汗渍"),
    QStringLiteral("衣物美化-衣服调整-衣服祛瑕疵"),
    QStringLiteral("衣物美化-衣服调整-衣服祛褶皱"),
    QStringLiteral("衣物美化-衣服调整-衣服祛褶皱-祛粗褶皱"),
    QStringLiteral("衣物美化-衣服调整-衣服祛褶皱-祛细褶皱"),
    QStringLiteral("衣物美化-衣服调整-衣服边缘平整"),
    QStringLiteral("衣物美化-衣服调整-道具保护")};
for (int i = 0; i < clothing_visible.size(); ++i) {
  auto* name = text(clothing_visible.at(i));
  name->setAccessibleName(clothing_full.at(i));
  name->setEnabled(false);
  clothing_layout->addWidget(name);
}
modules_layout->addWidget(clothing_labels);
```

两个列表必须保持 13 项且下标对齐。

- [x] **Step 2: 跑精修 smoke**

Run: `cmake --build build-desktop -j2 --target pixcake_app_shell && QT_QPA_PLATFORM=offscreen build-desktop/apps/shell/pixcake_app_shell.app/Contents/MacOS/pixcake_app_shell --page refine --smoke`

Expected: 退出码 0。

### Task 2: 验收并本地提交

**Files:**
- Modify: `docs/superpowers/evidence/2026-11-05-refine-clothing-names.md`

- [x] **Step 1: 跑完整验证**

```sh
ctest --test-dir build-desktop --output-on-failure
cmake --build build-desktop --target architecture_gate
git diff --check
```

`test_render_description` 若因工作区里未提交的 CMake 登记指向缺失源文件而未运行，记录该事实，不修改 `tests/CMakeLists.txt`，也不把它写成通过。其余已有测试和架构门禁必须通过。

- [x] **Step 2: 把验证结果补进读取记录**

写明 13 个可见名和无障碍名已经放进 `refine-clothing-labels`，没有数值。

- [x] **Step 3: 本地提交**

只提交 `apps/shell/main.cpp`、本计划、读取记录。提交说明：

```text
feat(desktop): show verified refine clothing labels
```

不推送，不创建 PR。
