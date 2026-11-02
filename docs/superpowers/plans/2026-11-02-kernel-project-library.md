# 项目库索引 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 做出项目库里能独立验证的一块：记住图片的绝对路径和访问凭证，标出原文件缺失和原文件回来，并按删除时间把回收站索引保留 30 天。

**Architecture:** 规则在 `kernel/project`。SQLite 只在 `storage/sqlite`，内核源文件不包含 `sqlite3.h`。命令入口继续不写 SQL。原图字节不进库。回收站到期只删索引。只有 `delete_source_file` 会调用删除端口，而且只删调用方给出的那一个路径。

**Tech Stack:** C++17、CMake、CTest、系统 SQLite。测试是独立可执行文件，不引入第三方测试框架。

## Global Constraints

- 用户能看见的行为以需求为准；目录和依赖以架构为准。本计划只建 `kernel/project` 和 `storage/sqlite`。
- 原图只存绝对路径和调用方交来的访问凭证。凭证是不透明字节，原样存、原样读，不拿它扩大读取范围，不写进日志（C-40、S-10、S-21）。
- 路径必须以 `/` 开头。空路径、相对路径不入库，返回状态冲突。本计划不接受 Windows 路径；窗口不在本计划里。
- `add_photo` 不探测文件，也不返回原文件缺失。原文件缺失只用于已经写入索引的路径后来找不到文件（C-15）。格式、损坏、解码不在本计划。
- 探测用 `FileSystem::probe_file`。`exists` 分不清「没有文件」和「没有权限」，不能用来打缺失标记。没有文件才标成原文件缺失；权限不足不改这个标记。
- 标成缺失或恢复时不打开、不改写参数文档。文档校验和不变（B-92、T-19）。参数文档在不在，项目库不解释。
- 回收站保存删除时间。30 天是 2592000 秒，不按日历月。`now - deleted_at < 2592000` 保留；大于等于则到期（C-42、O-33）。
- 启动时先清到期索引，再探测原文件。清索引不调用删除端口，不删用户磁盘上的原图。
- 只有 `delete_source_file` 调用 `SourceDeleter`。权限不足时进入回收站，结果码是权限不足，说明固定为 `磁盘文件还在`，并且不把原图标成已删除（B-102、O-19、S-11）。
- 彻底删除只作用于已经在回收站里的项目，只删该项目和它的图片索引。不删原图。不在回收站里就返回状态冲突，索引不动。
- 恢复项目时，该项目和它名下已在回收站的图片都离开回收站。部分还原只恢复点名的图片；项目自己还在回收站时，部分还原整次失败，一行都不改（需求第 7 节、B-112 的项目和图片部分）。虚拟副本不在本计划。
- 重复丢进回收站返回状态冲突，原来的删除时间不变。
- SQLite 使用预写日志。项目名、路径、文件名、凭证都用参数绑定。禁止把用户字符串拼进 SQL（S-01）。`user_version` 只接受 0 或 1；其他值返回状态冲突，不改那个库。
- 日志可以记模块名 `project`、结果码和路径。不记凭证，不记像素。
- `kernel/` 不包含 Qt 头，不包含 `sqlite3.h`。不调用 `system`。
- 不改命令入口。本计划不发明项目命令的命令名字符串。

本计划明确不做：星级、旗标、色标、筛选、虚拟副本、导入解码、缩略图、`kernel-cli`、预设、账本、导出队列、生成、GPU、Qt 壳、窗口。对应的 T-12、T-16、T-17、T-20、T-21 保持未实现。B-112 里「删原图连带副本、恢复副本带回原图」保持未实现。

---

### Task 1: 时钟

**Files:**
- Create: `kernel/support/include/pixcake/support/clock.hpp`
- Create: `kernel/support/src/clock.cpp`
- Modify: `kernel/support/CMakeLists.txt`
- Modify: `tests/kernel/support/test_support.cpp`

**Interfaces:**
- Consumes: 无
- Produces: `pixcake::Clock::unix_seconds()` 返回 `std::int64_t`；`pixcake::SystemClock`

- [x] **Step 1: Write the failing test**

在 `tests/kernel/support/test_support.cpp` 增加头 `#include "pixcake/support/clock.hpp"`，并在 `main` 返回前增加：

```cpp
  struct FakeClock : pixcake::Clock {
    std::int64_t now = 42;
    std::int64_t unix_seconds() const override { return now; }
  };
  FakeClock fake;
  EXPECT(fake.unix_seconds() == 42);
  fake.now = 100;
  EXPECT(fake.unix_seconds() == 100);

  pixcake::SystemClock system;
  const std::int64_t wall = system.unix_seconds();
  EXPECT(wall > 1760000000);
  EXPECT(wall < 2000000000);
```

- [x] **Step 2: Run test to verify it fails**

Run: `cmake -S . -B build && cmake --build build --target test_support && ctest --test-dir build -R '^test_support$' --output-on-failure`

Expected: 编译失败，因为还没有 `Clock`。

- [x] **Step 3: Write minimal implementation**

`kernel/support/include/pixcake/support/clock.hpp`：

```cpp
#pragma once

#include <cstdint>

namespace pixcake {

// Unix 秒。回收站的 30 天按这个秒数相减，不按日历月。
class Clock {
 public:
  virtual ~Clock() = default;
  virtual std::int64_t unix_seconds() const = 0;
};

class SystemClock : public Clock {
 public:
  std::int64_t unix_seconds() const override;
};

}  // namespace pixcake
```

`kernel/support/src/clock.cpp`：

```cpp
#include "pixcake/support/clock.hpp"

#include <chrono>

namespace pixcake {

std::int64_t SystemClock::unix_seconds() const {
  const auto elapsed = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
}

}  // namespace pixcake
```

`kernel/support/CMakeLists.txt` 的源文件列表加入 `src/clock.cpp`。

- [x] **Step 4: Run test to verify it passes**

Expected: `test_support` 通过。

- [x] **Step 5: Commit**

```bash
git add kernel/support/include/pixcake/support/clock.hpp kernel/support/src/clock.cpp kernel/support/CMakeLists.txt tests/kernel/support/test_support.cpp docs/superpowers/plans/2026-11-02-kernel-project-library.md
git commit -m "$(cat <<'EOF'
feat: add a unix clock for project retention

EOF
)"
```

### Task 2: 路径和访问凭证

**Files:**
- Create: `kernel/project/include/pixcake/project/records.hpp`
- Create: `kernel/project/include/pixcake/project/project_store.hpp`
- Create: `kernel/project/include/pixcake/project/project_library.hpp`
- Create: `kernel/project/src/project_library.cpp`
- Create: `kernel/project/CMakeLists.txt`
- Create: `storage/sqlite/include/pixcake/storage/sqlite_project_store.hpp`
- Create: `storage/sqlite/src/sqlite_project_store.cpp`
- Create: `storage/sqlite/CMakeLists.txt`
- Create: `tests/support/temp_dir.hpp`
- Create: `tests/kernel/project/test_project_paths.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `ResultCode`、`Clock`
- Produces: `ProjectLibrary::create_project`、`ProjectLibrary::add_photo`、`SqliteProjectStore::open`。记录字段见下面的头文件。访问凭证类型是 `std::vector<std::uint8_t>`。

- [x] **Step 1: Write the failing test**

`tests/kernel/project/test_project_paths.cpp` 断言：

- 项目名 `a'b` 存进去再读出来仍是这四个字符，库里只有这一个项目。
- 来源不是 `本地`、`云上传`、`云下载` 三者之一时返回状态冲突，项目数不变。
- 相对路径 `photos/a.jpg` 返回状态冲突，图片数为 0。
- 绝对路径 `/tmp/pic.png`、文件名 `pic.png`、凭证字节 `{1, 2, 0, 255}` 往返后完全一致。空凭证也可以往返。
- `add_photo` 在文件不存在时仍返回成功，presence 为 `可编辑`，结果码不是原文件缺失。
- 关掉存储再打开，上述行还在。`journal_mode()` 为 `wal`。
- 把普通文本写到库路径上再打开，返回输入输出失败，文件内容仍以 `hello` 开头。
- `user_version` 为 2 的库打开时返回状态冲突，版本仍是 2。

- [x] **Step 2: Run test to verify it fails**

Run: `cmake -S . -B build && cmake --build build --target test_project_paths && ctest --test-dir build -R '^test_project_paths$' --output-on-failure`

Expected: 编译失败，因为还没有 `ProjectLibrary`。

- [x] **Step 3: Write minimal implementation**

记录字面量只允许这些值：来源 `本地`、`云上传`、`云下载`；在场 `可编辑`、`原文件缺失`；回收站主体 `项目`、`图片`。`kTrashRetentionSeconds` 为 `2592000`。`kDiskFileRemains` 为 `磁盘文件还在`。

表：

```sql
CREATE TABLE IF NOT EXISTS project (
  id INTEGER PRIMARY KEY,
  name TEXT NOT NULL,
  origin TEXT NOT NULL,
  cloud_marked INTEGER NOT NULL,
  deleted_at INTEGER
);
CREATE TABLE IF NOT EXISTS photo (
  id INTEGER PRIMARY KEY,
  project_id INTEGER NOT NULL,
  absolute_path TEXT NOT NULL,
  file_name TEXT NOT NULL,
  access_credential BLOB NOT NULL,
  presence TEXT NOT NULL,
  FOREIGN KEY(project_id) REFERENCES project(id)
);
CREATE TABLE IF NOT EXISTS trash (
  id INTEGER PRIMARY KEY,
  subject TEXT NOT NULL,
  subject_id INTEGER NOT NULL,
  deleted_at INTEGER NOT NULL
);
```

用户数据只出现在 `sqlite3_bind_*`。`PRAGMA journal_mode=WAL` 和建表语句是常量。`user_version` 建成后为 1。

`create_project` 写入名称、来源和云标识，`deleted_at` 为空。`add_photo` 要求项目存在、项目不在回收站、路径以 `/` 开头、文件名非空。presence 写成 `可编辑`。它不调用文件系统。

- [x] **Step 4: Run test to verify it passes**

Expected: `test_project_paths` 通过。

- [x] **Step 5: Commit**

```bash
git add kernel/project storage tests/support/temp_dir.hpp tests/kernel/project/test_project_paths.cpp tests/CMakeLists.txt CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat: store photo paths and access credentials in sqlite

EOF
)"
```

### Task 3: 原文件缺失和回来

**Files:**
- Modify: `kernel/persist/include/pixcake/persist/file_system.hpp`
- Modify: `kernel/persist/src/posix_file_system.cpp`
- Modify: `tests/support/fake_file_system.hpp`
- Modify: `kernel/project/include/pixcake/project/project_library.hpp`
- Modify: `kernel/project/src/project_library.cpp`
- Create: `tests/kernel/project/test_source_presence.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `FileSystem::probe_file`、`EditDocument`、`LogSink`
- Produces: `ProjectLibrary::refresh_source_presence`、`ProjectLibrary::source_status`、`ProjectLibrary::startup`

`probe_file` 是 const，返回已有的 `IoResult`：普通文件为成功；`ENOENT` 或不是普通文件为 `not_found`；`EACCES`、`EPERM`、`EROFS` 为权限不足。

- [ ] **Step 1: Write the failing test**

断言：

- 索引里的路径从假文件系统消失后，`refresh_source_presence` 把 presence 改成 `原文件缺失`，`source_status` 返回原文件缺失。同一张图的参数文档字节和 `content_checksum` 都不变，随后仍能提交。
- 路径重新放回后，presence 回到 `可编辑`，`source_status` 返回成功，同一文档还能再提交。
- 日志记录的 `code` 是原文件缺失，`path` 是该绝对路径，`message` 是 `project`。凭证文本 `credential-do-not-log` 不出现在 `message` 或 `path`。
- 另一条路径探测返回权限不足时，它的 presence 仍是 `可编辑`，刷新的总结果是权限不足。已经判成缺失的那张保持缺失。

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake -S . -B build && cmake --build build --target test_source_presence && ctest --test-dir build -R '^test_source_presence$' --output-on-failure`

Expected: 编译失败，因为还没有 `refresh_source_presence`。

- [ ] **Step 3: Write minimal implementation**

`refresh_source_presence` 列出全部图片：

- `probe_file` 成功：presence 写成 `可编辑`。
- `not_found`：presence 写成 `原文件缺失`，并写一条日志。
- 其他失败：不改 presence。若还没有别的失败码，总结果用这一条的结果码。

`startup` 先调用 `purge_expired_trash`（这一步在 Task 4 才有到期数据），再刷新。Task 3 的 `purge_expired_trash` 可以先返回成功。

刷新不接收 `EditDocument`，不调用 `read_file`。

- [ ] **Step 4: Run test to verify it passes**

Expected: `test_source_presence` 通过。已有内核测试仍通过。

- [ ] **Step 5: Commit**

```bash
git add kernel/persist kernel/project tests/support/fake_file_system.hpp tests/kernel/project/test_source_presence.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat: mark missing originals without touching edit documents

EOF
)"
```

### Task 4: 回收站 30 天

**Files:**
- Create: `kernel/project/include/pixcake/project/source_deleter.hpp`
- Create: `kernel/project/src/file_source_deleter.cpp`
- Modify: `kernel/project/include/pixcake/project/project_library.hpp`
- Modify: `kernel/project/src/project_library.cpp`
- Modify: `kernel/project/CMakeLists.txt`
- Create: `tests/kernel/project/test_recycle_bin.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Clock`、`SourceDeleter::remove_source(const std::string&)` 返回 `ResultCode`、`FileSystem`
- Produces: `trash_project`、`trash_photo`、`restore_project`、`restore_photos`、`purge_expired_trash`、`erase_trashed_project`、`delete_source_file`、`FileSourceDeleter`

- [ ] **Step 1: Write the failing test**

使用假时钟和假删除端口。断言：

- 删除时间加 2591999 秒再 `startup`，项目和图片仍可恢复，原图字节还在，删除端口调用次数是 0。
- 删除时间加 2592000 秒再 `startup`，项目和图片索引都没有了，原图字节仍在，删除端口调用次数仍是 0。
- 只到期的图片被清掉时，未到期的另一张还在回收站，删除时间不变。
- 恢复项目后，项目和它名下已删除图片都回到在用列表。
- 两张图片在回收站时只恢复第一张，第二张的删除时间不变。空的恢复列表，或项目自己仍在回收站时，整次返回状态冲突，删除时间都不变。
- 再次把已经在回收站的图片丢进去，返回状态冲突，删除时间不变。
- 彻底删除只对回收站中的项目生效。原图字节还在。不在回收站时返回状态冲突。
- `delete_source_file` 成功时，删除端口只收到那一个绝对路径，旁边的另一个文件还在，图片进入回收站，presence 为 `原文件缺失`。
- 删除端口返回权限不足时，结果码是权限不足，`notice` 是 `磁盘文件还在`，原图字节不变，图片进入回收站，presence 仍是 `可编辑`。已经在回收站时，删除时间不变。
- `FileSourceDeleter` 拒绝不以 `/` 开头的路径，并且不调用删除。

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake -S . -B build && cmake --build build --target test_recycle_bin && ctest --test-dir build -R '^test_recycle_bin$' --output-on-failure`

Expected: 编译失败，因为还没有这些方法。

- [ ] **Step 3: Write minimal implementation**

`ProjectLibrary` 公开类旁边写上：回收站按删除时间加 2592000 秒到期；到期只删索引；只有 `delete_source_file` 调用 `SourceDeleter`。

到期判断：`deleted_at > now` 则未到期；否则 `now - deleted_at >= 2592000` 为到期。

`trash_project` 写入项目的 `deleted_at`、一条主体为 `项目` 的回收站行，并把尚未在回收站的图片各写一条主体为 `图片` 的行，时间都用同一次时钟读数。不调用删除端口。

`restore_project` 清掉项目的 `deleted_at`，并删掉该项目和它全部图片的回收站行。

`purge_expired_trash` 和 `erase_trashed_project` 只删行。删项目时连带删掉它的图片行和相关回收站行。

`delete_source_file`：

- 成功：presence 改为 `原文件缺失`；若还不在回收站，按当前时钟进入回收站。`notice` 为空。
- 权限不足：若还不在回收站则进入回收站；不改已有删除时间；不改 presence。`notice` 为 `磁盘文件还在`。
- 原文件缺失：presence 改为 `原文件缺失`；若还不在回收站则进入回收站。`notice` 为空。
- 其他结果码：索引不动。

`startup` 改为先 `purge_expired_trash`，成功后再 `refresh_source_presence`。

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`

Expected: 全部通过，包含 `arch_dependencies`。`kernel/project` 源文件没有 `sqlite3.h`、`QOpenGL` 和 Qt 头。

- [ ] **Step 5: Commit**

```bash
git add kernel/project tests/kernel/project/test_recycle_bin.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat: retain trashed project indexes for 30 days

EOF
)"
```
