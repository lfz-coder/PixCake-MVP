# 单图历史步骤 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 进程还在时，按已提交命令为每张图保留可回到的步骤；下标越界或旧修订号整次拒绝；重新打开后这张图只剩一条「上一次保存」。

**Architecture:** 步骤只活在 `kernel/history` 的内存里。每一步保存这次已提交命令的能力标识、参数标识、显示值和当时的文档快照。回到某一步只替换调用方手里的那份内存文档，不写文件。撤销链不进参数文档，也不进 SQLite。`command` 这一步不改。

**Tech Stack:** C++17、CMake、CTest。测试是独立可执行文件，不引入第三方测试框架。

## Global Constraints

- 进程活着时按已提交命令保留单图步骤。点击某一步回到那一步的已提交内容。关闭后再打开，该图此前步骤合并成一条「上一次保存」，不能再拆回中间步（需求第 8.3 节、第 3.5 节、C-54、B-101）。
- 历史模块不负责原子写盘。写盘内容仍是当前文档快照，不是整条撤销链。拖动中的预览不成为步骤（架构第 6.8 节、C-24）。
- 下标越界返回状态冲突，当前文档修订号和已有步骤都不改（B-29）。
- 新步骤的修订号必须大于这张图上一步。相同或更旧的修订号返回状态冲突，不追加，也不覆盖较新的步骤（B-60、B-73）。
- 批量设计进行中，记录和回到步骤都返回状态冲突，步骤和文档都不改（B-70、C-22）。这一步不实现配方模块，标志由调用方传入。
- 图片编号为空，或和文档上的图片编号不一致，返回状态冲突，不新建步骤。
- 快照解码失败返回文档损坏，内存文档保持调用前的字节。正常步骤来自 `encode()`，这条只防止坏快照把文档改掉。
- 历史不解释滑杆含义，不打开参数文件，不写 SQLite，不包含 Qt 头。
- 项目全局步骤这一步不做：架构写了有这一层，但没有写回到那一步时要恢复哪些图片。
- 回到旧步骤之后再提交，会不会删掉后面的步骤，文档没写。这一步不删除已有步骤。旧修订号按上面的规则拒绝追加。

本计划明确不做：项目全局撤销、命令入口接线、筛选、排序、导入解码、快捷键、壳层、预设、账本、导出队列、`kernel-cli`。T-12、T-16、T-17、T-20、T-21 保持未实现。

---

### Task 1: 已提交步骤可以回去，越界和旧修订号不行

**Files:**
- Create: `kernel/history/include/pixcake/history/history_log.hpp`
- Create: `kernel/history/src/history_log.cpp`
- Create: `kernel/history/CMakeLists.txt`
- Modify: `CMakeLists.txt`
- Create: `tests/kernel/history/test_history.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `EditDocument::create`、`commit_preview`、`encode`、`decode`、`image_id`、`revision`
- Produces: `HistoryStep`、`HistoryLog::record_committed`、`HistoryLog::select`、`HistoryLog::steps`、`HistoryLog::current_index`

- [x] **Step 1: Write the failing test**

`tests/kernel/history/test_history.cpp` 断言：

- 预览改了两次，只有提交之后的那一次被记成一步。一步里的显示值是提交值。
- 再提交一次后，回到第 0 步：内存文档修订号和已提交曝光回到第一步，预览不在。参数文件字节和写文件次数不变。再回到第 1 步，又是第二次提交的值。
- 另一张图的步骤不进这张图的列表。图片编号对不上时整次拒绝。
- 下标越界时，文档修订号、当前下标和三步的修订号都不变（B-29）。
- 批量设计进行中，记录和回到都不改步骤（B-70）。
- 把已经记过的旧修订号再记一次，返回状态冲突，不出现两个相同修订号（B-60、B-73）。

- [x] **Step 2: Run test to verify it fails**

Run: `cmake -S . -B build && cmake --build build --target test_history && ctest --test-dir build -R '^test_history$' --output-on-failure`

Expected: 编译失败，因为还没有 `HistoryLog`。实际先停在 CMake：找不到 `src/history_log.cpp`。

- [x] **Step 3: Write minimal implementation**

`record_committed` 在批量设计、空编号、编号不一致，或修订号不大于这张图最后一步时返回状态冲突。成功时把 `encode()` 追加为一步，当前下标指到新的最后一步。

`select` 在批量设计、空编号、没有这张图、下标越界或文档编号不一致时返回状态冲突。快照解不出时返回文档损坏。成功时用快照替换内存文档，当前下标改成这一步。两种失败都不改文档，也不改步骤。

- [x] **Step 4: Run test to verify it passes**

Expected: `test_history` 通过。

- [x] **Step 5: Commit**

```bash
git add kernel/history tests/kernel/history CMakeLists.txt tests/CMakeLists.txt docs/superpowers/plans/2026-11-03-kernel-history-steps.md
git commit -m "$(cat <<'EOF'
feat: keep one photo's committed history in memory

EOF
)"
```

### Task 2: 重新打开后只剩上一次保存

**Files:**
- Modify: `kernel/history/include/pixcake/history/history_log.hpp`
- Modify: `kernel/history/src/history_log.cpp`
- Modify: `tests/kernel/history/test_history.cpp`

**Interfaces:**
- Consumes: Task 1 的 `HistoryLog`
- Produces: `kHistoryFoldedLabel`、`HistoryLog::reopen_folded`

- [x] **Step 1: Write the failing test**

在同一测试文件追加：

- 一张图已经有两步。`reopen_folded` 用磁盘上最后一次提交的文档替换这张图的列表。列表只剩一步，标签是「上一次保存」，能力标识和显示值都空，快照仍是最后一次提交。下标 1 被拒绝。另一张图的步骤还在。
- 编号不一致时不替换已有列表。
- 折叠后再把同一修订号记一次，返回状态冲突，仍然只有那一条。

- [x] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_history && ctest --test-dir build -R '^test_history$' --output-on-failure`

Expected: 编译失败，因为还没有 `reopen_folded`。

- [x] **Step 3: Write minimal implementation**

`reopen_folded` 只替换这一张图的列表：一步，标签 `上一次保存`，快照是传入文档的 `encode()`。不写文件。编号不合时原列表不动。

- [x] **Step 4: Run test to verify it passes**

Expected: `test_history` 通过。再跑全量 `ctest --test-dir build --output-on-failure`。全量 13 项通过。

- [x] **Step 5: Commit**

```bash
git add kernel/history tests/kernel/history docs/superpowers/plans/2026-11-03-kernel-history-steps.md
git commit -m "$(cat <<'EOF'
feat: fold photo history to the last save on reopen

EOF
)"
```
