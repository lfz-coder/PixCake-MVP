# 日志与运行时计数：2026-11-05

计划：`docs/superpowers/plans/2026-11-05-logging-and-metrics.md`
对应：第三轮审核 O-01、O-04、O-05（不通过）与 O-06–O-12（未实现），审核记录第 144–145 行、第 180 行第 6 条。

## 做了什么

- `LogRecord` 增加 `module`、`event`、`task_id`、`document_id`、`charged`；仍然没有任何字节或像素字段，凭证和图像内容在结构上进不了日志（O-02、O-03）。
- `kernel/support` 的 `CountingLogSink`：按事件名和结果码计数，并转发给下游，用作运维清单第 2 节的本机计数。
- `services/logging`：
  - `FileLogSink`：一条记录一行，用制表符分隔，行首是 UTC 时间和进程名（O-01）。
  - `ProcessLog`：进程入口写 `process.start` 和 `process.exit`（O-10 的进程重启次数）。
  - `default_log_directory()` 返回 `$HOME/Library/Application Support/PixCake/logs`（需求 §3.8）。
- renderd/exportd：接受 `--health` 和 `--log-dir <目录>`，顺序不限。日志目录不可用时，健康检查文字照常输出，返回码为 3，并在 stderr 说明原因。
- 接入日志的模块和事件见计划里的事件表：persist（写盘开始、完成、失败，隔离，改用备份，恢复为空，打开失败）、editdoc（每次提交带修订号和耗时）、exportq（提交开始、完成、失败，终态）、ledger（终态带取消位置和是否扣点、崩溃、重复记账被挡住）、preview（合并、取走）。

## 测试（RED → GREEN）

| 测试 | RED | 清单 |
| --- | --- | --- |
| `test_support::test_o06_counting_sink_counts_by_event_and_code` | 头文件不存在 | O-06–O-12 计数 |
| `test_file_log_sink::test_o01_line_has_time_process_module_code`、`test_o01_unwritable_directory_reports_failure` | 库不存在 | O-01 |
| `test_atomic_file::test_o05_write_start_precedes_files_and_finish_follows` | 参数个数不符 | O-05、O-07 |
| `test_atomic_file::test_o11_disk_full_is_counted_each_time` | 同上 | O-11 |
| `test_atomic_file::test_o10_backup_and_empty_recovery_are_logged` | 同上 | O-04、O-10 |
| `test_edit_document::test_o07_commit_logs_revision_and_code` | 没有 `set_log` | O-01、O-07 |
| `test_export_queue::test_o05_export_commit_start_and_finish`、`test_o08_failed_export_records_reason` | 没有 `set_log` | O-05、O-08 |
| `test_ledger::test_o09_cancel_position_and_charge_are_logged` | 没有 `set_log` | O-09 |
| `test_ledger::test_o12_replayed_stroke_is_counted_once_charged`、`test_o12_repeated_success_is_deduplicated` | 同上 | O-04、O-12 |
| `test_preview_coordinator::test_o06_replaced_preview_values_are_counted`、`test_o06_stale_take_is_logged_with_code` | 没有 `set_log` | O-06 |
| `test_renderd_health_writes_log`、`test_exportd_health_writes_log` | 退出码 2（不认识 `--log-dir`） | O-01、O-10 |

全量 CTest 55/55 通过，其中包括 `arch_dependencies`。

## 仍未关闭

- **O-04 依赖环路**：能力注册表的构造函数是私有的，只能拿到 `phase_one()`；只看精修文档的编译，只在“恢复为空”或“没有原图编号”时失败，而这两种情况在预览取走时已经先被编号核对挡住。所以预览阶段的环路拒绝从公开接口触发不到，没有为它写假测试。图层父子环路在 `compile(document, layers, registry)` 里给出诊断，但它现在还没有运行时调用方。等 renderd 接上图层编译时，再补 `render.rejected`。
- **还没接日志的路径**：
  - 壳层进程入口；
  - 图层文档写盘：`LayerDocument::save` 调 `atomic_replace` 时还没有传日志；
  - 导入、同步、生成；
  - 生成落层（O-23 本身未实现，所以没有落层事件）。
- **调用方还没传日志**：现在只有测试会设置 sink。壳层和服务进程把 `ProcessLog::sink()` 交给各个模块，要等 UI 接入内核以后才做。
- O-13–O-20（用户能看见的失败）不在这一项的范围内。
