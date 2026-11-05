# 官方五页展示差异收口验收记录

日期：2026-11-05

计划：`docs/superpowers/plans/2026-11-05-official-display-gap-closure.md`

分支：`codex/desktop-official-layout`

## 本轮结果

- 主页搜索图标是输入框内部的前导动作（`QLineEdit::LeadingPosition`）。对象名 `home-search`。占位文字仍是「搜索所有项目」。
- 「常用功能」可见。其下有一块对象名为 `home-card` 的卡片，卡片里的唯一文案是「暂无内容」。
- 图库左上可见控件是「显示状态」和「全效果模式」，然后是「导入新图片」「筛选」「排序」，「批量删除」仍在右侧。三个显示项只留在无障碍名「显示状态：显示RAW格式、显示修图状态、显示导出状态」。
- 图库滑杆对象名 `library-grid-span`，范围 1 到 7，处于禁用，没有 `setValue`。因此 Qt 把滑块钳在最小值。这次钳制不是官方默认位置。
- 创意顶栏没有「首页」按钮。图库「首页」和精修「返回」仍在。
- 精修竖栏保留标题「功能区」，随后按顺序只有这些禁用按钮：AI色彩、调色、局部、人像、图像、衣物、宠物、后调色、裁剪、历史、联机。没有子行，也没有滑杆数值。

## 仍为延期

登录、导入、渲染、AI、导出，以及 F01–F13，仍然延期。

## 提交范围

`git diff --stat HEAD -- apps/shell/main.cpp` 的结果是 41 行插入、19 行删除。对照 `.superpowers/sdd/main.cpp.before-plan`，本计划 Task 1–6 只改了这些处：加入 `<QSlider>`；搜索图标从框外改成框内前导动作；显示「常用功能」并加上只有「暂无内容」的 `home-card`；图库左上收成「显示状态」等控件；加入禁用滑杆；去掉创意页「首页」；精修「功能区」换成上述 11 个禁用按钮。

同一份 `git diff HEAD -- apps/shell/main.cpp` 里，超出本计划、且在上述快照中已经存在的内容，是更早的未提交图标接线：`icon_action` 与页签图标；工作区工具按资源或「图标待补充」绘制；缩略条按钮改走 `icon_action`；登录页 `wechat.svg`、`merchant.svg`、`help.svg`；主页 `pixcake-logo.svg`、导航图标和三张功能卡片图标；主页工具条以及精修、图库、创意里原有按钮改走 `icon_action`；smoke 启动时检查 `uploadedShellAssets()`，缺资源则返回 2。这些接线没有从本次提交里拆出，也没有回退。

## 验证

工作目录为仓库根目录。

```text
ctest --test-dir build-desktop --output-on-failure
```

退出码 0。完整输出：

```text
Test project /Users/lifangzai/PixCake-MVP/build-desktop
      Start  1: test_shell_app_help
 1/29 Test  #1: test_shell_app_help ..............   Passed    0.16 sec
      Start  2: test_shell_app_smoke_login
 2/29 Test  #2: test_shell_app_smoke_login .......   Passed    0.22 sec
      Start  3: test_shell_app_smoke_home
 3/29 Test  #3: test_shell_app_smoke_home ........   Passed    0.19 sec
      Start  4: test_shell_app_smoke_library
 4/29 Test  #4: test_shell_app_smoke_library .....   Passed    0.10 sec
      Start  5: test_shell_app_smoke_refine
 5/29 Test  #5: test_shell_app_smoke_refine ......   Passed    0.10 sec
      Start  6: test_shell_app_smoke_creative
 6/29 Test  #6: test_shell_app_smoke_creative ....   Passed    0.10 sec
      Start  7: test_support
 7/29 Test  #7: test_support .....................   Passed    0.00 sec
      Start  8: test_atomic_file
 8/29 Test  #8: test_atomic_file .................   Passed    0.01 sec
      Start  9: test_edit_document
 9/29 Test  #9: test_edit_document ...............   Passed    0.01 sec
      Start 10: test_layer_document
10/29 Test #10: test_layer_document ..............   Passed    0.01 sec
      Start 11: test_workspace_display
11/29 Test #11: test_workspace_display ...........   Passed    0.00 sec
      Start 12: test_shell_display
12/29 Test #12: test_shell_display ...............   Passed    0.01 sec
      Start 13: test_guide_copy
13/29 Test #13: test_guide_copy ..................   Passed    0.00 sec
      Start 14: test_copy_committed_document
14/29 Test #14: test_copy_committed_document .....   Passed    0.03 sec
      Start 15: test_document_recovery
15/29 Test #15: test_document_recovery ...........   Passed    0.01 sec
      Start 16: test_sync
16/29 Test #16: test_sync ........................   Passed    0.01 sec
      Start 17: test_capability_registry
17/29 Test #17: test_capability_registry .........   Passed    0.00 sec
      Start 18: test_ledger
18/29 Test #18: test_ledger ......................   Passed    0.03 sec
      Start 19: test_export_queue
19/29 Test #19: test_export_queue ................   Passed    0.04 sec
      Start 20: test_preset_library
20/29 Test #20: test_preset_library ..............   Passed    0.01 sec
      Start 21: test_history
21/29 Test #21: test_history .....................   Passed    0.01 sec
      Start 22: test_command_gate
22/29 Test #22: test_command_gate ................   Passed    0.01 sec
      Start 23: test_project_paths
23/29 Test #23: test_project_paths ...............   Passed    0.01 sec
      Start 24: test_source_presence
24/29 Test #24: test_source_presence .............   Passed    0.01 sec
      Start 25: test_recycle_bin
25/29 Test #25: test_recycle_bin .................   Passed    0.04 sec
      Start 26: test_virtual_copy
26/29 Test #26: test_virtual_copy ................   Passed    0.10 sec
      Start 27: test_photo_marks
27/29 Test #27: test_photo_marks .................   Passed    0.04 sec
      Start 28: arch_dependencies
28/29 Test #28: arch_dependencies ................   Passed    0.21 sec
      Start 29: capability_ownership
29/29 Test #29: capability_ownership .............   Passed    0.01 sec

100% tests passed out of 29

Total Test time (real) =   1.53 sec
```

```text
cmake --build build-desktop --target architecture_gate
```

退出码 0。输出：

```text
-- arch dependencies ok
Built target architecture_gate
```

```text
git diff --check
```

该命令本身没有输出，退出码 0。
