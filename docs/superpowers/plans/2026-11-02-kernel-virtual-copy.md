# 虚拟副本 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在项目库里记下虚拟副本：它指向一张原图，带有自己的参数文档编号；删除和恢复按需求第 7 节连带处理；调用方只能问这些条目是否共用一张原图。

**Architecture:** 副本行在 `kernel/project`，SQLite 表在 `storage/sqlite`。副本不复制原图路径，不调用删除端口，不打开参数文档。扣张数不在这里。`user_version` 1 的旧库只追加副本表，不改已有项目和图片行。

**Tech Stack:** C++17、CMake、CTest、系统 SQLite。测试是独立可执行文件，不引入第三方测试框架。

## Global Constraints

- 用户能看见的行为以需求第 7 节和第 20 节为准。目录和依赖以架构第 6.3 节为准。本计划只改 `kernel/project`、`storage/sqlite` 和对应测试。
- 数据模型里的虚拟副本只有三件事：指向原图、序号、自己的参数文档（需求第 20 节）。参数文档编号是调用方交来的不透明字符串。项目库不打开、不复制、不删除参数文档文件（架构第 6.3 节：参数文档是否存在，不由项目库解释）。
- 计费在账本。项目库只提供「是否共用一张原图」。不新增余额、张数或扣减函数（C-41、B-110）。
- 可以单独把副本放进回收站，原图和其他副本不动。原图进入回收站时，还没在回收站的副本用同一次时钟一起进入；已经在回收站的副本保留原来的删除时间（需求第 7 节、B-112）。
- 恢复副本时，若原图在回收站，原图一起离开回收站。其他副本不动。项目自己还在回收站时，这次恢复整次失败，一行都不改（与已经实现的部分还原相同）。
- 恢复原图、恢复项目，都不把副本带出回收站。需求只写了「恢复任一副本时原图一并恢复」和「恢复项目时恢复其中被删图片」，没有写反方向。
- 原图索引被清掉时，指向它的副本索引一并清掉。只删索引，不调用删除端口，不删用户磁盘上的原图。副本单独到期时，只清该副本的索引。30 天仍是 2592000 秒（C-42）。
- 序号是该原图当前副本里的最大序号加 1；还没有副本时为 1。删掉最大序号后，下一个副本可以再次使用这个空出来的序号。不另建计数器。
- 路径、项目名、文档编号都用参数绑定，不拼进 SQL（S-01）。
- `kernel/` 不包含 Qt 头，不包含 `sqlite3.h`。不调用 `system`。不改命令入口，不发明命令名字符串。
- 日志沿用模块名 `project`。本计划的新路径不写日志，也不记录文档编号和凭证。

本计划明确不做：继承调整的字节复制、星级、旗标、色标、筛选、聚合显示、右键文案、快捷键、导入解码、缩略图、`kernel-cli`、预设、账本扣张、导出队列、生成、GPU、Qt 壳、窗口。`kernel-cli` 仍要等预设和导出队列各自完成之后。T-12、T-16、T-17、T-20、T-21 保持未实现。

---

### Task 1: 副本指向原图，并保存自己的文档编号

**Files:**
- Modify: `kernel/project/include/pixcake/project/records.hpp`
- Modify: `kernel/project/include/pixcake/project/project_store.hpp`
- Modify: `kernel/project/include/pixcake/project/project_library.hpp`
- Modify: `kernel/project/src/project_library.cpp`
- Modify: `storage/sqlite/src/sqlite_project_store.cpp`
- Modify: `storage/sqlite/include/pixcake/storage/sqlite_project_store.hpp`
- Modify: `tests/kernel/project/test_project_paths.cpp`
- Create: `tests/kernel/project/test_virtual_copy.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `ProjectLibrary::create_project`、`add_photo`、`trash_photo`、`trash_project`
- Produces: `ProjectLibrary::create_virtual_copy(photo_id, document_id)`、`load_virtual_copy`、`list_virtual_copies`。记录字段是 `id`、`project_id`、`source_photo_id`、`sequence`、`document_id`。

- [x] **Step 1: Write the failing test**

`tests/kernel/project/test_virtual_copy.cpp` 断言：

- 一张原图连续创建两份副本。图片数仍是 1。第一份 `source_photo_id` 是这张原图，序号是 1，文档编号是 `doc-1`。第二份序号是 2，文档编号是 `a'b`。
- 创建过程不调用删除端口。假文件系统里的原图字节和另一份参数文件字节都不变。
- 文档编号为空、文档编号重复、原图不存在、原图已在回收站、项目已在回收站：都返回状态冲突，副本数不变。
- 关掉存储再打开，两份副本还在，序号和文档编号不变。

`tests/kernel/project/test_project_paths.cpp` 里「不认识的库版本」从 2 改成 3：版本仍是 3，`marker` 表还在。同一文件不新写虚拟副本断言。

另在 `test_virtual_copy.cpp` 断言：用旧的三张表建成 `user_version = 1` 的库，写入一条项目和一条图片后，再用当前存储打开。图片路径和凭证不变，`user_version` 变成 2，并能创建副本。

- [x] **Step 2: Run test to verify it fails**

Run: `cmake -S . -B build && cmake --build build --target test_virtual_copy && ctest --test-dir build -R '^test_virtual_copy$' --output-on-failure`

Expected: 编译失败，因为还没有 `create_virtual_copy`。

- [x] **Step 3: Write minimal implementation**

`records.hpp` 增加：

```cpp
inline constexpr char kTrashVirtualCopy[] = "虚拟副本";

struct VirtualCopyRow {
  std::int64_t id = 0;
  std::int64_t project_id = 0;
  std::int64_t source_photo_id = 0;
  std::int64_t sequence = 0;
  std::string document_id;
};

struct VirtualCopyDraft {
  std::int64_t project_id = 0;
  std::int64_t source_photo_id = 0;
  std::int64_t sequence = 0;
  std::string document_id;
};
```

表，加进新建库的 schema，也作为版本 1 的追加语句：

```sql
CREATE TABLE IF NOT EXISTS virtual_copy (
  id INTEGER PRIMARY KEY,
  project_id INTEGER NOT NULL,
  source_photo_id INTEGER NOT NULL,
  sequence INTEGER NOT NULL,
  document_id TEXT NOT NULL,
  FOREIGN KEY(project_id) REFERENCES project(id),
  FOREIGN KEY(source_photo_id) REFERENCES photo(id)
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_virtual_copy_document ON virtual_copy(document_id);
CREATE UNIQUE INDEX IF NOT EXISTS idx_virtual_copy_sequence
  ON virtual_copy(source_photo_id, sequence);
```

打开规则：

- 版本 0：建全部表，`user_version` 写成 2。
- 版本 1：只执行上面的追加语句，再把 `user_version` 写成 2。不改 `project`、`photo`、`trash` 的行。
- 版本 2：直接打开。
- 其他版本：返回状态冲突，关闭连接，不执行建表。

`create_virtual_copy`：原图必须存在，原图和它所在项目都不在回收站，文档编号非空且还没有副本使用。序号取该原图已有副本的最大序号加 1，没有则为 1。`project_id` 用原图自己的项目。不调用文件系统，不调用删除端口。

`list_virtual_copies` 按序号升序。存储方法按现有 `insert_photo` 的绑定方式写，不拼接用户字符串。

`ProjectLibrary` 的类注释写上：副本指向原图，并保存自己的参数文档编号；这里只回答是否共用原图，不减张数；编号是不透明字符串，不打开参数文档。

- [x] **Step 4: Run test to verify it passes**

Run: `cmake --build build && ctest --test-dir build -R '^(test_virtual_copy|test_project_paths)$' --output-on-failure`

Expected: 这两个测试通过。

- [x] **Step 5: Commit**

```bash
git add kernel/project storage/sqlite tests/kernel/project/test_virtual_copy.cpp tests/kernel/project/test_project_paths.cpp tests/CMakeLists.txt docs/superpowers/plans/2026-11-02-kernel-virtual-copy.md
git commit -m "$(cat <<'EOF'
feat: record a virtual copy against its original photo

EOF
)"
```

### Task 2: 是否共用一张原图

**Files:**
- Modify: `kernel/project/include/pixcake/project/project_library.hpp`
- Modify: `kernel/project/src/project_library.cpp`
- Modify: `tests/kernel/project/test_virtual_copy.cpp`

**Interfaces:**
- Consumes: `load_photo`、`load_virtual_copy`
- Produces: `ProjectLibrary::shares_original(left_kind, left_id, right_kind, right_id, shares)`

- [x] **Step 1: Write the failing test**

断言：

- 原图和它的两份副本，两两 `shares_original` 都为真。种类用 `图片` 和 `虚拟副本`。
- 另一张原图和这些条目都为假。同一张原图和自己为真。
- 种类不是这两种、编号不存在：返回状态冲突，并且不把输出改成真。
- `project` 目录的源文件里没有「扣张」这两个字，也没有余额字段。测试只调用 `shares_original`，不存在扣减函数。

- [x] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_virtual_copy && ctest --test-dir build -R '^test_virtual_copy$' --output-on-failure`

Expected: 编译失败，因为还没有 `shares_original`。

- [x] **Step 3: Write minimal implementation**

`图片` 的原图就是它自己的编号。`虚拟副本` 的原图是 `source_photo_id`。两边原图编号相同则为真。不读回收站，不读文件系统，不写库。失败时把 `shares` 设为假。

- [x] **Step 4: Run test to verify it passes**

Expected: `test_virtual_copy` 通过。

- [x] **Step 5: Commit**

```bash
git add kernel/project tests/kernel/project/test_virtual_copy.cpp docs/superpowers/plans/2026-11-02-kernel-virtual-copy.md
git commit -m "$(cat <<'EOF'
feat: report whether library entries share one original

EOF
)"
```

### Task 3: 删除时的连带关系

**Files:**
- Modify: `kernel/project/include/pixcake/project/project_library.hpp`
- Modify: `kernel/project/include/pixcake/project/project_store.hpp`
- Modify: `kernel/project/src/project_library.cpp`
- Modify: `storage/sqlite/src/sqlite_project_store.cpp`
- Modify: `storage/sqlite/include/pixcake/storage/sqlite_project_store.hpp`
- Modify: `tests/kernel/project/test_virtual_copy.cpp`

**Interfaces:**
- Consumes: `trash_photo`、`trash_project`、`delete_source_file`、`purge_expired_trash`、`erase_trashed_project`
- Produces: `ProjectLibrary::trash_virtual_copy`

- [x] **Step 1: Write the failing test**

使用假时钟和假删除端口。断言：

- 单独丢弃一份副本后，原图和其他副本都不在回收站。再丢一次返回状态冲突，删除时间不变。
- 原图进入回收站时，尚未在回收站的副本使用同一个删除时间。已经在回收站的副本保留旧时间。
- 项目进入回收站时同样带走副本。已经在回收站的副本保留旧时间。
- `delete_source_file` 成功时，删除端口只收到原图那一个路径，原图和副本进入回收站，原图 presence 为 `原文件缺失`。权限不足时结果码是权限不足，`notice` 是 `磁盘文件还在`，原图字节还在，presence 仍是 `可编辑`，原图和副本仍进入回收站。原图已经在回收站时，副本的删除时间不变。
- 副本删除时间加 2591999 秒再 `startup`，副本还在，原图文件还在，删除端口调用次数是 0。加到 2592000 秒再 `startup`，这份副本索引没了，原图索引和原图文件还在。
- 原图索引到期被清掉时，它的副本索引也没了，原图文件还在，删除端口调用次数是 0。
- 彻底删除回收站中的项目后，副本索引没了，原图文件还在。

- [x] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_virtual_copy && ctest --test-dir build -R '^test_virtual_copy$' --output-on-failure`

Expected: 编译失败，因为还没有 `trash_virtual_copy`。

- [x] **Step 3: Write minimal implementation**

`trash_virtual_copy` 只给这一份副本写回收站行，主体是 `虚拟副本`。不写原图的回收站行。

`trash_photo`、`trash_project`，以及 `delete_source_file` 在原图本次新进入回收站时，把该原图尚未在回收站的副本一并写入，时间用同一次时钟。这些写入放在同一个事务里。原图本次没有新进入回收站时，不改副本的删除时间。

`purge_expired_trash` 认识 `虚拟副本`。到期的副本只删自己的回收站行和副本行。到期的原图在删除图片行之前，先删掉指向它的全部副本行和这些副本的回收站行。到期的项目走 `erase_project_index`：先删该项目的副本回收站行和副本行，再删图片。不调用删除端口。

不认识的回收站主体仍返回状态冲突，并且不删已有行。

- [x] **Step 4: Run test to verify it passes**

Run: `cmake --build build && ctest --test-dir build -R '^(test_virtual_copy|test_recycle_bin)$' --output-on-failure`

Expected: 两个测试通过。

- [x] **Step 5: Commit**

```bash
git add kernel/project storage/sqlite tests/kernel/project/test_virtual_copy.cpp docs/superpowers/plans/2026-11-02-kernel-virtual-copy.md
git commit -m "$(cat <<'EOF'
feat: trash virtual copies together with their original

EOF
)"
```

### Task 4: 恢复时的连带关系

**Files:**
- Modify: `kernel/project/include/pixcake/project/project_library.hpp`
- Modify: `kernel/project/src/project_library.cpp`
- Modify: `tests/kernel/project/test_virtual_copy.cpp`

**Interfaces:**
- Consumes: `restore_project`、`restore_photos`、回收站行
- Produces: `ProjectLibrary::restore_virtual_copies`

- [ ] **Step 1: Write the failing test**

断言：

- 原图和两份副本都在回收站时，只恢复第一份副本。第一份副本和原图离开回收站。第二份副本的删除时间不变。
- 原图本来就不在回收站时，恢复副本只让这份副本离开回收站。
- 项目还在回收站时，恢复副本返回状态冲突，原图、副本和项目的删除时间都不变。
- 空列表、不存在的副本、不在回收站的副本：返回状态冲突，已有删除时间不变。一次调用里混进一个不存在的编号时，另一份本来可以恢复的副本也不离开回收站。
- 只恢复原图时，副本仍留在回收站，删除时间不变。
- 恢复项目后，原图离开回收站，副本仍留在回收站。

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_virtual_copy && ctest --test-dir build -R '^test_virtual_copy$' --output-on-failure`

Expected: 编译失败，因为还没有 `restore_virtual_copies`。

- [ ] **Step 3: Write minimal implementation**

`restore_virtual_copies` 先检查每一份副本都存在、都在回收站、所在项目不在回收站。任一不满足则整次返回状态冲突，不写。通过后在一个事务里删掉这些副本的回收站行；原图若在回收站，也删掉原图的回收站行。不删其他副本的回收站行，不清项目的 `deleted_at`。

不修改 `restore_photos` 和 `restore_project` 去连带恢复副本。

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`

Expected: 全部通过，包含 `arch_dependencies`。`kernel/project` 源文件没有 `sqlite3.h`、`QOpenGL` 和 Qt 头。

- [ ] **Step 5: Commit**

```bash
git add kernel/project tests/kernel/project/test_virtual_copy.cpp docs/superpowers/plans/2026-11-02-kernel-virtual-copy.md
git commit -m "$(cat <<'EOF'
feat: restoring a virtual copy brings its original back

EOF
)"
```
