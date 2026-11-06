# 第七轮复审条件 1：图层蒙版密度 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

日期：2026-11-06
状态：已完成
分支：`codex/desktop-official-layout`，基线 `1f3ffa3`

**Goal:** 关闭第七轮条件 1（Q-06 蒙版密度）：取得来源，登记参数模式，`layerdoc` 的默认值、编辑、解码与渲染端检查都查参数模式。

**Architecture:** 来源是 test 版实测（`evidence/2026-11-06-layer-mask-density-measurement.md`），先写进架构附录 B“创意图层蒙版”与第 444 行的参数归属说明。参数模式 `mask/density`（0–100，步进 1，默认 100）、`mask/layer_feather`（0–1000，步进 1，默认 0）。`layerdoc` 新加、去掉蒙版和生成“可编辑蒙版的图层”时都取模式默认值；`set_mask_density`、`set_mask_feather` 与解码按模式校验；`render::compile` 的图层越界检查改查模式。

**Tech Stack:** C++17、CMake、CTest。

**Spec:** 第七轮记录“打回项与复审条件”第 1 条；需求第 728 行；架构第 444 行、附录 B。

## Global Constraints

- 上位文档先改，代码后改；没有测到的不写（精修 `mask/feather` 不重测、不改）。
- 每步先写失败测试；变异验证；干净工作树全量 CTest、`git diff --check` 后提交。
- 不提交另一会话的分割改动。

## Review Focus

1. 旧文件里羽化超过 100 的值不再被夹到 100（解码夹紧上限改为 1000）。
2. 旧文件里密度越界的图层记录：解码拒绝（与不透明度一致）。
3. 生成结果图层自带蒙版时也取模式默认值。

## Tasks

- [x] **Task 1**：test 版实测与证据、架构附录 B。
- [x] **Task 2**：参数模式与 `layerdoc`、渲染。测试 `test_q06_layer_mask_modes_match_measurement`、`test_q06_new_mask_takes_defaults_from_parameter_modes`、`test_q06_layer_mask_feather_at_mode_maximum_round_trips`、`test_q06_decode_rejects_density_outside_mode`、`test_q06_layer_mask_feather_above_100_renders`；`test_group_mask_selection_and_batch_design` 改为密度 101 拒绝、羽化 1000 接受。变异：模式默认值改 90 同时 `layerdoc` 写死 100（2 处失败）；解码去掉密度检查（1 处）；`set_mask_density` 只拒负数（2 处）；渲染查 `mask/feather`（1 处）。干净工作树 69/69。

## Rulings

- 图层蒙版羽化不再借用 `mask/feather`：实测 0–1000，而 `mask/feather` 是精修蒙版的刻度（0–100，步进待确认），两者是不同控件。新登记 `mask/layer_feather`。代价：如果精修蒙版羽化实测也是 0–1000，两条模式可以合并，届时改一处登记。
- 归属方用 `mask`（第七轮条件原文写 `mask/density`，与 `mask/selection_feather` 同一归属）。代价：只是命名。
- 解码遇到越界密度判为文件损坏，不夹紧：数值框实测对越界输入是拒绝而不是夹紧；羽化保留原有“超过上限夹到上限”的迁移，只把上限改为模式的 1000。
