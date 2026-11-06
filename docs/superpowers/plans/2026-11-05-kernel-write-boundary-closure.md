# 内核写入边界收口 Implementation Plan

> 本计划接在 `2026-11-05-official-display-gap-closure.md` 之后，只处理七关复审中仍然不通过、且属于现有内核持久化写入边界的问题。展示线由 Cursor 继续处理，本计划不修改 `apps/shell`、`ui` 或展示资源。

## 目标

让现有内核的写入行为有一个可验证的边界：同一路径的文档写入串行、极速导出门闩不能被其它现有写入口绕过、文件写入支持进程间排他锁、参数范围来自统一模式、源文件删除失败时可以在启动时恢复。导入、真实解码、渲染、AI、真实登录、云服务、完整导出进程和 `kernel-cli` 继续留在未实现范围。

## 范围与不做事项

- 修改范围仅限 `kernel/`、`storage/`（必要的 SQLite 迁移）、`tests/` 和本计划/验收记录。
- 不增加 Qt、OpenGL、HTTP、推理运行时或第三方测试框架。
- 不把预览中间值写入磁盘；只有提交和已有的文档/图层/源文件索引写入需要经过边界。
- 远程服务、导入内容验真、渲染/生成进程、真实账号和导出业务仍由后续计划负责。
- 不重置或覆盖展示线已有修改；不修改 `apps/shell`、`ui/shell`、展示资源和展示计划。

## 设计

`pixcake_persist` 提供 `WriteScope`：按正式文件路径共享进程内递归锁，并由 `PosixFileSystem` 对 `<path>.lock` 持有 `flock`，从而让不同进程不能同时提交同一份文档。极速导出门闩在 `CommandGate` 注册到同一路径，所有现有文档/图层提交在进入写区时检查门闩。`atomic_replace` 作为最后一道保护，即使调用方遗漏外层 scope，也不会把两个进程的临时文件提交交错。

参数模式由 `CapabilityRegistry` 唯一提供。现有白平衡、影调、AI 色彩程度、蒙版羽化和裁剪角度范围以 `ParameterMode` 记录；验证函数只查询模式，不再维护第二套分支常量。没有模式的能力参数继续拒绝正式提交。

源文件删除使用两阶段状态：先把照片标记为“删除待完成”并写入回收站意图，再执行磁盘删除；完成状态写成“原文件缺失”。如果磁盘副作用后索引更新失败，启动时的 presence 扫描会根据文件事实把待完成状态收敛到“原文件缺失”或“可编辑”，不会把删除中的照片当作已完成。

## 执行步骤

### Task 1：建立共享写锁和跨进程文档锁

**Files:**

- Create: `kernel/persist/include/pixcake/persist/write_lock.hpp`
- Create: `kernel/persist/src/write_lock.cpp`
- Modify: `kernel/persist/include/pixcake/persist/file_system.hpp`
- Modify: `kernel/persist/src/posix_file_system.cpp`
- Modify: `kernel/persist/src/atomic_file.cpp`
- Modify: `kernel/persist/CMakeLists.txt`
- Modify: `kernel/command/include/pixcake/command/command_gate.hpp`
- Modify: `kernel/command/src/command_gate.cpp`

- [x] `FileSystem` 增加可选的写锁对象；默认文件系统保持进程内行为，POSIX 实现使用 `flock`，锁失败返回 `StateConflict`。
- [x] `WriteScope` 共享同一路径的递归 mutex，并检查已注册的 `FastExportLatch`；`CommandGate` 注册/注销门闩并使用 scope 包住整个提交。
- [x] `EditDocument` 的 `write_snapshot`、`copy_committed`、三种 commit，`LayerDocument::save` 和 `atomic_replace` 经过 scope；已有调用者不改变业务结果码。
- [x] 增加两个 POSIX 文件系统实例争抢同一路径的失败测试，以及同进程不同写入口互斥测试。

### Task 2：把参数范围收进唯一模式

**Files:**

- Modify: `kernel/capability/include/pixcake/capability/capability_registry.hpp`
- Modify: `kernel/capability/src/capability_registry.cpp`
- Modify: `kernel/editdoc/src/edit_document.cpp`
- Modify: `tests/kernel/capability/test_capability_registry.cpp`
- Modify: `tests/kernel/editdoc/test_edit_document.cpp`

- [x] 增加 `ParameterMode`、模式查找和阶段一模式表。
- [x] 覆盖色温、色调、曝光、影调、AI 追色/风格程度、蒙版羽化、裁剪角度以及合同明确的眼神光位置/旋转和光影均匀明度；默认值只作为模式数据暴露，不在控件中复制。
- [x] `validate_parameter_display_value` 只查询模式，未知参数、越界值和错误步进拒绝正式提交；保留预览中间值不写盘的行为。
- [x] 为每个模式增加上下界、步进、默认值和未知参数反例测试。

### Task 3：收紧所有现有文档/图层提交入口

**Files:**

- Modify: `kernel/editdoc/src/edit_document.cpp`
- Modify: `kernel/layerdoc/src/layer_document.cpp`
- Modify: `kernel/preset/src/preset_library.cpp`
- Modify: `kernel/sync/src/sync_batch.cpp`
- Modify: `tests/kernel/command/test_command_gate.cpp`
- Modify: `tests/kernel/editdoc/test_edit_document.cpp`
- Modify: `tests/kernel/layerdoc/test_layer_document.cpp`
- Modify: `tests/kernel/preset/test_preset_library.cpp`
- Modify: `tests/kernel/sync/test_sync.cpp`

- [x] 证明预设、粘贴、同步、应用图层和直接文档提交共享同一路径写区，极速导出开启时都返回 `FastExportBusy`。
- [x] 写入前统一检查修订号、编辑实例身份和批量设计状态；失败不修改内存文档、不产生临时文件。
- [x] 保留未知/过新片段，拒绝没有参数模式的新增正式参数；不删除用户无法理解的记录。
- [x] 增加跨入口、跨句柄和写失败后的内容/字节不变测试。

### Task 4：源文件删除的可恢复写入边界

**Files:**

- Modify: `kernel/project/include/pixcake/project/records.hpp`
- Modify: `kernel/project/src/project_library.cpp`
- Modify: `storage/sqlite/src/sqlite_project_store.cpp`
- Modify: `tests/kernel/project/test_recycle_bin.cpp`
- Modify: `tests/kernel/project/test_source_presence.cpp`

- [x] 增加“删除待完成”presence 状态并兼容旧库；删除意图和状态先落库，再执行带身份校验的磁盘删除。
- [x] 删除完成、文件缺失、权限失败和索引更新失败分别留下可解释状态；启动扫描可重试/收敛，不删除错误的同路径新文件。
- [x] 增加删除器失败、索引更新失败和重启恢复测试。

### Task 5：验证与记录

**Files:**

- Create: `docs/superpowers/evidence/2026-11-05-kernel-write-boundary-closure.md`

- [x] 运行独立 Debug 构建、全量 CTest、架构门禁、POSIX 锁测试和 `git diff --check`。
- [x] 记录已关闭的 C-10/C-21/C-23/C-25/C-26、C-35/C-36–C-39、B-27/B-32/B-33/B-44、S-10/S-11 的内核子路径；明确未实现的导入、渲染、AI、登录、导出、IPC 和 `kernel-cli` 不在本计划完成定义内。
- [x] 不提交展示线文件，不创建远程 PR；本地工作树保留用户与 Cursor 的其他修改。

## 完成定义

只有 Task 1–5 的实现、失败反例和回归验证全部完成，才把本计划标记为完成。若仍有跨进程协议、导入、渲染、AI、登录或导出进程缺失，只在验收记录中标为后续未实现，不把它们伪装成内核写入边界已完成。
