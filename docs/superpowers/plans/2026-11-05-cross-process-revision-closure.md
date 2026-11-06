# 跨进程修订条件收口 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

日期：2026-11-05
状态：已完成
分支：`codex/desktop-official-layout`，基线 `5ff31dd`

**Goal:** 让过期修订的提交在跨句柄、跨进程时一律返回 `StateConflict`，不覆盖已写盘的较新文档，关闭第三轮审核的打回项 1（C-23、B-60、B-73）。

**Architecture:** 提交已经在 `WriteScope`（同路径 `flock` + 进程内递归锁）里执行。在锁内、写盘前重读正式文件的持久修订号，与调用方期望的修订号比较，不相等就拒绝。比较逻辑放在 `kernel/persist` 的一个通用函数里，`EditDocument` 和 `LayerDocument` 各自传入“从字节取修订号”的函数。`LayerDocument` 目前没有自己的修订号，本计划给它加一个持久记录。

**Tech Stack:** C++17、CMake/CTest、POSIX `flock`/`fork`（测试）。

**Spec:**
- `docs/superpowers/specs/review/2026-11-05-pixcake-seven-gate-review-r3.md`（打回项 1、本轮探针）
- `docs/superpowers/specs/review/2026-11-02-pixcake-contract-checklist.md` C-22、C-23、C-25
- `docs/superpowers/specs/review/2026-11-02-pixcake-boundary-checklist.md` B-60、B-73、B-116
- `docs/superpowers/specs/2026-11-02-pixcake-kernel-architecture-design.md` §6.14（五步替换）

## Global Constraints

- 修订号过期返回 `ResultCode::StateConflict`，不写文件、不改内存文档（C-23、B-73）。
- 同一份文档的修订号只增，不出现两个相同修订号（B-60）。
- 写盘仍走 `atomic_replace` 的五步替换；禁止先截断正式文件（B-111）。
- 精修文档字节布局按 B-116 不变（魔数 `PXED`、格式版本 1、记录 TLV、FNV-1a 64）；本计划不改精修文档格式。
- `kernel/` 不得引入 Qt/OpenGL；`arch_dependencies`、`capability_ownership` 必须继续通过。
- 测试名带清单编号（T-09）。

## Review Focus

1. 正式文件不存在时的首次提交（新建文档）必须成功，不能被当成冲突。
2. 正式文件损坏但 `.bak` 可用：按备份里的修订号比较，不能因损坏直接放行覆盖。
3. 被拒绝的过期句柄重新装载后，以新修订再提交应成功，修订号继续加一。
4. 旧图层文件没有新的修订记录：装载为修订 0，再保存后仍能被旧逻辑当作未知记录保留。
5. 写盘失败（磁盘满）后，内存修订号不前进，下一次提交仍以原修订比较。

---

## 文件

- Modify: `kernel/persist/include/pixcake/persist/atomic_file.hpp`、`kernel/persist/src/atomic_file.cpp` — 新增 `check_durable_revision`
- Modify: `kernel/editdoc/src/edit_document.cpp` — 三个 `commit_*` 与 `write_snapshot` 调用它
- Modify: `kernel/layerdoc/include/pixcake/layerdoc/layer_document.hpp`、`kernel/layerdoc/src/layer_document.cpp` — 文档修订号与带条件的 `save`
- Test: `tests/kernel/persist/test_atomic_file.cpp`、`tests/kernel/editdoc/test_edit_document.cpp`、`tests/kernel/layerdoc/test_layer_document.cpp`、`tests/kernel/command/test_command_gate.cpp`
- Create: `docs/superpowers/evidence/2026-11-05-cross-process-revision-closure.md`

### Task 1: 持久修订比较函数

**Interfaces:**
- Produces: `ResultCode check_durable_revision(FileSystem& files, const std::string& formal_path, std::uint64_t expected_revision, const std::function<std::optional<std::uint64_t>(const std::vector<std::uint8_t>&)>& revision_of);`
  - 用 `open_durable_file(files, formal_path, valid)` 读取，`valid = revision_of(bytes).has_value()`。
  - `absent` 或 `recovered_empty` → `Ok`（没有可比较的持久版本；以调用方内存为准）。
  - `code != Ok` → 原样返回该结果码。
  - `*revision_of(bytes) != expected_revision` → `StateConflict`；相等 → `Ok`。
  - 调用方必须已持有该路径的 `WriteScope`。

- [x] **Step 1: 写失败测试** `test_c23_check_durable_revision`（`test_atomic_file.cpp`，用 `pixcake::test::FakeFileSystem`，`revision_of` 取字节首 8 字节小端）：
  - 路径不存在 → `Ok`；
  - 正式文件修订 3、期望 3 → `Ok`；期望 2 → `StateConflict`；
  - 正式文件为坏字节、`.bak` 修订 5：期望 5 → `Ok`，期望 3 → `StateConflict`。
- [x] **Step 2:** `cmake --build <build> --target test_atomic_file` 编译失败（函数未声明）。
- [x] **Step 3:** 实现函数。
- [x] **Step 4:** `ctest --test-dir <build> -R test_atomic_file --output-on-failure` 通过。

### Task 2: 精修文档提交使用持久修订条件

**Interfaces:**
- Consumes: Task 1 `check_durable_revision`。
- 文件内部 helper：`std::optional<std::uint64_t> durable_document_revision(const std::vector<std::uint8_t>& bytes)` = `decode(bytes)` 成功时返回其 `revision()`。
- `commit_preview`、`commit_fragment_replacement`、`commit_baked_reset`：在现有 `expected_revision != revision()` 检查之后、构造 `next` 之前调用 `check_durable_revision(files, path, expected_revision, durable_document_revision)`，非 `Ok` 直接返回。
- `write_snapshot`：在 `WriteScope` 之后调用 `check_durable_revision(files, path, revision(), …)`。

- [x] **Step 1: 写失败测试**（`test_edit_document.cpp`，共用 `FakeFileSystem`，两个句柄 A/B 从同一路径在修订 0 装载）：
  - `test_c23_b73_stale_commit_preview_is_rejected`：A `commit_preview` 曝光 `1.00` → `Ok`；B 以 0 提交 `-1.00` → `StateConflict`；B 的 `revision()` 仍为 0；重新装载得到修订 1、曝光 `1.00`。
  - `test_c23_b73_stale_fragment_replacement_is_rejected`：A 用 `commit_preview` 前进到修订 1 后，B `commit_fragment_replacement(0, …)` → `StateConflict`，磁盘内容不变。
  - `test_c23_b73_stale_baked_reset_is_rejected`：同上，B `commit_baked_reset(0, …)` → `StateConflict`，磁盘仍有 A 的片段、`effects_baked()` 为假。
  - `test_c23_stale_write_snapshot_is_rejected`：A 前进到修订 1 后，B `write_snapshot` → `StateConflict`。
  - `test_b73_reloaded_handle_commits_next_revision`：B 被拒后重新 `load`，以 1 提交 → `Ok`，磁盘修订 2。
- [x] **Step 2:** 运行 `test_edit_document`，以上 4 个拒绝用例失败（实际返回 `Ok`）。
- [x] **Step 3:** 实现 Interfaces 中的改动。
- [x] **Step 4:** `ctest --test-dir <build> --output-on-failure` 全量通过（同步、预设、恢复测试也调用 `write_snapshot`，必须一起通过）。

### Task 3: 图层文档修订号与带条件保存

**Interfaces:**
- 新记录类型 `kLayerDocumentRevision = 9`，负载为 u64 小端；`encode` 总是写出；`decode` 缺失时为 0。
- 新增 `std::uint64_t LayerDocument::document_revision() const;`
- `save` 改为非 const：`ResultCode save(FileSystem& files, const std::string& path, const FastExportLatch* latch = nullptr);`
  - 在 `WriteScope` 内调用 `check_durable_revision(files, path, document_revision_, durable_layer_revision)`；
  - 写出修订 `document_revision_ + 1` 的编码；成功后才把 `document_revision_` 加一；失败时不变。
- `durable_layer_revision(bytes)` = `decode(bytes)` 成功时返回 `document_revision()`。

- [x] **Step 1: 写失败测试**（`test_layer_document.cpp`）：
  - `test_c23_b73_stale_layer_save_is_rejected`：A、B 从同一文件装载（修订 0）；A 改不透明度后 `save` → `Ok`，`document_revision() == 1`；B 改别的图层后 `save` → `StateConflict`；重新装载看到 A 的修改。
  - `test_b116_layer_file_without_revision_record_loads_as_zero`：手工构造不含记录 9 的旧格式字节（用现有 `encode` 后去掉记录 9），`decode` 成功且 `document_revision() == 0`。
  - `test_b93_failed_layer_save_keeps_revision`：注入 `DiskFull`，`save` 返回 `DiskFull`，`document_revision()` 不变。
- [x] **Step 2:** 编译失败（`document_revision` 未声明）。
- [x] **Step 3:** 实现。
- [x] **Step 4:** 全量 CTest 通过。

### Task 4: 跨进程验收与证据

- [x] **Step 1: 写失败测试** `test_b60_c23_cross_process_gate_rejects_stale_commit`（`test_command_gate.cpp`，仅 `__APPLE__ || __unix__`）：临时目录写入修订 0 的文档；`fork`；子进程装载并建 `CommandGate` 后经管道通知父进程，再等父进程信号；父进程装载、`preview`+`commit` 曝光 `2.00` 得到修订 1 后通知子进程；子进程 `preview`+`commit` `-2.00`，以“返回码是否为 `StateConflict`”作为退出码；父进程 `waitpid` 断言子进程退出码为 0，重新装载磁盘：修订 1、曝光 `2.00`。
  - 说明：Task 2 完成后该测试应直接通过；先在 Task 2 之前的基线上运行一次确认它会失败（`git stash` 后编译运行），记录到证据。
- [x] **Step 2:** 重新运行第三轮审核的两个探针（scratchpad `probe1`、`probe2`），期望 B 返回 `StateConflict`（码 12）、磁盘保留先提交的值。
- [x] **Step 3:** 全量构建、CTest、`git diff --check`。
- [x] **Step 4:** 写证据 `docs/superpowers/evidence/2026-11-05-cross-process-revision-closure.md`：改动、测试名、探针前后对比、仍未覆盖的项（极速导出门闩跨进程 B-62、单实例 B-67 不在本计划范围）。
- [x] **Step 5:** 勾选本计划、状态改为已完成，提交 `fix(kernel): reject stale-revision commits across handles and processes`。
