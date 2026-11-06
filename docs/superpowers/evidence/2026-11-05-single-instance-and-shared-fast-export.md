# 单实例与跨进程极速导出开关：2026-11-05

计划：`docs/superpowers/plans/2026-11-05-single-instance-and-shared-fast-export.md`
对应：第三轮审核第 7 条（B-62 不通过；B-67 未实现；P-25 中“没有单实例，两个进程可同时打开同一套 SQLite”的一半；S-29）。

## 做了什么

- **跨进程极速导出开关**：`storage/sqlite` 的 `SqliteFastExportLatch` 实现 `FastExportLatch`。每次 `enabled()` 都从导出事件表重读最后一条 `fast-on`/`fast-off`，所以导出进程或主进程任何一方写入的开关，另一方下一次判断就能看到。读不出状态时按“开着”处理。`CommandGate` 本来就接收任意 `FastExportLatch`，把这个开关交给它即可，命令入口不用改。
- **单实例**：新库 `services/instance`，不依赖窗口系统，按架构第 576 行实现：
  - 用 `flock` 锁住 `instance.lock` 的进程是主实例，它在 `instance.sock` 上监听本机套接字；
  - 后启动的进程拿不到锁，就发一行 `activate` 后退出；
  - 套接字文件权限 0600，并用 `getpeereid` 核对连接方是同一用户；
  - 锁随进程退出释放，主实例被强杀后，下一次启动删掉残留的套接字，重新监听。
- **壳层**：
  - 新增 `--instance-dir`，默认 `~/Library/Application Support/PixCake`；
  - 在构造窗口之前抢锁；没抢到就通知已有实例，返回 0；
  - 抢锁出现读写错误时返回 4；通知不到已有实例时返回 5；两种情况都不显示窗口；
  - 主实例收到 `activate` 时把窗口恢复并提到前台；
  - `--smoke` 不抢锁，并行的冒烟测试互不阻挡。

## 测试（RED → GREEN）

| 测试 | RED | 清单 |
| --- | --- | --- |
| `test_sqlite_fast_export_latch::test_b62_other_process_sees_fast_export` | 头文件不存在 | B-62、C-21 |
| `test_sqlite_fast_export_latch::test_b62_empty_queue_keeps_fast_export` | 同上 | B-62、C-03、T-18 |
| `test_sqlite_fast_export_latch::test_b62_unreadable_log_fails_closed`、`test_b62_missing_database_is_reported` | 同上 | B-62 |
| `test_single_instance::test_b67_second_claim_sees_running_primary` | 头文件不存在 | B-67 |
| `test_single_instance::test_t21_second_launch_activates_primary` | 同上 | T-21 |
| `test_single_instance::test_s29_killed_primary_releases_lock` | 同上 | S-29 |
| `test_single_instance::test_s29_concurrent_claims_have_one_winner`（8 个进程同时抢） | 同上 | S-29、P-25 |
| `test_single_instance::test_b67_long_socket_path_is_rejected` | 同上 | B-67 |
| `test_shell_single_instance_T21`（真实壳层可执行文件，offscreen） | `Unknown option 'instance-dir'` | B-67、T-21、S-29 |

全量 CTest 58/58 通过；单实例两项测试连续跑 3 次都通过。

**测试方法上的一处发现**：跨进程开关测试一开始用 `fork`，子进程打开导出库时返回 `IoFailed`。原因是 SQLite 在进程内部用一张全局表记录已打开文件和锁状态，fork 而不 exec 的子进程会继承父进程的这份记录，但并不真正持有那些锁，所以这不代表真实场景。改用 `posix_spawn` 重新启动测试程序本身，作为独立的进程，之后全部通过；产品代码没有因此改动。

## 仍未关闭

- **单实例锁目前只靠启动顺序保证“在开库之前”**：壳层现在还不打开任何数据库，所以“第二个实例不开第二套库”是靠“抢锁在构造窗口之前”这一启动顺序来保证的。以后壳层接入 SQLite 时，开库代码应当要求先持有 `PrimaryInstance`。这条留给第四轮复审核对。
- **P-25 的另一半**（修订号比较放进跨进程串行区）已在第 1 项 `b75c7e8` 关闭。
- 拖文件到程序图标交给已有实例（需求 §3.2 后半）、Windows 和鸿蒙的单实例实现、真实 macOS 下通过程序坞第二次启动的激活，都不在本计划内。macOS 从程序坞再次打开时，系统本身就只会激活已有程序；本计划测的是直接运行可执行文件的情况。
- 渲染进程收到预览前也应当看这个开关。目前预览只经过主进程的 `CommandGate`，这一点要等 IPC 接通（renderd Task 2）后再核对。
