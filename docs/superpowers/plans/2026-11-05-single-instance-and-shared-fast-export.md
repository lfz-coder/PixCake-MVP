# 单实例与跨进程极速导出开关 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

日期：2026-11-05
状态：已完成
分支：`codex/desktop-official-layout`，基线 `cf9d36b`

**Goal:** 关闭第三轮审核第 7 条：B-62（另一个进程看不到极速导出开关）、B-67/S-29/P-25（没有单实例，两个主进程可以同时打开同一套库）。

**Architecture:**
- 极速导出开关的事实本来就在 SQLite 导出事件表里（`fast-on`/`fast-off` 事件，与缓存同一事务写入）。`storage/sqlite` 新增 `SqliteFastExportLatch`，实现 `FastExportLatch`：每次 `enabled()` 都用自己的连接重读最后一条开关事件，所以任何进程写入的开关，别的进程下一次判断就能看到。读失败时按“开着”处理（宁可拒绝编辑，也不在开关状态不明时放行）。
- 单实例按架构第 576 行，“由平台层的锁和主进程套接字实现，不进内核库”：新库 `services/instance`。用 `flock` 锁住 `<目录>/instance.lock` 的进程是主实例，它在 `<目录>/instance.sock` 上监听本机套接字；后启动的进程拿不到锁，就连上套接字发 `activate`，然后退出，不打开任何数据库。
- 壳层：显示窗口之前先抢锁。抢到了，用 `QSocketNotifier` 监听套接字，收到 `activate` 就把窗口提到前台；没抢到，通知主实例后退出。`--smoke` 不抢锁（并行的冒烟测试不能互相挡）。

**Tech Stack:** C++17、POSIX（`flock`、`AF_UNIX`、`getpeereid`）、SQLite、Qt 6（仅壳层）、CMake/CTest。

**Spec:**
- 需求 §3.2（同一用户同时只跑一个主实例；再次启动激活已有窗口，不再生出第二套数据库）
- 架构第 222、324 行（极速导出由用户关闭，队列变空不自动放开），第 570–576 行（导出进程用事务追加事件；单实例由平台层的锁和主进程套接字实现）
- 边界清单 B-62、B-67；安全清单 S-29；性能清单 P-25；测试清单 T-18、T-21；契约 C-03、C-21
- 第三轮审核第 69、73、102 行与第 181 行第 7 条

## Global Constraints

- 开关只能由用户关闭：队列变空、任务全部完成都不改变 `enabled()` 的结果（C-03）。
- 跨进程开关以导出事件表为准，不另起一份文件状态；读不到表或语句失败时 `enabled()` 返回 `true`。
- 单实例目录默认 `$HOME/Library/Application Support/PixCake`（与第 5 项日志目录同一个根，需求 §3.8）；目录权限 0700。套接字只接受同一用户的连接（`getpeereid` 核对 uid）。
- 消息协议：一行 UTF-8 文本，以 `\n` 结束，最长 4096 字节；本阶段只认 `activate`，其他内容忽略（以后拖文件到图标时再扩展，需求 §3.2 后半）。
- 主实例异常退出后，`flock` 随进程释放；下一次启动删掉残留的套接字文件，重新监听。
- 套接字路径超过 `sockaddr_un::sun_path` 长度时返回 `IoFailed`，不截断。
- 壳层在抢锁失败（不是“已有实例”，而是读写错误）时拒绝启动，返回码 4，不显示窗口。
- 测试名带清单编号。

## Review Focus

1. 主实例被 `kill -9`：第二次启动必须能成为新的主实例，而不是一直以为“已有实例”。
2. 残留的 `instance.sock` 文件存在、但没有进程监听：后启动的一方拿到锁后要能重新绑定。
3. 两个进程同时启动：只有一个成为主实例。
4. 极速导出开着，导出队列已经清空：另一个进程的编辑和预览仍被拒绝。
5. 导出数据库被删表或损坏：开关按“开着”处理，编辑被拒绝，返回 `FastExportBusy`，不放行。

---

## 文件

- Create: `storage/sqlite/include/pixcake/storage/sqlite_fast_export_latch.hpp`、`storage/sqlite/src/sqlite_fast_export_latch.cpp`；Modify: `storage/sqlite/CMakeLists.txt`
- Create: `services/instance/{CMakeLists.txt,include/pixcake/instance/single_instance.hpp,src/single_instance.cpp}`；Modify: 顶层 `CMakeLists.txt`
- Modify: `apps/shell/main.cpp`、`apps/shell/CMakeLists.txt`
- Test: `tests/storage/test_sqlite_fast_export_latch.cpp`、`tests/services/instance/test_single_instance.cpp`、`tests/apps/check_shell_single_instance.sh`、`tests/CMakeLists.txt`

### Task 1: 跨进程极速导出开关

**Interfaces（Produces）:**
```cpp
namespace pixcake {
class SqliteFastExportLatch : public FastExportLatch {
 public:
  // path 是已由 SqliteExportLog 建好的导出库；文件不存在返回 IoFailed。
  static ResultCode open(const std::string& path, std::unique_ptr<SqliteFastExportLatch>& out);
  ~SqliteFastExportLatch() override;
  bool enabled() const override;          // 读失败返回 true
  ResultCode read(bool& on) const;        // 读失败原样返回结果码
};
}
```
- `read`：`SELECT kind FROM export_event WHERE kind IN ('fast-on','fast-off') ORDER BY seq DESC LIMIT 1`；没有行为关闭。

- [x] **Step 1: 写失败测试**（`test_sqlite_fast_export_latch.cpp`，用 `fork` 模拟另一个进程）
  - `test_b62_other_process_sees_fast_export`：父进程建库并打开 `SqliteFastExportLatch`；子进程用自己的 `SqliteExportLog` + `ExportBook` 打开同一个库并 `set_fast_export(true)` 后退出；父进程 `enabled()` 为真；用这个开关构造的 `CommandGate` 提交 `commit` 和 `preview` 都返回 `FastExportBusy`。
  - `test_b62_empty_queue_keeps_fast_export`：上面的状态下，子进程入队一个任务并让它走到成功（队列变空），父进程仍然 `enabled()`；另一个子进程 `set_fast_export(false)` 后，父进程的 `commit` 返回 `Ok`。
  - `test_b62_unreadable_log_fails_closed`：删掉 `export_event` 表后，`read` 返回非 `Ok`，`enabled()` 为真。
  - `test_b62_missing_database_is_reported`：路径不存在时 `open` 返回 `IoFailed`。
- [x] **Step 2:** 编译失败（头文件不存在）。
- [x] **Step 3:** 实现：只读打开（`SQLITE_OPEN_READONLY`），`busy_timeout` 1000 ms，与现有导出库一致。
- [x] **Step 4:** 全量 CTest 通过。

### Task 2: 单实例锁和主进程套接字

**Interfaces（Produces，`pixcake/instance/single_instance.hpp`）:**
```cpp
namespace pixcake::instance {
class PrimaryInstance {
 public:
  ~PrimaryInstance();                       // 关闭监听、删除套接字文件、释放锁
  int listen_fd() const;                    // 给壳层的 QSocketNotifier
  // 取一条已到达的消息；没有连接时返回 nullopt，不阻塞。uid 不同的连接直接关闭。
  std::optional<std::string> take_message();
};
struct InstanceClaim {
  ResultCode code = ResultCode::Ok;         // 读写错误时非 Ok
  bool another_running = false;             // 锁已被别的进程持有
  std::unique_ptr<PrimaryInstance> primary; // 成为主实例时非空
};
InstanceClaim claim_instance(const std::string& directory);
ResultCode notify_primary(const std::string& directory, const std::string& message);
std::string default_instance_directory();   // $HOME/Library/Application Support/PixCake
}
```

- [x] **Step 1: 写失败测试**（`test_single_instance.cpp`）
  - `test_b67_second_claim_sees_running_primary`：父进程 `claim_instance` 成为主实例；子进程再 claim，得到 `another_running == true`、`primary == nullptr`、`code == Ok`。
  - `test_t21_second_launch_activates_primary`：子进程 `notify_primary(dir, "activate")` 返回 `Ok`；父进程轮询 `take_message()`（最多 2 秒），得到 `"activate"`。
  - `test_s29_killed_primary_releases_lock`：子进程成为主实例后 `_exit(0)`，不做清理（套接字文件残留）；父进程 claim 成为新的主实例，并能收到另一个子进程发来的 `activate`。
  - `test_s29_concurrent_claims_have_one_winner`：同时 fork 8 个子进程 claim 并持有 300 ms；按退出码统计，恰好 1 个是主实例。
  - `test_b67_long_socket_path_is_rejected`：目录长度超过 `sun_path` 时，claim 返回 `IoFailed`，不建锁之外的文件。
- [x] **Step 2:** 编译失败。
- [x] **Step 3:** 实现；`services/instance` 只链接 `pixcake_support`。
- [x] **Step 4:** 全量 CTest 通过。

### Task 3: 壳层接入

- [x] **Step 1: 写失败测试** `tests/apps/check_shell_single_instance.sh`（ctest 名 `test_shell_single_instance_T21`）：
  - 用 `QT_QPA_PLATFORM=offscreen` 在后台启动第一个壳层 `--page home --instance-dir <tmp>`；等 `<tmp>/instance.sock` 出现（最多 10 秒）。
  - 启动第二个壳层，同样的参数：2 秒内退出，返回码 0，stdout 含 `already running`。
  - 第一个壳层的 stderr 里出现 `activated by another launch`。
  - 结束第一个壳层；再启动一次，又能成为主实例（套接字重新出现）。
- [x] **Step 2:** 确认失败（当前不认识 `--instance-dir`）。
- [x] **Step 3:** 实现：新增 `--instance-dir` 选项；非 `--smoke` 时在构造窗口前 claim；不是主实例就 `notify_primary` 后返回 0；读写错误返回 4；主实例用 `QSocketNotifier` 读 `take_message()`，`activate` 时 `showNormal()`、`raise()`、`activateWindow()`，并 `qInfo` 一行 `activated by another launch`。
- [x] **Step 4:** 全量 CTest（含 5 个壳层冒烟测试）通过。

### Task 4: 证据与提交

- [x] **Step 1:** 证据 `docs/superpowers/evidence/2026-11-05-single-instance-and-shared-fast-export.md`：改动、测试、RED 证据、仍未关闭的项（壳层还没打开数据库，所以“锁在开库之前”目前靠启动顺序保证；拖文件到图标的消息、Windows 与鸿蒙的单实例实现不在本计划内）。
- [x] **Step 2:** `git diff --check`；勾选；提交 `feat(platform): single instance lock and cross-process fast export latch`。
