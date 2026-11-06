# 跨进程修订条件收口验收记录：2026-11-05

计划：`docs/superpowers/plans/2026-11-05-cross-process-revision-closure.md`
对应：第三轮审核 `specs/review/2026-11-05-pixcake-seven-gate-review-r3.md` 打回项 1（C-23、B-60、B-73）

## 改动

- `kernel/persist`：新增 `check_durable_revision`。调用方持有同一路径的 `WriteScope`（路径 `flock` + 进程内锁）后，重读正式文件（含 `.bak` 恢复），把持久修订号与期望值比较；不一致返回 `StateConflict`。
- `kernel/editdoc`：`commit_preview`、`commit_fragment_replacement`、`commit_baked_reset` 和 `write_snapshot` 写盘前都做这项比较。所有经过这些方法的入口（命令入口、预设、同步、应用至精修）都受同一条件约束，绕开 `CommandGate` 也绕不开它。
- `kernel/layerdoc`：图层文件新增自己的修订号记录（类型 9，u64 小端；旧文件没有这条记录时按 0 处理）。`save` 改为带条件提交：成功后修订号加一，失败时不变。未知记录的范围从 ≥9 改为 ≥10。

## 测试（新增，名称带清单编号）

| 测试 | 断言 |
| --- | --- |
| `test_atomic_file::test_c23_check_durable_revision` | 文件不存在时返回成功；修订号相同返回成功、不同返回冲突；正式文件损坏时按 `.bak` 的修订号比较 |
| `test_edit_document::test_c23_b73_stale_commit_preview_is_rejected` 等 4 项 | 过期句柄的预览提交、片段替换、烘焙重置、快照写入都返回 `StateConflict`；磁盘保留先提交的修订 1 和曝光 `1.00` |
| `test_edit_document::test_b73_reloaded_handle_commits_next_revision` | 被拒的句柄重新装载后，以修订 1 提交成功，磁盘变为修订 2 |
| `test_layer_document::test_c23_b73_stale_layer_save_is_rejected` | 过期的图层句柄保存时返回冲突，磁盘保留先保存的不透明度 40、修订 2 |
| `test_layer_document::test_b116_layer_file_without_revision_record_loads_as_zero` | 旧格式文件装载成功，修订号为 0，且不产生未知记录 |
| `test_layer_document::test_b93_failed_layer_save_keeps_revision` | 磁盘满时修订号不前进，恢复后正常加一 |
| `test_command_gate::test_b60_c23_cross_process_gate_rejects_stale_commit` | 用 `fork` 起两个进程，都在修订 0 装载；先提交的成功，后提交的得到 `StateConflict`；磁盘为修订 1、曝光 `2.00` |

RED 证据：上面每项都先在没有实现的代码上运行并失败。精修文档的 4 个拒绝用例原先返回 `Ok`、磁盘被覆盖；图层用例原先编译失败；跨进程用例在暂存产品改动的基线上失败（子进程退出码非 0，磁盘为 `-2.00`）。

## 审核探针复跑

| 探针 | 第三轮结果 | 本次结果 |
| --- | --- | --- |
| `probe_cross_handle_commit` | B 以过期修订提交返回 `Ok`，磁盘被改成 `-1.00` | B 返回 `StateConflict`，磁盘保持修订 1、`1.00` |
| `probe_cross_process_gate` | 两个进程都返回 `Ok`、修订号都是 1，磁盘为 `-2.00` | 后提交的进程返回码 12（`StateConflict`）、修订号仍为 0；磁盘保持修订 1、`2.00` |

## 验证

```text
cmake --build <scratchpad>/build-r3 --parallel 8
ctest --test-dir <scratchpad>/build-r3 -j8 --output-on-failure   → 51/51 通过
git diff --check                                                 → 通过
```

## 行为变化与已知取舍

- `EditDocument::copy_committed` / `write_snapshot` 不再覆盖修订号不同的已有目标文件，改为返回 `StateConflict`。
- 正式文件损坏且没有可用备份时（`recovered_empty`），没有可比较的持久版本，提交以当前句柄为准写回。
- 每次提交会多读并解码一次正式文件。精修和图层文档体积小，没有做性能基准；要在第四轮审核时按 P-16 复核。

## 不在本次范围

- 极速导出门闩的跨进程可见性（B-62）和单实例（B-67、S-29、P-25）仍未实现。
- `CommandGate` 的进程内修订表保留；正确性现在由持久修订条件保证，这张表只起附加作用。
