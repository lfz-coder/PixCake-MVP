# 内核文档核心 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 做出不链接 Qt 的第一块内核：结果码、精修文档的读写、命令入口的修订号和极速导出门闩。

**Architecture:** `support` 只有结果码、校验和、日志接口和极速导出开关接口。`persist` 只做五步替换、隔离和备份恢复，不解释滑杆。`editdoc` 保存架构第 6.4 节的信封，片段内部不认识的字节原样保留。`command` 串行提交，自己不计算色彩、不写 SQL、不碰 OpenGL。本计划不建项目库、预设、账本、导出队列、GPU 和 Qt 壳。

**Tech Stack:** C++17、CMake、CTest。测试是独立可执行文件，不引入第三方测试框架。

## Global Constraints

- 用户能看见的行为以需求文档为准；目录、依赖以架构规范为准。
- 结果码使用契约清单 C-10 到 C-22，外加本规范补齐的 C-61「输入输出失败」。不得把这些码互相借用。
- 缺命令名、缺目标、提交时没有修订号，返回状态冲突，不写文档，不改修订号（B-01、C-22）。
- 修订号只增。预览副本不写盘，也不能被导出取走（C-24、Q-14）。
- 正式文件按架构第 6.14 节五步替换。失败时留下上一份完整文件，禁止先截断再写（C-25、B-111、Q-18）。
- 字节布局以边界清单 B-116 为准：小端、魔数 `PXED`、格式版本 1、FNV-1a 64 位校验和、`.tmp`、`.bak`、`isolate/`。
- 文件格式版本不是 1 时按文档损坏处理，不用「能力版本过新」拒绝整份文件（C-17）。
- 极速导出只看开关。队列变空不会关掉它，因为这个接口没有「队列已空」（C-03、B-62、Q-19）。
- 同一份文档的写入在命令入口里串行。后到的旧修订号返回状态冲突，不覆盖新内容（B-60、B-73、C-23）。
- 能力不在本轮的可读版本表里：跳过并保留，提交它返回状态冲突。版本高于该表：返回能力版本过新，原字节不动（B-84、B-85、C-17、C-26）。
- 显示值按调用方给出的原文存进片段。本轮不解释曝光或色温的含义，也不做 Free 分组。
- `kernel/` 不包含 Qt 头。内核测试不链接 Qt。
- 日志可以记路径、结果码、修订号和能力标识，不记显示值，不记像素，不记整份文档（架构第 6.1 节、S-21、S-24）。
- 不使用 `system` 或 shell 移动文件（S-02）。

本计划明确不做：`kernel-cli`、SQLite、预设、账本、导出队列、历史折叠、Qt 壳、GPU、任何带 Free 的参数模式。对应的 T-12、T-16、T-17、T-20、T-21 保持未实现。

---

### Task 1: support

**Files:**
- Create: `kernel/support/include/pixcake/support/result_code.hpp`
- Create: `kernel/support/include/pixcake/support/checksum.hpp`
- Create: `kernel/support/include/pixcake/support/log_sink.hpp`
- Create: `kernel/support/include/pixcake/support/fast_export_latch.hpp`
- Create: `kernel/support/src/result_code.cpp`
- Create: `kernel/support/src/checksum.cpp`
- Test: `tests/kernel/support/test_support.cpp`

**Interfaces:**
- Consumes: 无
- Produces: `pixcake::ResultCode`、`result_code_label`、`checksum_fnv1a64`、`LogSink`、`FastExportLatch`

- [ ] **Step 1: Write the failing test**

覆盖 C-10 到 C-22、C-61 的中文名各不相同。空输入的 FNV-1a 64 位是 `0xcbf29ce484222325`，单字节 `a` 是 `0xaf63dc4c8601ec8c`。`LogRecord` 没有像素字段。`FastExportLatch` 只有 `enabled()`。

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake -S . -B build && cmake --build build --target test_support && ctest --test-dir build -R test_support --output-on-failure`

Expected: 编译失败，因为还没有这些类型。

- [ ] **Step 3: Write minimal implementation**

结果码按契约语义命名。校验和只在 `support` 里实现一次。日志接口不写文件。

- [ ] **Step 4: Run test to verify it passes**

Expected: `test_support` 通过。

- [ ] **Step 5: Commit**

```bash
git add kernel/support tests/kernel/support CMakeLists.txt tests/CMakeLists.txt .gitignore
git commit -m "$(cat <<'EOF'
feat: add kernel result codes and checksum

EOF
)"
```

### Task 2: persist

**Files:**
- Create: `kernel/persist/include/pixcake/persist/file_system.hpp`
- Create: `kernel/persist/include/pixcake/persist/atomic_file.hpp`
- Create: `kernel/persist/src/atomic_file.cpp`
- Create: `kernel/persist/src/posix_file_system.cpp`
- Test: `tests/kernel/persist/test_atomic_file.cpp`
- Test: `tests/support/fake_file_system.hpp`

**Interfaces:**
- Consumes: `ResultCode`
- Produces: `FileSystem`、`atomic_replace`、`open_durable_file`、`PosixFileSystem`

- [ ] **Step 1: Write the failing test**

`atomic_replace` 成功时必须先 `sync_file` 再换名，最后 `sync_directory`，然后才返回成功。磁盘已满、权限不足、输入输出失败时，正式文件仍是上一份。同步目录失败要自行回滚。进程在「正式文件已换成备份、临时文件还没换成正式文件」时停住，下次 `open_durable_file` 用备份恢复。谓词拒绝的文件进入 `isolate/`，没有备份时 `recovered_empty`。

- [ ] **Step 2: Run test to verify it fails**

Expected: 编译失败，因为还没有 `atomic_replace`。

- [ ] **Step 3: Write minimal implementation**

五步都走 `FileSystem`。不调用 `system`。不把正式路径传给 `write_new`。

- [ ] **Step 4: Run test to verify it passes**

Expected: `test_atomic_file` 通过。

- [ ] **Step 5: Commit**

```bash
git add kernel/persist tests/kernel/persist tests/support/fake_file_system.hpp
git commit -m "$(cat <<'EOF'
feat: replace edit files without truncating the last good copy

EOF
)"
```

### Task 3: editdoc

**Files:**
- Create: `kernel/editdoc/include/pixcake/editdoc/edit_document.hpp`
- Create: `kernel/editdoc/src/edit_document.cpp`
- Test: `tests/kernel/editdoc/test_edit_document.cpp`
- Test: `tests/recovery/test_document_recovery.cpp`

**Interfaces:**
- Consumes: `atomic_replace`、`open_durable_file`、`checksum_fnv1a64`
- Produces: `EditDocument::create`、`load`、`set_preview`、`commit_preview`、`store_fragment`、`write_snapshot`、`content_checksum`

- [ ] **Step 1: Write the failing test**

断言：预览不写盘；提交后修订号加一，片段里是显示值原文；另一个能力的字节不变；未知记录还在；过高版本返回能力版本过新且字节不变；不在版本表里的能力返回状态冲突且字节不变；截断文件被隔离；有备份时用备份；没有备份时标成参数已恢复为空，并且不把空文档写成正式文件；校验和不包含另给的原图路径。

- [ ] **Step 2: Run test to verify it fails**

Expected: 编译失败，因为还没有 `EditDocument`。

- [ ] **Step 3: Write minimal implementation**

信封记录类型只包含架构第 6.4 节已经点名的事实，另加 B-116 的恢复标记和原样保留的未知类型。片段正文用同样的类型加长度；类型 1 是参数标识和显示值原文。

- [ ] **Step 4: Run test to verify it passes**

Expected: `test_edit_document` 和 `test_document_recovery` 通过。

- [ ] **Step 5: Commit**

```bash
git add kernel/editdoc tests/kernel/editdoc tests/recovery
git commit -m "$(cat <<'EOF'
feat: keep unknown edit fields across save and reload

EOF
)"
```

### Task 4: command 和依赖禁令

**Files:**
- Create: `kernel/command/include/pixcake/command/command_gate.hpp`
- Create: `kernel/command/src/command_gate.cpp`
- Test: `tests/kernel/command/test_command_gate.cpp`
- Test: `tests/arch/check_dependencies.cmake`

**Interfaces:**
- Consumes: `EditDocument`、`FastExportLatch`、`FileSystem`、`LogSink`
- Produces: `UserCommand`、`CommandGate::submit`

- [ ] **Step 1: Write the failing test**

断言：缺字段不写盘；预览多次修订号不变，磁盘无写入；提交写入最新预览值；旧修订号不覆盖；极速导出开关开着时预览和提交都返回极速导出占用，把队列深度改成 0 仍然拒绝，关掉开关才恢复；两个相同修订号同时提交只有一个成功。日志里没有显示值。`tests/arch` 对架构第 14 节十条都执行检查。

- [ ] **Step 2: Run test to verify it fails**

Expected: 编译失败，因为还没有 `CommandGate`。

- [ ] **Step 3: Write minimal implementation**

命令名只接受 `preview` 和 `commit`。其他名字返回状态冲突。锁覆盖「读修订号、改预览或写盘」整段。

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`

Expected: 全部通过，包含 `arch_dependencies`。

- [ ] **Step 5: Commit**

```bash
git add kernel/command tests/kernel/command tests/arch
git commit -m "$(cat <<'EOF'
feat: reject stale edits and fast-export changes at the command gate

EOF
)"
```
