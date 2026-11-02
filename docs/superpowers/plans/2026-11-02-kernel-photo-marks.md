# 图片标记 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在图片行上保存星级、旗标和色标，并按需求第 6.1 节接受或拒绝这些值。

**Architecture:** 三列在 `storage/sqlite` 的 `photo` 表。读写在 `kernel/project`。标记不是参数文档，项目库不打开参数文件。虚拟副本表不加这三列。`user_version` 从 2 升到 3：版本 2 的旧库只追加这三列，不改已有路径和凭证。

**Tech Stack:** C++17、CMake、CTest、系统 SQLite。测试是独立可执行文件，不引入第三方测试框架。

## Global Constraints

- 星级是 0 到 5 的整数。0 表示取消。旗标只有三态：`选取`、`排除`、空字符串。空字符串表示移除。色标只有 `红`、`黄`、`绿`、`蓝`、`紫` 或空字符串。空字符串表示取消色标（需求第 6.1 节、B-28）。
- 不在范围内的值返回状态冲突，已有标记不变。紫色是合法色标。按键、`6` 到 `9` 再按一次取消、紫色没有数字键，都由壳层处理；本计划不发明命令名，也不读取键盘。
- 多张图片可以一次写成同一个标记。空列表返回状态冲突，不写任何行（B-15）。列表里有一个不存在的图片编号时，整次返回状态冲突，已经能改的图片也不改。
- 新图片的星级是 0，旗标和色标是空。版本 1 和版本 2 的旧图片打开后也是这三个默认值，路径、凭证和 presence 不变。
- 虚拟副本的记录仍只有指向、序号和参数文档编号。不把标记复制进副本表（需求第 20 节）。
- 标记用参数绑定，不拼进 SQL（S-01）。不调用删除端口，不改参数文档字节。
- `kernel/` 不包含 Qt 头，不包含 `sqlite3.h`。不调用 `system`。
- 这三列各有一个索引，供以后的筛选和排序使用。本计划不实现筛选、排序和视图（P-12 只要求字段和索引在，不要求本计划做出筛选结果）。

本计划明确不做：筛选交集、排序、显示状态、图库视图、快捷键、右键、聚合、预设、账本、导出队列、导入解码、修图状态、导出状态、格式、构图、拍摄时间、Qt 壳、`kernel-cli`。T-12、T-16、T-17、T-20、T-21 保持未实现。

---

### Task 1: 图片行带上三列默认标记

**Files:**
- Modify: `kernel/project/include/pixcake/project/records.hpp`
- Modify: `storage/sqlite/src/sqlite_project_store.cpp`
- Modify: `tests/kernel/project/test_virtual_copy.cpp`
- Modify: `tests/kernel/project/test_project_paths.cpp`
- Create: `tests/kernel/project/test_photo_marks.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `ProjectLibrary::add_photo`、`load_photo`
- Produces: `PhotoRow::rating`、`PhotoRow::flag`、`PhotoRow::color_label`。新建库的 `user_version` 是 3。

- [x] **Step 1: Write the failing test**

`tests/kernel/project/test_photo_marks.cpp` 断言：

- 新加的图片星级是 0，旗标和色标是空。`user_version` 是 3。
- 手工建成 `user_version = 2` 的库，照片表没有这三列，但已有一条图片。当前存储打开后版本变成 3，路径和凭证不变，三个标记是默认值。

`test_virtual_copy.cpp` 里两处打开后的版本从 2 改成 3。版本 1 的旧图片在断言里加上星级 0、旗标空、色标空。

`test_project_paths.cpp` 里不认识的库版本从 3 改成 4。版本仍是 4，`marker` 表还在。

- [x] **Step 2: Run test to verify it fails**

Run: `cmake -S . -B build && cmake --build build --target test_photo_marks && ctest --test-dir build -R '^test_photo_marks$' --output-on-failure`

Expected: 编译失败，因为 `PhotoRow` 还没有 `rating`。

- [x] **Step 3: Write minimal implementation**

`PhotoRow` 增加 `int rating = 0`、`std::string flag`、`std::string color_label`。

新建库的 `photo` 表在 `presence` 后增加：

```sql
rating INTEGER NOT NULL DEFAULT 0,
flag TEXT NOT NULL DEFAULT '',
color_label TEXT NOT NULL DEFAULT '',
```

并加上三个索引：`idx_photo_rating`、`idx_photo_flag`、`idx_photo_color`。

版本 1 和版本 2 只执行：

```sql
ALTER TABLE photo ADD COLUMN rating INTEGER NOT NULL DEFAULT 0;
ALTER TABLE photo ADD COLUMN flag TEXT NOT NULL DEFAULT '';
ALTER TABLE photo ADD COLUMN color_label TEXT NOT NULL DEFAULT '';
CREATE INDEX IF NOT EXISTS idx_photo_rating ON photo(rating);
CREATE INDEX IF NOT EXISTS idx_photo_flag ON photo(flag);
CREATE INDEX IF NOT EXISTS idx_photo_color ON photo(color_label);
```

打开规则：

- 版本 0：建全部表，`user_version` 写成 3。
- 版本 1：追加副本表和上面的标记语句，`user_version` 写成 3。不改已有项目行和图片的路径、凭证、presence。
- 版本 2：只执行标记语句，`user_version` 写成 3。
- 版本 3：直接打开。
- 其他版本：返回状态冲突，关闭连接，不执行建表。

`read_photo` 和两条 `SELECT` 读出这三列。`INSERT` 仍不写这三列，用默认值。

- [x] **Step 4: Run test to verify it passes**

Run: `cmake --build build && ctest --test-dir build -R '^(test_photo_marks|test_virtual_copy|test_project_paths)$' --output-on-failure`

Expected: 这三个测试通过。

- [x] **Step 5: Commit**

```bash
git add kernel/project/include/pixcake/project/records.hpp storage/sqlite/src/sqlite_project_store.cpp tests/kernel/project/test_photo_marks.cpp tests/kernel/project/test_virtual_copy.cpp tests/kernel/project/test_project_paths.cpp tests/CMakeLists.txt docs/superpowers/plans/2026-11-02-kernel-photo-marks.md
git commit -m "$(cat <<'EOF'
feat: store default star, flag, and color on each photo

EOF
)"
```

### Task 2: 接受或拒绝一次标记

**Files:**
- Modify: `kernel/project/include/pixcake/project/records.hpp`
- Modify: `kernel/project/include/pixcake/project/project_store.hpp`
- Modify: `kernel/project/include/pixcake/project/project_library.hpp`
- Modify: `kernel/project/src/project_library.cpp`
- Modify: `storage/sqlite/include/pixcake/storage/sqlite_project_store.hpp`
- Modify: `storage/sqlite/src/sqlite_project_store.cpp`
- Modify: `tests/kernel/project/test_photo_marks.cpp`

**Interfaces:**
- Consumes: `load_photo`、`in_transaction`
- Produces: `ProjectLibrary::set_photo_ratings`、`set_photo_flags`、`set_photo_colors`

- [ ] **Step 1: Write the failing test**

断言：

- 两张图片一次设为星级 5。另一张图片仍是 0。星级 0 可以写回。6 和 -1 返回状态冲突，原来的 5 还在。
- 旗标写成 `选取` 后再写成 `排除`，只保留 `排除`。空字符串可以写回。`无` 返回状态冲突，旗标仍是 `排除`。
- 色标 `紫` 可以写入。`青` 返回状态冲突。空字符串可以取消色标。
- 空列表、以及列表里混进一个不存在的编号：返回状态冲突。同一次调用里另一张本来可以改的图片保持原标记。
- 假文件系统里预先放好的参数文件字节不变，`writes` 仍是空，删除端口调用次数是 0。
- `kernel/project` 的源文件不包含 `edit_document.hpp`。

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_photo_marks && ctest --test-dir build -R '^test_photo_marks$' --output-on-failure`

Expected: 编译失败，因为还没有 `set_photo_ratings`。

- [ ] **Step 3: Write minimal implementation**

`records.hpp` 增加：

```cpp
inline constexpr int kRatingMin = 0;
inline constexpr int kRatingMax = 5;
inline constexpr char kFlagPick[] = "选取";
inline constexpr char kFlagExclude[] = "排除";
inline constexpr char kColorRed[] = "红";
inline constexpr char kColorYellow[] = "黄";
inline constexpr char kColorGreen[] = "绿";
inline constexpr char kColorBlue[] = "蓝";
inline constexpr char kColorPurple[] = "紫";
```

三个存储方法按 `update_photo_presence` 的绑定方式更新对应列。

`set_photo_ratings`、`set_photo_flags`、`set_photo_colors` 先检查值。星级不在 0 到 5、旗标不是 `选取`、`排除` 或空、色标不是五种颜色或空、编号列表为空，都返回状态冲突。通过后在一个事务里逐张 `load_photo`，缺一张就让事务回滚。全部存在后再更新。不读回收站，不调用文件系统。

类注释写上：星级、旗标和色标记在图片上。虚拟副本不另存这三列。

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`

Expected: 全部通过，包含 `arch_dependencies`。

- [ ] **Step 5: Commit**

```bash
git add kernel/project storage/sqlite tests/kernel/project/test_photo_marks.cpp docs/superpowers/plans/2026-11-02-kernel-photo-marks.md
git commit -m "$(cat <<'EOF'
feat: accept or reject a photo star, flag, and color

EOF
)"
```
