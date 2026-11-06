# 七关审核第六轮复审计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

日期：2026-11-06
状态：已完成（结论：打回重写，见审核记录）
适用分支：`codex/desktop-official-layout`，基线 `30e0835`（上一轮 r5 基线 `4eb78fc`，记录提交 `2ef3fd1`）

**Goal:** 判定第五轮“打回项与复审条件”1–3（C-31、Q-06、P-12、P-23）是否关闭，重跑两个否决探针，对 `4eb78fc..30e0835` 改动涉及的清单行增量重判，给出第六轮总结论。

**Architecture:** 只读审核，增量复审。改动过证据文件的行重新取证；其余行沿用 r5，记录中写“沿用 r5”。探针放 scratchpad，不提交。

**Tech Stack:** C++17、CMake、CTest、SQLite、`tests/arch/*.cmake`。

**Spec:**
- `docs/superpowers/specs/review/2026-11-02-pixcake-review-index.md` 与七份清单
- 上一轮：`docs/superpowers/specs/review/2026-11-06-pixcake-seven-gate-review-r5.md`
- 本轮整改计划：`docs/superpowers/plans/2026-11-06-r5-conditions-1-3.md`（含 Rulings）

## Global Constraints

- 每行四种结论：通过 / 不通过 / 未实现 / 不适用；“通过”写到文件和测试名。
- 第 1 关命中否决项，总结论即“打回重写”。
- 不为结论变绿改产品代码、需求、架构或清单；发现的问题只记录。
- 证据不足记“不通过（证据不足）”。
- 增量复审的边界写进记录：沿用的行注明“沿用 r5”。

## Review Focus

1. C-31 的判定口径：清单要求“每个可单独勾选、禁用、跳转或排序的精修能力是独立模块”。只有两个能力有独立目录时，不能判通过。
2. P-12：筛选排序是否装载参数正文；修图状态、导出状态是否真有写入方。
3. P-23：增量快照与重开全量折叠是否一致；拷贝、移动后快照是否跟随。
4. Q-06：`layerdoc` 是否还有写死的范围常数。
5. 行为改动的回归：`editdoc` 禁用判定改为预览优先；颗粒子项在效果为 0 时被拒绝；壳层颗粒滑杆。

---

### Task 0: 基线

- [x] 全新目录构建、CTest、`git diff --check`；统计测试函数与带编号比例。

### Task 1: 否决探针

- [x] 按 HEAD 重编 `probe_cross_handle_commit`、`probe_cross_process_gate`（×20）。Expected：过期提交状态冲突，非法值拒绝，20/20 一方成功。

### Task 2: 条件 1–3 逐条取证

- [x] C-31、Q-06、P-12、P-23：读代码、测试与变异记录，逐条判定。

### Task 3: 增量重判与新发现

- [x] `4eb78fc..30e0835` 改动涉及的其他行（B-33、C-05、C-28、C-30、S 类 SQL 行、架构门禁）重判；其余沿用 r5。

### Task 4: 记录与结论

- [x] 写 `specs/review/2026-11-06-pixcake-seven-gate-review-r6.md`，勾选本计划，提交 `docs(review): seven-gate review round 6 on 30e0835`。
