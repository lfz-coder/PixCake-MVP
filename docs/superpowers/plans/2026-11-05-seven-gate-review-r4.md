# 七关审核第四轮复审计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

日期：2026-11-05
状态：已完成（结论：打回重写，见审核记录）
适用分支：`codex/desktop-official-layout`，基线 `7eaa48a`（上一轮 r3 基线 `1de349e`）

**Goal:** 判定第三轮复审“打回项与复审条件”的 7 条是否关闭，重跑 r3 的两个否决探针，并给出第四轮总结论。

**Architecture:** 只读审核，增量复审。`1de349e..HEAD` 改动过证据文件的清单行重新核对；证据文件未改动的行沿用 r3 结论，并在记录中写明“沿用 r3”。r3 的否决项与 7 条复审条件逐条重新取证。探针放 scratchpad，不提交。

**Tech Stack:** C++17、CMake、CTest、SQLite、Qt 6（壳层）、`tests/arch/*.cmake`。

**Spec:**
- `docs/superpowers/specs/review/2026-11-02-pixcake-review-index.md`（总则）与七份清单
- 上一轮：`docs/superpowers/specs/review/2026-11-05-pixcake-seven-gate-review-r3.md`（含“后续裁决”：Free 只是计费标记）
- 上位文档：需求、架构、依赖规范；`docs/superpowers/specs/2026-11-06-pixel-processing-provenance.md`

## Global Constraints

- 每行四种结论：通过 / 不通过 / 未实现 / 不适用；“通过”写到文件和测试名。
- 第 1 关命中否决项，总结论即“打回重写”。
- 不为结论变绿改产品代码、需求、架构或清单；发现的问题只记录。
- 证据不足记“不通过（证据不足）”。
- 页面 smoke 不得作为导入、渲染、GPU、日志/指标等的实现证据。
- 增量复审的边界写进记录：沿用的行注明“沿用 r3”。

## Review Focus

1. 跨句柄、跨进程以过期修订提交（r3 两个探针）：必须返回 `StateConflict`，磁盘保留较新内容。
2. 壳层滑杆范围：控件源码不得再写上下限；每个有范围的滑杆都来自参数模式。
3. 参数模式：步进错误、未声明参数、黑白档下的鲜艳度/饱和度/HSL；导出 `quality` 重复或缺失。
4. `add_photo` 旁路：未经准入的路径不能进项目库。
5. 新增能力 `grain` 与契约 §4 标识表：未经上位文档登记的新能力标识按 Q-02/C-31 判定。

---

## 文件

- Create: `docs/superpowers/specs/review/2026-11-05-pixcake-seven-gate-review-r4.md`
- Modify: 本计划（勾选）
- Scratch（不提交）：`$SCRATCH/probes/`

### Task 0: 基线

- [x] 记录 HEAD、`git status`；全量构建、CTest、`git diff --check`；统计测试函数与带编号比例。

### Task 1: 否决项与探针

- [x] 重跑 `probe_cross_handle_commit`、`probe_cross_process_gate`（参数段含曝光 `0.03`、对比度 `1.5`、未声明参数）。Expected：过期提交 `StateConflict`，非法值拒绝。
- [x] 核对壳层 `apps/shell/main.cpp` 无范围常数；冒烟检查 95 个滑杆来自参数模式。

### Task 2: 7 条复审条件逐条取证

- [x] 1 跨进程修订；2 C-02 裁决；3 壳层参数来源；4 参数模式补全（禁用原因、黑白档、B-32、§4 外标识）；5 导入与依赖；6 可观测性；7 单实例与跨进程极速导出。

### Task 3: 七关增量复审

- [x] 对每关列出改动文件涉及的行并重判；其余沿用 r3。新增能力 `grain`、`color_editor_*`、GPU 状态升级一并判定。

### Task 4: 记录与结论

- [x] 写打回项关闭对照表、本轮新发现、总结论；勾选本计划；提交 `docs(review): seven-gate review round 4 on 7eaa48a`。
