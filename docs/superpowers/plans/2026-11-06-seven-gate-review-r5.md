# 七关审核第五轮复审计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

日期：2026-11-06
状态：已完成（结论：打回重写，见审核记录）
适用分支：`codex/desktop-official-layout`，基线 `4eb78fc`（第四轮基线 `7eaa48a`）

**Goal:** 判定第四轮的否决项与 7 条复审条件是否关闭，重跑两个否决探针，给出第五轮总结论。

**Architecture:** 只读审核，增量复审。`7eaa48a..4eb78fc` 改动过证据文件的清单行重新取证；其余行沿用第四轮结论并注明。探针放 scratchpad，不提交。

**Spec:** `docs/superpowers/specs/review/2026-11-02-pixcake-review-index.md` 与七份清单；`2026-11-05-pixcake-seven-gate-review-r4.md`（含“后续修复”）。

## Global Constraints

- 四种结论；“通过”写到文件和测试名；证据不足记“不通过（证据不足）”。
- 任何否决行“不通过”即打回重写；第 1 关有否决项时总结论为打回重写。
- 不为结论变绿改产品代码、清单或上位文档。
- 沿用的行注明“沿用 r4”。

## Review Focus

1. 两个否决探针（跨句柄、跨进程过期提交）。
2. `detail` 描述与 `grain_*` 参数、同步树数量、预设归属。
3. 追色 0–200 与风格 0–100 在代码、需求、契约清单、边界清单四处一致。
4. 2 秒截止：换名之前判超时，失败时正式文件与修订号不变。
5. 门禁 rule 16 与 `third_party_locks_T22` 能抓到违规。

### Task 0: 基线（全新构建、CTest、`git diff --check`、测试统计）
- [x] 完成
### Task 1: 探针与第四轮否决项逐条取证
- [x] 完成
### Task 2: 七关增量复审
- [x] 完成
### Task 3: 记录、结论、提交 `docs(review): seven-gate review round 5 on 4eb78fc`
- [x] 完成
