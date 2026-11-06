# Free 标签范围修订计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

日期：2026-11-05
状态：已完成
分支：`codex/desktop-official-layout`

**Goal:** 按用户 2026-11-05 的裁决，把“带 Free 的分组本阶段不实现”从架构文档和现行审核清单中移除，改写成正常的功能合同。

**Architecture:** 只改文档。需求文档（`2026-11-02-pixcake-desktop-phase1-design.md`）本来没有 Free 概念，不改。架构文档和 6 份现行清单逐条改写；清单行不删除，只改行内容。历史审核记录（11-03、11-04 两轮）不改；第三轮记录追加“后续裁决”一节。

**Spec:** 用户裁决原文：“这里的 Free 只是一个标签，是这个软件后续提供免费试用还是使用会消耗响应的额度，但是我们所要实现的都是可以直接使用的……不需要去管 free 这个标签，默认就是没有的。”需求依据：需求文档 §10.3（第 415–458 行）、§23（第 978–982 行）、§24 第 9 条（第 1042 行）。

## Global Constraints

- Free 只是试用/额度标记，不构成排除；原 Free 分组的行为以需求 §10.3 为准，快捷键以需求 §23 为准。
- 不猜范围：这些分组的显示范围、步进、默认值必须来自需求原文或在像素蛋糕-test 上真实拖到两端的读数；没有来源之前不写进参数模式或代码（与架构文首、附录 B 的测量规则一致）。
- 审核清单不许删行；改写后的行仍保留原编号和严重程度。
- 计费里的“免点队列/限时免费/永久免费”（`FreeQueuePolicy`、`AccelerationCharge::*Free`）与本裁决无关，不动。

## Review Focus

1. 改写后不能留下自相矛盾的句子（例如某处仍写“本阶段不注册绘制”）。
2. 不能为原 Free 分组凭空补出需求里没有的范围或默认值。
3. 原来依赖“Free 排除”而判“不适用”的行（如 B 透视引导线）要改成可审的合同。

---

### Task 1: 架构文档

**Files:** `docs/superpowers/specs/2026-11-02-pixcake-kernel-architecture-design.md`

- [x] 第 11 行：删去 Free 例外，改为“Free 标记不构成排除”的说明并注明裁决日期。
- [x] 第 198 行：曲线和色轮属于本阶段要做的专用交互，进入 `ui/editors`。
- [x] 第 413、429、443、444 行：`detail` 及其 `geometry`、`denoise` 阶段改为本阶段实现；参数模式与绘制在范围实测后注册。
- [x] 第 698 行（§17）：删去 Free 排除条目，改为“Free 标记不是排除依据”。
- [x] 附录 B 第 708、752、766–768、785–787 行：改写为“标记只作记录；这些分组要实现，范围待实测”。
- [x] 验证：`grep -n 'Free' <架构文档>`，剩余提及只用于描述画面上的标记，不再含“不实现/不注册/不做”。

### Task 2: 现行审核清单

**Files:** `specs/review/2026-11-02-pixcake-{review-index,contract-checklist,boundary-checklist,quality-checklist,test-checklist}.md`

- [x] 总则第 11 行：删去 Free 例外，保留架构 §17 的排除。
- [x] 契约 C-02、C-05、C-06、C-07、第 28 行段落、第 99 行、第 158 行：改写为正常合同（C-05 按需求 §10.3 和 §24 第 9 条：黑白档禁用鲜艳度、饱和度，并把 HSL 换成黑白组；C-06 按需求 §10.3：AI 校准同时做白平衡和曝光，快捷键 `Cmd/Ctrl + U`；C-07 按需求 §23：镜头矫正 `Shift + Cmd/Ctrl + R`，曲线通道 `Option/Alt + 1–6` 和方向键取点不可自定义）。
- [x] 边界第 57 行：透视引导线最多 4 条（需求 §10.3），超出时拒绝这一次、不删已有引导线（本规范补齐）。
- [x] 质量 Q-17、Q-33：改为“不认识的旧片段保留并跳过”和“以 Free 为由不实现，或无来源写死范围”为否决。
- [x] 测试清单第 79 行：改为原 Free 分组按已实现能力要求测试。
- [x] 验证：`grep -rn 'Free' specs/review/2026-11-02-*.md` 无排除语义残留。

### Task 3: 第三轮审核记录追加裁决，提交

- [x] 在 `specs/review/2026-11-05-pixcake-seven-gate-review-r3.md` 末尾追加“后续裁决（2026-11-05）”：C-02 的“为 Free 分组建界面”结论作废；这些控件留下的问题只剩写死范围（C-38、Q-06/Q-20），由第 2 项处理；C-05/C-33 中“黑白档、精准控制”的缺口仍然存在。
- [x] 更新记忆 `free-label-in-scope.md`：上位文档已修订。
- [x] `git diff --check`；提交 `docs: treat the Free label as a billing marker, not a scope exclusion`。
