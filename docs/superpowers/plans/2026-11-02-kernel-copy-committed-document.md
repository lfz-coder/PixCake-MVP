# 复制已提交参数文档 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 创建虚拟副本时，把原图已提交的参数文档写成副本自己的那一份文件。

**Architecture:** 复制在 `kernel/editdoc`。它读取源路径上已经落盘的文档，再用现有的五步替换写到调用方给出的另一条路径。项目库仍只保存这条路径作为不透明的文档编号，不打开文件。预览只活在内存里，不会进入副本文件。

**Tech Stack:** C++17、CMake、CTest、系统 SQLite。测试是独立可执行文件，不引入第三方测试框架。

## Global Constraints

- 用户能看见的行为以需求第 7 节为准：副本继承当前全部调整，不额外占一套原文件。本计划里的「当前全部调整」只指已经提交进参数文档的内容。拖动中的预览不写盘，也不能被这份复制带走（架构第 6.4 节、C-24、Q-14）。
- 一张图或一个虚拟副本一份参数文档（架构第 6.4 节、需求第 20 节）。副本文件是另一条路径。源路径和目标路径相同，或任一路径为空，返回状态冲突，不写文件。
- 项目库不解释参数文档是否存在，不打开、不复制、不删除参数文档（架构第 6.3 节）。`create_virtual_copy` 保持原样，只接收调用方交来的文档编号。
- 复制的是耐久打开所接受的已提交字节。正式文件合格就复制它。只有备份合格时，沿用现有耐久打开：备份成为已提交内容，再复制那一份。损坏且没有备份时，不给副本写空文档，也不把空文档伪装成原来的精修（B-08）。
- 信封里的文档版本、图片编号、修订号、片段、蒙版编号、人物属性覆盖、手动增量引用、预设引用和未知记录按原样进入副本。不改写图片编号，不把修订号清零。图片编号仍是原图文档里的那个字符串，不是项目库里的副本行号。
- 写副本文件走架构第 6.14 节的五步替换。磁盘满或权限不足时，副本正式文件不出现半份内容，源文件保持复制前的字节。
- 文档里已经提交的预设引用记录随字节复制。不新建预设模块，不解释谁覆盖谁。
- `kernel/` 不包含 Qt 头，不包含 `sqlite3.h`。`editdoc` 不包含项目库头。不调用 `system`。不改命令入口，不发明命令名。
- 这次不写日志。不记录显示值、文档编号或凭证。

本计划明确不做：星级、旗标、色标、筛选、聚合显示、右键文案、快捷键、预设模块、账本扣张、导出队列、历史步骤、生成、GPU、Qt 壳、窗口、`kernel-cli`。T-12、T-16、T-17、T-20、T-21 保持未实现。

---

### Task 1: 创建副本时复制已提交文档

**Files:**
- Modify: `kernel/editdoc/include/pixcake/editdoc/edit_document.hpp`
- Modify: `kernel/editdoc/src/edit_document.cpp`
- Create: `tests/kernel/editdoc/test_copy_committed_document.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `EditDocument::load`、`EditDocument::write_snapshot`、`ProjectLibrary::create_virtual_copy`
- Produces: `EditDocument::copy_committed(files, source_path, destination_path, limits, copy)`，返回 `LoadResult`

- [x] **Step 1: Write the failing test**

`tests/kernel/editdoc/test_copy_committed_document.cpp` 断言：

- 原图文档已提交曝光 `-1.50`，修订号是 1，图片编号是 `img-1`。内存里另有预览 `drag-final`。未知记录、蒙版编号、手动引用和预设引用 `preset-1` 已在提交文件里。原图像素文件是另一条路径上的三个字节。
- `copy_committed` 写到一条新路径。新文件和源文件字节相同，因此没有 `drag-final`。重新打开后，曝光、修订号、图片编号、校验和、未知记录、蒙版、手动引用和预设引用都与源文件一致。内存里的原图文档仍保留预览。
- 用这条新路径调用 `create_virtual_copy`。图片数仍是 1。副本的 `document_id` 就是这条新路径。原图像素字节不变，删除端口调用次数是 0。
- 之后只提交副本的曝光为 `2.00`。源文件字节不变。再只提交原图的曝光为 `3.00`。副本文件仍是 `2.00`。
- 源路径为空、目标路径为空、两条路径相同：返回状态冲突。源文件字节不变，不产生目标文件、`.tmp` 或 `.bak`。
- 源文件不存在：`absent` 为真，不产生目标文件。输出文档保持调用前的图片编号。
- 源文件是坏字节且没有备份：结果码是文档损坏，`recovered_empty` 为真，不产生目标文件。这是现有耐久打开的隔离，不是给副本写一份空文档。
- 正式文件不在、只有合格备份：`used_backup` 为真，副本文件等于恢复后的已提交内容。
- 写入返回磁盘已满：结果码是磁盘已满，源文件字节不变，目标正式文件不存在。

另断言：`kernel/editdoc` 的源文件不包含 `project_library.hpp`、`sqlite3.h`、`exportq`、`ledger`。`kernel/project` 的源文件不包含 `copy_committed`。

- [x] **Step 2: Run test to verify it fails**

Run: `cmake -S . -B build && cmake --build build --target test_copy_committed_document && ctest --test-dir build -R '^test_copy_committed_document$' --output-on-failure`

Expected: 编译失败，因为还没有 `copy_committed`。

- [x] **Step 3: Write minimal implementation**

`edit_document.hpp` 在 `load` 后面声明：

```cpp
  // 把源路径上已提交的文档写成目标路径上的另一份。内存里的预览不写入。
  // 路径为空或两条路径相同：返回状态冲突，不写文件。
  static LoadResult copy_committed(FileSystem& files, const std::string& source_path,
                                   const std::string& destination_path,
                                   const std::vector<VersionLimit>& limits, EditDocument& copy);
```

`edit_document.cpp`：

```cpp
LoadResult EditDocument::copy_committed(FileSystem& files, const std::string& source_path,
                                        const std::string& destination_path,
                                        const std::vector<VersionLimit>& limits, EditDocument& copy) {
  LoadResult rejected;
  if (source_path.empty() || destination_path.empty() || source_path == destination_path) {
    rejected.code = ResultCode::StateConflict;
    return rejected;
  }
  EditDocument loaded;
  const LoadResult opened = load(loaded, files, source_path, limits);
  if (opened.absent || !opened.code.has_value() || *opened.code != ResultCode::Ok || opened.recovered_empty) {
    return opened;
  }
  const ResultCode saved = loaded.write_snapshot(files, destination_path);
  if (saved != ResultCode::Ok) {
    rejected.code = saved;
    return rejected;
  }
  copy = std::move(loaded);
  LoadResult result;
  result.code = ResultCode::Ok;
  result.used_backup = opened.used_backup;
  return result;
}
```

失败时不改 `copy`。不调用项目库，不改修订号，不读取预览表。

- [x] **Step 4: Run test to verify it passes**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`

Expected: 全部通过，包含 `test_copy_committed_document` 和 `arch_dependencies`。

- [x] **Step 5: Commit**

```bash
git add kernel/editdoc tests/kernel/editdoc/test_copy_committed_document.cpp tests/CMakeLists.txt docs/superpowers/plans/2026-11-02-kernel-copy-committed-document.md
git commit -m "$(cat <<'EOF'
feat: copy the committed edit document for a virtual copy

EOF
)"
```
