# 日志与运行时计数 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

日期：2026-11-05
状态：已完成
分支：`codex/desktop-official-layout`，基线 `48a7bf5`

**Goal:** 关闭第三轮审核 O-01、O-04、O-05 的“不通过”和 O-06–O-12 的“未实现”：跨模块失败、恢复分支、重复记账拦截、写盘与导出提交的开始/完成都有能按模块和结果码数出来的记录；renderd/exportd 进程入口把记录写进 `logs/`。

**Architecture:** `LogRecord` 增加 `module`、`event`、`task_id`、`document_id`、`charged` 五个字段。`kernel/support` 新增 `CountingLogSink`：在内存里按（模块、事件、结果码）计数，并可转发给下游 sink；这就是 O-06–O-12 的“本机计数”。各内核模块照现有 `CommandGate`/`ProjectLibrary` 的做法接收可选的 `LogSink*`（默认 `nullptr`，不改现有调用方的行为）。写文件的 `FileLogSink` 放在新库 `services/logging`，由进程入口创建；内核不决定日志位置（架构 §6 第 209 行）。

**Tech Stack:** C++17、CMake/CTest。

**Spec:**
- 运维清单 `docs/superpowers/specs/review/2026-11-02-pixcake-ops-checklist.md` §1（O-01–O-05）、§2（O-06–O-12）
- 第三轮审核 `2026-11-05-pixcake-seven-gate-review-r3.md` 第 144–145 行、第 180 行第 6 条
- 架构第 209 行（日志接口不写文件，由进程入口接到 `logs/`；不记像素）
- 需求 §3.8（应用支持目录下的 `logs/`）
- 安全清单第 4 节（日志不含像素、密码、令牌、整份带秘密的配置）

## Global Constraints

- 日志只记路径、结果码、编号、修订号、阶段、耗时、计数；不记像素、照片内容、整份精修文档、凭证字节（O-02、O-03）。`access_credential` 不进日志。
- 写盘和导出提交：开始记录在碰任何用户可见文件之前写出，完成记录在校验成功之后写出（O-05）。失败时写失败记录，不写完成记录。
- `LogSink*` 为空时行为与现在完全相同；不新增全局日志对象。
- 事件名用 `模块.动作` 的小写 ASCII 形式，结果码用 `result_code_label`。字段名不冻结（运维清单第 23 行），但测试按这里的事件名断言。
- `FileLogSink` 一条记录一行，字段之间用制表符；字段里的制表符、换行替换为空格。每行开头是 UTC 时间（ISO 8601，到秒）和进程名（O-01）。
- 进程入口的日志目录：`--log-dir <目录>` 参数；没给时用 `$HOME/Library/Application Support/PixCake/logs`（需求 §3.8 只说“系统规定的应用支持目录”，目录名 `PixCake` 是本计划的裁决）。测试一律传 `--log-dir`，不写用户真实目录。
- 测试名带清单编号。

## 事件表

| 事件 | 模块 | 何时写 | 关键字段 | 对应 |
| --- | --- | --- | --- | --- |
| `persist.write.start` | persist | `atomic_replace` 写临时文件之前 | path | O-05 |
| `persist.write.finish` | persist | 五步全部成功之后 | path、duration_ms | O-05、O-07 |
| `persist.write.failed` | persist | 任一步失败 | path、code（磁盘满为 `DiskFull`） | O-01、O-11 |
| `persist.recover.backup` | persist | `open_durable_file` 改用备份 | path | O-04、O-10 |
| `persist.recover.empty` | persist | 恢复为空 | path | O-04、O-10 |
| `persist.quarantine` | persist | 坏文件进隔离区 | path | O-04 |
| `editdoc.commit` | editdoc | 提交（预览提交、片段替换、烘焙复位、快照）结束时 | document_id=路径、revision、code、duration_ms | O-01、O-07 |
| `export.commit.start` / `export.commit.finish` / `export.commit.failed` | exportq | 写临时文件前 / 换名并校验后 / 失败 | task_id、path、code | O-05、O-08 |
| `export.terminal` | exportq | 任务进入成功、失败、取消 | task_id、phase=终态、code=失败原因 | O-08 |
| `ledger.task.terminal` | ledger | 生成任务成功、失败、取消、崩溃 | task_id、phase=取消前状态、charged | O-09 |
| `ledger.dedup` | ledger | 同一任务编号/笔触编号/导出编号的重复记账被挡住 | task_id、phase=`task`/`stroke`/`export`、deduplicated=true | O-04、O-12 |
| `preview.merged` | preview | 新预览值替换了尚未取走的旧值 | revision=被替换值的基础修订 | O-06 |
| `preview.taken` | preview | 取走最新预览 | revision、code | O-06 |
| `render.rejected` | preview | 编译失败（含依赖环路、图层环路） | code、message=诊断文字 | O-04 |
| `process.start` / `process.exit` | renderd/exportd | 进程入口启动、退出 | message=进程名 | O-01、O-10 |

## Review Focus

1. 写盘第一步就失败（临时文件写不进去）：必须有 `start` 和 `failed`，没有 `finish`。
2. 磁盘满（`DiskFull`）连续发生两次：计数是 2，不是只记第一次。
3. 同一笔触编号重放三次：只扣一次，`ledger.dedup` 计数是 2。
4. 生成中取消：记录 `phase=生成中`、`charged=true`；排队中取消：`phase=排队中`、`charged=false`。两者不能记反（O-09）。
5. 带访问凭证的照片导入、带秘密的配置：日志文件里找不到凭证字节。

---

## 文件

- Modify: `kernel/support/include/pixcake/support/log_sink.hpp`；Create: `kernel/support/include/pixcake/support/counting_log_sink.hpp`、`kernel/support/src/counting_log_sink.cpp`
- Create: `services/logging/{CMakeLists.txt,include/pixcake/logging/file_log_sink.hpp,src/file_log_sink.cpp}`；Modify: 顶层 `CMakeLists.txt`
- Modify: `kernel/persist`（`atomic_replace`、`open_durable_file` 加 `LogSink* log = nullptr`）
- Modify: `kernel/editdoc`（`set_log`，各提交路径把 log 传给 persist）
- Modify: `kernel/exportq`（`set_log`）、`kernel/ledger`（`set_log`）、`kernel/preview`（`set_log`）
- Modify: `apps/renderd/main.cpp`、`apps/exportd/main.cpp`、两者的 `CMakeLists.txt`
- Test: `tests/kernel/support/test_support.cpp`、`tests/services/test_file_log_sink.cpp`、`tests/kernel/persist/test_atomic_file.cpp`、`tests/kernel/editdoc/test_edit_document.cpp`、`tests/kernel/exportq/test_export_queue.cpp`、`tests/kernel/ledger/test_ledger.cpp`、`tests/kernel/preview/*`、`tests/CMakeLists.txt`（进程入口的日志测试）

### Task 1: 记录结构、计数和文件日志

**Interfaces（Produces）:**
```cpp
struct LogRecord {  // 现有字段保留，新增：
  std::string module;       // "persist"、"exportq"…
  std::string event;        // 事件表里的事件名
  std::string task_id;
  std::string document_id;
  bool charged = false;
};
class CountingLogSink : public LogSink {
 public:
  explicit CountingLogSink(LogSink* forward = nullptr);
  void write(const LogRecord& record) override;   // 计数后转发
  std::uint64_t count(std::string_view event) const;
  std::uint64_t count(std::string_view event, ResultCode code) const;
};
// services/logging
class FileLogSink : public LogSink {
 public:
  static std::unique_ptr<FileLogSink> open(const std::string& directory, std::string process_name, ResultCode& code);
  void write(const LogRecord& record) override;   // 追加一行到 <directory>/<process_name>.log
};
std::string default_log_directory();  // $HOME/Library/Application Support/PixCake/logs
```

- [x] **Step 1: 写失败测试**
  - `test_support::test_o06_counting_sink_counts_by_event_and_code`：写入两条 `persist.write.failed/DiskFull`、一条 `persist.write.failed/IoFailed`，`count("persist.write.failed") == 3`，`count(..., DiskFull) == 2`；下游 sink 收到 3 条。
  - `test_file_log_sink::test_o01_line_has_time_process_module_code`：写一条 `{module=exportq, event=export.terminal, code=DiskFull, task_id="e1", path="/a\tb"}`，文件只有一行，以 `YYYY-MM-DDTHH:MM:SSZ\texportd\t` 开头，包含 `exportq`、`export.terminal`、`DiskFull` 的标签、`e1`，路径里的制表符变成空格。
  - `test_file_log_sink::test_o01_unwritable_directory_reports_failure`：目录是一个已存在的普通文件时 `open` 返回空指针，`code != Ok`。
- [x] **Step 2:** 编译失败。
- [x] **Step 3:** 实现；`services/logging` 只链接 `pixcake_support`。
- [x] **Step 4:** 全量 CTest 通过。

### Task 2: 写盘、恢复、精修文档提交

**Interfaces:**
- `atomic_replace(files, path, bytes, LogSink* log = nullptr)`；`open_durable_file(files, path, valid, LogSink* log = nullptr)`。
- `EditDocument::set_log(LogSink*)`；`load` 与 `copy_committed` 增加末尾参数 `LogSink* log = nullptr`，并把它设为载入后文档的 log。

- [x] **Step 1: 写失败测试**（`test_atomic_file.cpp`）
  - `test_o05_write_start_precedes_files_and_finish_follows`：`FakeFileSystem` 在第一次写入时把“当前日志条数”记下来；断言写入发生时已经有 `persist.write.start`，结束后有 `persist.write.finish`，`finish` 的 `duration_ms` 字段存在（≥0）。
  - `test_o11_disk_full_is_counted_each_time`：写入失败码设为 `DiskFull`，连续两次 `atomic_replace`，`count("persist.write.failed", DiskFull) == 2`，`count("persist.write.finish") == 0`。
  - `test_o10_backup_and_empty_recovery_are_logged`：正式文件损坏、备份完好 → `persist.recover.backup` 1 次、`persist.quarantine` 1 次；两者都坏 → `persist.recover.empty` 1 次。
- [x] **Step 2: 写失败测试**（`test_edit_document.cpp`）`test_o07_commit_logs_revision_and_code`：设置 log 后 `commit_preview` 成功，有一条 `editdoc.commit`，`revision` 等于新修订，`code == Ok`；随后旧修订提交返回 `StateConflict`，有一条 `editdoc.commit` 带 `StateConflict`。
- [x] **Step 3:** 确认失败，实现，全量 CTest 通过。

### Task 3: 导出队列

**Interfaces:** `ExportQueue::set_log(LogSink*)`。

- [x] **Step 1: 写失败测试**（`test_export_queue.cpp`）
  - `test_o05_export_commit_start_and_finish`：成功的 `commit_product` 产生 `export.commit.start`（在临时文件写入前）和 `export.commit.finish`，`task_id` 为导出编号；`export.terminal` 的 `phase` 为 `成功`。
  - `test_o08_failed_export_records_reason`：临时文件写入返回 `DiskFull` → `export.commit.failed/DiskFull`，没有 `finish`；`fail()` 之后 `export.terminal` 的 `phase` 为 `失败`、`code` 等于 `failure_reason`。
- [x] **Step 2:** 确认失败，实现，全量 CTest 通过。

### Task 4: 账本、预览、渲染拒绝

**Interfaces:** `CreditLedger::set_log(LogSink*)`、`PreviewCoordinator::set_log(LogSink*)`。

- [x] **Step 1: 写失败测试**（`test_ledger.cpp`）
  - `test_o09_cancel_position_and_charge_are_logged`：排队中取消 → `ledger.task.terminal`，`phase=排队中`、`charged=false`；另一任务生成中取消 → `phase=生成中`、`charged=true`。
  - `test_o12_replayed_stroke_is_counted_once_charged`：同一笔触编号调用三次，点数只扣一次，`count("ledger.dedup") == 2`，记录的 `phase == "stroke"`。
  - `test_o12_repeated_success_is_deduplicated`：`succeed` 调两次，第二次记 `ledger.dedup`、`phase == "task"`。
- [x] **Step 2: 写失败测试**（预览测试文件）
  - `test_o06_replaced_preview_values_are_counted`：连续 `submit` 三次再 `take_latest`，`count("preview.merged") == 2`，`count("preview.taken", Ok) == 1`。
  - `test_o04_rejected_compile_is_logged`：取走时编译失败（参数值不符合模式的旧片段，或基础修订不一致），记 `render.rejected` 或 `preview.taken` 带非 `Ok` 码；编译失败时 `message` 含诊断文字。
- [x] **Step 3:** 确认失败，实现，全量 CTest 通过。

### Task 5: 进程入口、证据与提交

- [x] **Step 1: 写失败测试**（`tests/CMakeLists.txt`）：`test_renderd_health_writes_log`、`test_exportd_health_writes_log` 用 `--health --log-dir <构建目录下的临时目录>` 运行，再由 CMake 脚本断言 `<dir>/renderd.log`（`exportd.log`）里有 `process.start` 和 `process.exit` 两行。
- [x] **Step 2:** 实现：参数解析允许 `--health` 与 `--log-dir` 任意顺序；日志目录打不开时健康检查仍输出原文字，但返回码为 3，并在 stderr 说明日志不可用。
- [x] **Step 3:** 写证据 `docs/superpowers/evidence/2026-11-05-logging-and-metrics.md`：事件表、测试名、RED 证据、仍未接日志的路径（壳层、导入、项目库以外的 sync/generate 尚未接入；生成落层 O-23 未实现所以没有落层事件）。
- [x] **Step 4:** `arch_dependencies` 通过；全量 CTest；`git diff --check`；勾选；提交 `feat(ops): structured logs and runtime counters for persist, export, ledger and preview`。
