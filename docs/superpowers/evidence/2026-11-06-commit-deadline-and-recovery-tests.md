# 提交写盘 2 秒判定与恢复测试归集

日期：2026-11-06　对应：第四轮复审条件 6（P-16、B-52、O-07；T-08）

## 2 秒写盘判定
- `kernel/persist`：`kCommitWriteDeadline = 2000 ms`；`atomic_replace(..., deadline = kCommitWriteDeadline)` 从进入函数（含等写锁）开始计时。临时文件刷盘后、动正式文件之前已超时：删临时文件，正式文件不动，返回 `IoFailed`，写 `persist.write.deadline` 与 `persist.write.failed`。换名之后不再判超时（新内容已落盘，报失败会与磁盘不一致）。
- `EditDocument` 与 `LayerDocument` 的提交都走 `atomic_replace`，失败时修订号不前进。
- 测试：`test_atomic_file::test_b52_late_write_keeps_formal_file_and_is_not_success`（10 ms 截止、40 ms 写入）、`test_p16_default_commit_deadline_is_two_seconds`、`test_edit_document::test_b52_commit_slower_than_two_seconds_is_not_success`（真实 2.15 秒写入：提交非成功、修订号仍为 0、磁盘为上一份；之后正常提交成功）。RED：修改前慢写入返回“成功”、修订号前进到 1。
- 未覆盖：截止时间之内但已超过 2 秒才换名完成（目录刷盘慢）的情形仍返回成功，因为此时正式文件已经是新内容。

## `tests/recovery` 归集（T-08）
崩溃、截断、换名和事件重放的测试从各模块测试迁到 `tests/recovery`，原文件删除对应函数与调用，辅助代码各自只保留用到的部分：

| 新文件（CTest 名） | 迁来的测试 | 来源 |
| --- | --- | --- |
| `test_persist_recovery.cpp` | B-111 ×3、B-100、B-08、截断正式文件重开备份、O-10 | `kernel/persist/test_atomic_file.cpp` |
| `test_export_recovery.cpp` | 崩溃点与换名失败、重开修复缓存、recover 的同步/删除失败、拒绝无主临时文件（B-98）、路径逃逸、临时文件清理重试、失败原因重开后保留 | `kernel/exportq/test_export_queue.cpp` |
| `test_ledger_replay.cpp` | 重开重放修复缓存、导出批次重开后身份不变、生成中取消与崩溃不退点、O-12 重放只扣一次 | `kernel/ledger/test_ledger.cpp` |
| `test_history_restart.cpp` | B-101 重开折叠成上一次保存 | `kernel/history/test_history.cpp` |
| `test_source_delete_recovery.cpp` | 删除原图终态写失败后启动收敛 | `kernel/project/test_recycle_bin.cpp` |
| `test_manual_replay.cpp` | 重放失败只标记不删数据 | `kernel/manual/test_manual_stroke_session.cpp` |
| `test_layer_recovery.cpp` | 恢复为空的图层文档保存往返 | `kernel/layerdoc/test_layer_document.cpp` |

核对：测试函数总数 234 → 237（新增 3 个截止测试，迁移无丢失）；CTest 58 → 65，全部通过。
