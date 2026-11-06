# 七关审核第三轮复跑计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

日期：2026-11-05
状态：已完成（结论：打回重写，见审核记录）
适用分支：`codex/desktop-official-layout`，基线 `1de349e`

**Goal:** 按审核总则对当前 HEAD 逐关复审七份清单，判定 2026-11-04 复审的 7 条打回项是否关闭，并给出新的总结论。

**Architecture:** 只读审核。每关读清单原文 → 按行对照源码、测试和最近一次测试输出 → 必要时在 scratchpad 写跨句柄/故障探针 → 把结论写进同一份审核记录。审核不改产品代码、不改清单和上位文档；发现的问题只记录，修复另立计划。

**Tech Stack:** C++17、CMake、CTest、SQLite、Qt 6（壳层）、`tests/arch/*.cmake` 门禁。

**Spec:**
- `docs/superpowers/specs/review/2026-11-02-pixcake-review-index.md`（总则：四种结论、否决规则、审核数据量）
- 七份清单：`2026-11-02-pixcake-{contract,boundary,safelist,performance,quality,test,ops}-checklist.md`（safelist 文件名为 `2026-11-02-pixcake-safelist.md`）
- 上一轮：`docs/superpowers/specs/review/2026-11-04-pixcake-seven-gate-review-rerun.md`
- 上位文档：`specs/2026-11-02-pixcake-desktop-phase1-design.md`、`specs/2026-11-02-pixcake-kernel-architecture-design.md`、`specs/2026-11-06-pixcake-third-party-dependency-policy.md`

## Global Constraints

- 每行只有四种结论：通过 / 不通过 / 未实现 / 不适用；“通过”必须写到文件和测试名。
- 第 1 关命中否决项，总结论即为“打回重写”，后续关继续记录但不能改变结论。
- 禁止为让结论变绿而修改需求、架构、清单或产品代码。
- 不许删行；整模块缺失的行记“未实现”，不能记“通过”。
- 架构第 17 节排除项（Free 分组）若在代码里出现，改判“不通过”。
- 证据不足、无法从代码/测试/文档确认的行，记“不通过（证据不足）”，不猜测为通过。
- 页面 smoke 不得作为导入、配置、渲染、生成、IPC、kernel-cli、GPU、日志/指标、更新器、卸载器的实现证据。
- 探针代码只放在 scratchpad，不提交进仓库。

## Review Focus

1. 跨进程提交：两个进程/两个句柄同时写同一文档、历史、图层或导出任务时，旧修订不得覆盖新修订（打回项 1）。
2. 参数模式：步进错误值（如曝光 0.03、整数参数 1.5）、未声明参数、Free 分组参数必须被拒绝（打回项 2）。
3. 源文件删除：磁盘删除成功但索引事务失败后重启，项目状态必须可收敛、不丢原图事实（打回项 3）。
4. 导入验真：扩展名正确但内容损坏、尺寸超限的文件不得进入项目（打回项 4）。
5. 晚到事件：已取消/失败/旧修订的导出任务事件不能再发布成品（上一轮 B-53/B-73 反例）。

每条在对应 Task 中有探针或测试核对步骤。

---

## 文件

- Create: `docs/superpowers/specs/review/2026-11-05-pixcake-seven-gate-review-r3.md` — 本轮唯一审核记录
- Modify: `docs/superpowers/plans/2026-11-05-seven-gate-review-r3.md` — 勾选进度
- Scratch（不提交）：`$SCRATCH/probes/*.cpp`、`$SCRATCH/build-r3/`

记录格式沿用上一轮：每关一节，含“结论”行和 `| 结论 | 编号 | 证据与差异 |` 表；末尾“打回项关闭对照表”和“打回项与复审条件”。

### Task 0: 基线冻结

- [x] **Step 1:** 记录 `git rev-parse HEAD`、`git status --short`，确认分支为 `codex/desktop-official-layout`。
- [x] **Step 2:** 独立构建：`cmake -S . -B $SCRATCH/build-r3 -DCMAKE_BUILD_TYPE=Debug && cmake --build $SCRATCH/build-r3 --parallel 8`。Expected：构建成功。
- [x] **Step 3:** `ctest --test-dir $SCRATCH/build-r3 --output-on-failure`、架构门禁（`arch_dependencies`、`capability_ownership` 已在 CTest 内）、`git diff --check`。Expected：全部通过，记录用时和数量。
- [x] **Step 4:** 列出当前模块清单（`kernel/*`、`services/*`、`apps/*`、`gpu/*`、`storage/*`、`ui/*`）和测试函数总数、带清单编号的测试数，写入记录“验证命令”节。

### Task 1: 第 1 关 业务契约（C-01–C-62）

- [x] **Step 1:** 逐行读契约清单；对上一轮“不通过”的 C-10、C-21、C-23、C-25、C-26、C-35、C-36–C-39、C-61 优先复核。
- [x] **Step 2:** 打回项 1 核对：在 `kernel/editdoc`、`kernel/history`、`kernel/layerdoc`、`kernel/preset`、`kernel/exportq` 的公开头文件里列出所有写方法，逐个确认是否只能经 `CommandGate`/`WriteScope` 提交，且跨进程使用持久修订条件。
- [x] **Step 3:** 探针 `probe_cross_handle_commit.cpp`：两个独立句柄打开同一文档目录，A 提交 r+1 后 B 以 r 提交。Expected：B 被拒绝且正式文件仍为 A 的内容；否则 C-10/C-61 判不通过。
- [x] **Step 4:** 打回项 2 核对：确认 `ParameterMode` 是范围、步进、默认值、禁用原因的唯一来源；探针 `probe_parameter_mode.cpp` 提交曝光 `0.03`、整数参数 `1.5`、未声明参数、`hsl.*` 参数。Expected：全部拒绝；黑白配置文件下鲜艳度/饱和度不可编辑（C-05）。
- [x] **Step 5:** 对上一轮“未实现”行，按当前已存在的 `kernel/import`、`kernel/render`、`kernel/generate`、`kernel/ipc`、`services/config`、`services/account`、`apps/renderd`、`apps/exportd`、`gpu/*` 逐行重判；不存在的模块（如 `kernel-cli`）继续“未实现”。
- [x] **Step 6:** 写第 1 关小节，明确是否仍有否决项。

### Task 2: 第 2 关 边界条件（B-01–B-117）

- [x] **Step 1:** 逐行读边界清单；优先 B-27、B-32、B-33、B-44、B-52、B-60、B-62、B-67、B-73。
- [x] **Step 2:** 打回项 3 核对：读 `ProjectLibrary::delete_source_file` 现顺序和“删除待完成”状态；确认存在“磁盘删除后索引事务失败 → 重启 presence 扫描收敛”的测试（`tests/kernel/project/test_recycle_bin.cpp`），并读断言是否真正检查原图事实。
- [x] **Step 3:** 探针 `probe_export_late_event.cpp`：句柄 B 取消任务后，句柄 A 对同一任务 `commit_product`。Expected：拒绝且无成品；否则 B-53/B-73 判不通过。
- [x] **Step 4:** 检查自动保存 2 秒判定（B-52）、单实例锁（B-60/B-62）是否存在实现与测试。
- [x] **Step 5:** 写第 2 关小节。

### Task 3: 第 3 关 安全 SafeList（S-01–S-32）

- [x] **Step 1:** 逐行读清单；打回项 4 核对：动态 SQL 白名单保持（S-01）、导入内容/尺寸验真（S-07，`kernel/import` 的 source probe 与 admission）、路径身份（S-10/S-11）、单实例/会话（`services/account`）。
- [x] **Step 2:** 探针 `probe_import_forged.cpp`：扩展名 `.jpg` 但内容为文本；声明尺寸超上限。Expected：被拒绝并给出 `ImageCorrupt`/`UnsupportedFormat` 一类结果码。
- [x] **Step 3:** `rg -n 'sqlite3_exec|sqlite3_prepare' storage kernel services` 检查新增动态 SQL；`rg` 扫描密钥/令牌样式（S-16/S-17）。
- [x] **Step 4:** 写第 3 关小节。

### Task 4: 第 4 关 性能（P-01–P-25）

- [x] **Step 1:** 逐行读清单；打回项 5 核对：N+1 是否仍已消除（`SqliteLedgerLog::load`、`SqliteExportLog::load`）、`CreditLedger` 是否仍每次全量 fold（P-07）、缩略图生命周期、渲染/生成调度、配置缓存（`services/config`）、跨进程取消/超时（`kernel/ipc`）。
- [x] **Step 2:** 对照总则第 5 节数据量（2000 张、上百个滑杆中间值）判断预览合并（`kernel/preview` 最新值覆盖）是否成立。
- [x] **Step 3:** 写第 4 关小节。

### Task 5: 第 5 关 代码质量（Q-01–Q-33）

- [x] **Step 1:** 逐行读清单；重点 Q-01/Q-04 页面文字重复（`kernel/workspace`、`ui/shell`、`apps/shell/main.cpp`）、参数模式统一、`LogSink` 接入面、第三方依赖规范（Glad 等 `third_party` 条目的版本/许可证证据）。
- [x] **Step 2:** 写第 5 关小节。

### Task 6: 第 6 关 测试（T-01–T-11）

- [x] **Step 1:** 用 Task 0 的数据核对 T-01；统计带编号测试比例（T-09）；检查 F/B/C/O 故障组合测试镜像（打回项 7）。
- [x] **Step 2:** 写第 6 关小节。

### Task 7: 第 7 关 可观测与回滚（O-01–O-35）

- [x] **Step 1:** `rg -n 'LogSink' kernel storage services apps ui gpu` 列出调用方；检查导出、账本、恢复、生成是否有开始/完成事件与任务/文档标识（O-01/O-04/O-05）；运行时计数（O-06–O-12）；用户可见失败链路（O-13–O-20）。
- [x] **Step 2:** 写第 7 关小节。

### Task 8: 汇总与提交

- [x] **Step 1:** 写“打回项关闭对照表”：7 条逐条判 关闭 / 部分关闭 / 未关闭，附证据。
- [x] **Step 2:** 写总结论和新的“打回项与复审条件”，列出建议的修复计划入口（只列候选，不在本计划执行）。
- [x] **Step 3:** `git diff --check`；只 `git add` 审核记录和本计划文件；提交 `docs(review): seven-gate review round 3 on 1de349e`。
