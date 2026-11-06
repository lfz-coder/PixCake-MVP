# 第七轮复审条件 2 与 4 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

日期：2026-11-06
状态：进行中
分支：`codex/desktop-official-layout`，基线 `9d03627`

**Goal:** 关闭第七轮条件 2（修改都经命令入口）和条件 4（第五轮条件 4–7：S-25、S-10；C-30、C-33、Q-25；Q-11、Q-12、O-13–O-15；T-02、T-03、T-09）。

**Architecture:** 按依赖顺序九个任务，每个任务一次提交。内核先行（命令、追色、IPC、会话、书签、结果呈现），壳层随后（拆 `main.cpp`、接会话与提示），测试补齐收尾。上位文档没写到的设计，先补进架构再写代码。

**Tech Stack:** C++17、CMake/CTest、SQLite、Qt 6（只在 `apps/shell`）、macOS CoreFoundation（只在 `platform/macos`）。

**Spec:** 需求第 79、103、118、204、223–226、897 行与第 21 节；架构第 34、215、299、354、394–396、505–511、594–599、664 行；七份清单的对应行；第七轮记录“打回项与复审条件”。

## Global Constraints

- 用户发起的修改都从命令入口进入（架构 215）；导出进程和生成进程不接受这组命令。
- `ipc` 只有消息结构，消息体用和磁盘相同的记录编码（架构 354）；四个进程不各写一套字段（C-30、Q-25）。
- 生成进程不链接 Qt、不链接 OpenGL，不调用图层文档写入接口（架构 507、552）。
- 内核不读窗口系统判断登录态；登录态来自账号服务和本机会话（安全清单 S-25 说明）。
- 原图只用绝对路径加书签或安全范围（需求 118、S-10）；平台代码只在 `platform/macos`。
- 不改另一会话的文件：`apps/renderd/CMakeLists.txt`、`apps/renderd/*segmentation*`、`tests/CMakeLists.txt` 里的 `test_segmentation_contract`、`:memory:.ses`、分割相关文档。提交 `tests/CMakeLists.txt` 时以 HEAD 版本为底只加本任务的块。
- 每步先写失败测试；变异验证；干净工作树全量 CTest、`git diff --check`、架构门禁通过后提交。

## Review Focus

1. 命令入口的预设、同步、粘贴、应用至精修：失败（过期修订、极速导出、写盘失败）时不通知 `CommitListener`、文档不变。
2. 追色精准控制来回切换：两边数据都还在；同一帧只有一边参与编译。
3. IPC 解码遇到截断、版本过新、未知字段：拒绝或保留，不崩溃；生成进程崩溃按不退点失败。
4. 未登录时项目、导出、生成、扣点、扣张命令都返回拒绝，不只靠界面隐藏；发布构建没有 `--page` 直达。
5. 书签失效或过期：标“原文件缺失”或要求重新授权，参数文档保留。

## Tasks

- [x] **Task 1（条件 2）**：`CommandGate` 增加 `apply_preset`、`paste_fragments`、`apply_layers_to_original` 三个命令，批量同步经 `SyncCommand` 对每个目标走各自的命令入口；成功后通知 `CommitListener`。预设库、同步、图层的直接提交函数降为命令入口内部调用。测试 `test_c23_preset_apply_marks_edited`、`test_c23_paste_marks_edited`、`test_c23_sync_marks_each_target_edited`、`test_c23_apply_to_original_marks_edited`、`test_c23_failed_command_does_not_notify`。
- [x] **Task 2（C-33）**：追色精准控制。`color.match` 增加枚举参数 `precise`（`off`/`on`，默认 `off`）。精准控制写回的值作为“追色写回”记录写进色彩能力片段（与用户参数记录分开的记录类型），由命令入口写入（架构 394）。编译：`precise=on` 时程度通道不画、写回记录参与；`off` 时写回记录不参与、程度通道画。切换只改 `precise`。上位文档：架构 396 后补记录类型说明。测试 `test_c33_precise_on_draws_written_back_values_only`、`test_c33_precise_off_draws_amount_only`、`test_c33_toggle_keeps_both_sides`。
- [x] **Task 3（C-30、Q-25）**：`kernel/ipc` 统一消息：预览请求/取消/帧头改用文档记录编码（`fragment_codec` 的记录格式）；新增导出进度、生成进度、取消、心跳；渲染、导出服务只用这些结构。新增 `apps/generated`（无 Qt、无 GL）：读取任务、写结果、追加账本事件；崩溃按不退点失败。门禁：`pixcake_app_generated` 不链接 Qt、GL、`pixcake_layerdoc`。测试 `test_c30_messages_round_trip_through_record_codec`、`test_c30_truncated_or_newer_message_rejected`、`test_q25_services_use_ipc_structs`（门禁）、`test_c30_generated_crash_fails_without_refund`。
- [ ] **Task 4（S-25）**：命令入口与项目、导出、生成、账本的写操作要求已登录会话（接口 `SessionGuard`，由 `services/account` 实现）；未登录返回 `NotSignedIn`。壳层：`--page` 只在 `PIXCAKE_DEV_PAGES` 构建可用；发布构建由会话决定首页（需求 79、103）。测试 `test_s25_signed_out_commands_rejected`、`test_s25_restored_session_enters_home`、门禁检查发布构建没有 `--page`。
- [ ] **Task 5（S-10）**：原图访问凭据。`kernel/project` 只接受带书签的绝对路径；`SourceAccess` 接口解析书签（失效、过期、路径变化），`platform/macos` 用安全范围书签实现，内核测试用替身。书签失效标“原文件缺失”，参数文档保留（需求 118）。测试 `test_s10_admission_requires_bookmark`、`test_s10_stale_bookmark_marks_missing_and_keeps_document`、`test_s10_resolved_path_change_updates_record`。
- [ ] **Task 6（O-13–O-15）**：结果呈现链路。内核 `kernel/support` 增加 `UserNotice`（结果码 + 场景 → 文案 + 停留页面）；登录流程 `services/account/LoginFlow`（替身远端）区分验证码错误、过期、未收到（可重发）、断网，停留登录页（需求 897、223）；导入逐张结果（O-14）；原图缺失、文档恢复为空、能力需要更新三态分开（O-15）。壳层显示提示。测试 `test_o13_*`、`test_o14_import_reports_each_file`、`test_o15_three_states_distinct`。
- [ ] **Task 7（Q-11、Q-12）**：拆 `apps/shell/main.cpp`：每页一个文件（登录、主页、图库、精修、创意），控件构建与文档修改分开（页面类只画，动作经控制器调用命令入口）；页面文字单一来源（`ui/shell` 的文案表，壳层与 `kernel/workspace` 不再各写一份）；没有超过 160 字符的行。测试：门禁统计 `apps/shell` 行宽；`test_q11_page_copy_single_source`（同一文案只在文案表出现）。
- [ ] **Task 8（T-03）**：替身场景：第三方坏返回、超时、生成进程崩溃（与 Task 3、6 共用替身）。测试 `test_t03_*`。
- [ ] **Task 9（T-02、T-09）**：第 2 关审查范围内每条 B 行至少一条用例，测试名带编号；已有用例改名补编号。

## Rulings

- Task 1：直接提交函数（`PresetLibrary::apply`、`paste_committed`、`apply_checked`、`apply_to_original`）保留为公开函数，由命令入口调用；“只经命令入口”改由架构门禁规则 17 保证（`apps/shell`、`ui` 不得出现这些调用和 `PresetLibrary`）。理由：内核测试与所属模块仍需直接验证它们；把它们藏起来要改 4 个模块的公开头。代价：内核里别的模块仍可绕过命令入口，门禁只看壳层和界面。
- Task 1：命令入口自己的修订号检查是第二道防线，去掉它测试不变红（所属模块也按修订号拒绝），保留不删。
- Task 1：批量同步不做“一个命令处理多张目标”，而是每张目标图的命令入口各调一次 `sync_from`；通知按图发出。代价：调用方要为每张目标图持有一个命令入口。
- Task 1：已经烘焙过再“应用至精修”返回成功但没有新修订，不通知“已编辑”。
- Task 2：需求第 397 行“开启精准控制后全图程度仍是 0–200”与架构第 396 行“程度通道挂起”看似冲突。按架构：打开时程度不参与编译，但滑杆仍可调、值保留（不加禁用规则）。代价：如果产品要求精准控制下程度缩放写回值，需要再定一条编译规则。
- Task 2：有效值的取法放在 `EditDocument::effective_parameter`，开关和目标由 `capability_registry` 的 `match_writeback_switch()` 声明，编辑文档不写死能力名。渲染读取方与禁用判断都改用有效值。界面面板（`ui/panels`）还没有读取文档值，接入时同样用有效值。
- Task 2：精准控制打开时，用户提交一个“已有追色写回”的参数，改写回记录；没有写回的参数仍写用户记录。代价：关闭精准控制后，用户在精准控制下对写回参数做的修改随写回一起挂起。
- Task 2：追色分析（AI）本身不在本任务；命令入口 `write_match_result` 接收追色能力给出的写回值。
- Task 3：IPC 编码复用 `kernel/capability` 的 `fragment_codec`（`pixcake_ipc` 私有链接 `pixcake_capability`），没有把记录编码挪进 `kernel/support`。理由：架构第 354 行要求“和磁盘相同的文档编码”，复用同一实现最直接。代价：`ipc` 多了一条对能力接口库的链接。
- Task 3：生成进程在本进程里持有账本副本；主进程写入的“取消”要靠同一个账本对象或重新打开才能看到。测试里取消发生在同一个对象上。跨进程刷新（每次远端返回后重读事件）留到接上真实传输时做。
- Task 3：服务里“任务必须是排队中”的检查是第二道防线，去掉它测试不变红（账本本身拒绝开始未排队的任务），保留。
- Task 3：导出进程的 `ExportResult` 是进程内结果，不是进程间消息；跨进程报告用 `ipc::ExportProgressMessage`，接线留到导出进程接上传输时。
