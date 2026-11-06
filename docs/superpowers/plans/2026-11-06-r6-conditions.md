# 第六轮复审条件 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

日期：2026-11-06
状态：执行中
分支：`codex/desktop-official-layout`，基线 `4d44b77`

**Goal:** 关闭第六轮复审条件 1–4（Q-06 收尾、C-31 续做、状态接线、账本移动语义），然后跑第七轮复审。

**Architecture:**
- 每个能力一个目录 `kernel/capabilities/<能力>/`，目录里一个工厂 `make_<能力>_module`，写明这个能力的声明：需要检测、需要素材、可能坏片、只对纯色背景生效的参数、跳到创意的生成入口。声明是数据（`ModuleTraits`），由 `ParameterBackedModule` 统一执行；只有行为不同的能力派生（`detail` 按阶段编译）。`module_set` 用一张“标识 → 工厂”表组装，表外的能力没有模块。
- 读取接口 `ParameterReader` 增加 `conditions()`（图片条件），编译据此跳过不可用的能力和非纯色背景下的纯色参数。
- 预设、批量同步、粘贴经 `transfer_fragment`（模块的迁移 + 提取 + 替换写回）写到目标。
- 修图状态：`CommandGate` 提交成功后通知 `CommitListener`；`PhotoEditRecorder` 把它记到项目库。导出状态：`record_export_statuses` 把导出队列里成品存在的任务投影到项目库（性能清单第 55 行：导出状态归导出事件）。

**Spec:** 第六轮记录“打回项与复审条件”；架构 §6.2、§6.7 第 271 行、§7.2、§7.3、§7.5、第 154、196、369、622–623 行；需求第 525、584、590、594、610、640、767、905 行；性能清单第 55 行。

## Global Constraints

- 上位文档先改：跳转工具编号与各能力的禁用条件写进架构 §7.2、§7.5，并注明需求行号。
- 没有来源的禁用条件不写（例如宠物是否需要检测）。
- 能力目录只链接 `pixcake_capability`（规则 4），互不包含（规则 2）。
- 每步先写失败测试；全量 CTest、`git diff --check`、架构门禁通过后提交。

## Review Focus

1. 预设与同步改走模块后，目标片段里的未知字段保留；来源片段过新时原样复制；目标片段过新时不动。
2. 只有 `portrait.body` 会因坏片不可用；人像五个能力在检测未完成时不画。
3. 非纯色背景下光影均匀不画、不报错（需求第 905 行）。
4. 提交失败时不记“已编辑”；状态记录失败不改变提交结果，但写日志。
5. 被移走的账本对象仍可安全查询。

## Tasks

- [x] **Task 1**：Q-06 收尾（`layer_document.cpp:711`）+ 账本移动语义。测试 `test_p23_moved_from_ledger_is_empty`；Q-06 用变异验证（把 `mask/selection_feather` 上限改成 900，解码 1000 的用例应失败）。
- [ ] **Task 2**：模块声明与图片条件。测试 `test_c31_only_body_reports_broken_image`、`test_c31_portrait_needs_detection`、`test_c31_sky_needs_assets`、`test_c31_solid_background_parameter_has_no_effect_on_other_backgrounds`、`test_c31_generation_entries_compile_to_jumps`。
- [ ] **Task 3**：20 个能力目录与工厂表。测试 `test_c31_every_capability_has_its_own_directory`；门禁对能力目录的链接检查改到工厂函数。
- [ ] **Task 4**：预设、同步、粘贴经模块写回。测试 `test_c31_transfer_keeps_target_unknown_fields`、`test_c31_transfer_copies_too_new_source`、`test_c31_transfer_leaves_too_new_target`，以及预设、同步模块里的对应集成测试。
- [ ] **Task 5**：状态接线。测试 `test_p12_commit_marks_photo_edited`、`test_p12_failed_commit_does_not_mark_edited`、`test_p12_exported_products_mark_photos_exported`。
- [ ] **Task 6**：第七轮复审。

## Rulings

（执行中记录）
