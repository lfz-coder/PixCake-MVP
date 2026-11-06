# 七关审核第七轮复审计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

日期：2026-11-06
状态：已完成（结论：打回重写，见审核记录）
适用分支：`codex/desktop-official-layout`，基线 `03b8cf5`（上一轮 r6 基线 `30e0835`，记录提交 `4d44b77`）

**Goal:** 判定第六轮“打回项与复审条件”1–4 是否关闭，重跑两个否决探针，对 `30e0835..03b8cf5` 改动涉及的清单行增量重判，给出第七轮总结论。

**Architecture:** 只读审核，增量复审。构建与测试在 HEAD 的干净工作树里做（工作目录里有另一个会话未提交的分割改动，不计入本轮）。改动过证据文件的行重新取证；其余行沿用 r6。

**Tech Stack:** C++17、CMake、CTest、SQLite、`tests/arch/*.cmake`。

**Spec:** 审核总则与七份清单；`2026-11-06-pixcake-seven-gate-review-r6.md`；整改计划 `2026-11-06-r6-conditions.md`（含 Rulings）；架构 §7.2 禁用条件表、§7.5 生成工具编号表。

## Global Constraints

- 每行四种结论：通过 / 不通过 / 未实现 / 不适用；“通过”写到文件和测试名。
- 第 1 关命中否决项，总结论即“打回重写”。
- 不为结论变绿改产品代码、需求、架构或清单；发现的问题只记录。
- 证据不足记“不通过（证据不足）”。

## Review Focus

1. C-31：20 个能力是否各有目录与工厂、实现 7.2 全部操作；预设、同步、粘贴是否真的经过模块。
2. Q-06：`layerdoc` 里是否还有不来自参数模式的滑杆数值。
3. 状态接线：哪些修改文档的路径没有经过命令入口，因而不会记“已编辑”。
4. 图片条件：模块会按条件禁用，但渲染端有没有条件来源。
5. 与另一会话并行改动的隔离：本轮提交里不得带入对方未提交的文件。

---

- [x] Task 0：干净工作树全新构建、CTest、`git diff --check`；测试函数统计。
- [x] Task 1：两个否决探针按 HEAD 重编重跑。
- [x] Task 2：第六轮条件 1–4 逐条取证。
- [x] Task 3：增量重判与新发现。
- [x] Task 4：写记录、勾选、提交 `docs(review): seven-gate review round 7 on 03b8cf5`。
