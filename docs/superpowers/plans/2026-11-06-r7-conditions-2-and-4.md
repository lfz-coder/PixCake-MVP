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
- [x] **Task 4（S-25）**：命令入口与项目、导出、生成、账本的写操作要求已登录会话（接口 `SessionGuard`，由 `services/account` 实现）；未登录返回 `NotSignedIn`。壳层：`--page` 只在 `PIXCAKE_DEV_PAGES` 构建可用；发布构建由会话决定首页（需求 79、103）。测试 `test_s25_signed_out_commands_rejected`、`test_s25_restored_session_enters_home`、门禁检查发布构建没有 `--page`。
- [x] **Task 5（S-10）**：原图访问凭据。`kernel/project` 只接受带书签的绝对路径；`SourceAccess` 接口解析书签（失效、过期、路径变化），`platform/macos` 用安全范围书签实现，内核测试用替身。书签失效标“原文件缺失”，参数文档保留（需求 118）。测试 `test_s10_admission_requires_bookmark`、`test_s10_stale_bookmark_marks_missing_and_keeps_document`、`test_s10_resolved_path_change_updates_record`。
- [x] **Task 6（O-13–O-15）**：结果呈现链路。内核 `kernel/support` 增加 `UserNotice`（结果码 + 场景 → 文案 + 停留页面）；登录流程 `services/account/LoginFlow`（替身远端）区分验证码错误、过期、未收到（可重发）、断网，停留登录页（需求 897、223）；导入逐张结果（O-14）；原图缺失、文档恢复为空、能力需要更新三态分开（O-15）。壳层显示提示。测试 `test_o13_*`、`test_o14_import_reports_each_file`、`test_o15_three_states_distinct`。
- [x] **Task 7（Q-11、Q-12）**：拆 `apps/shell/main.cpp`：每页一个文件（登录、主页、图库、精修、创意），控件构建与文档修改分开（页面类只画，动作经控制器调用命令入口）；页面文字单一来源（`ui/shell` 的文案表，壳层与 `kernel/workspace` 不再各写一份）；没有超过 160 字符的行。测试：门禁统计 `apps/shell` 行宽；`test_q11_page_copy_single_source`（同一文案只在文案表出现）。
- [x] **Task 8（T-03）**：替身场景：第三方坏返回、超时、生成进程崩溃（与 Task 3、6 共用替身）。测试 `test_t03_*`。
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
- Task 4：未登录不新增结果码，用状态冲突（契约 C-22“当前状态不允许这个命令”）。契约第 2 节要求新增结果码先改清单表；这里语义已覆盖，不改表。提示层（Task 6）按会话区分“请先登录”。
- Task 4：`CommandGate::set_session` 是可选设置，不设置时不查会话（现有内核测试大量构造命令入口）。主进程总是设置。代价：内核里漏设会话的调用方会绕过 S-25；`BusinessCommands` 的会话是构造参数，必须给。
- Task 4：本机会话缓存（需求第 103 行“记住上次登录态”）还没有持久化来源，壳层的账号会话启动时是未登录，首屏是登录页。会话缓存的存放位置先要在上位文档里定。
- Task 4：发布构建门禁：规则 18 检查 `--page` 与 `dev_preview` 只在 `PIXCAKE_DEV_PAGES` 块里；Release 配置下 CTest 有 `test_s25_release_shell_rejects_page`（本次在 Release 构建里跑过：`Unknown option 'page'`）。默认的 Debug 全量 CTest 不含这一条。
- Task 5：书签解析到别的路径（被挪走或改名）时按需求第 118 行标“原文件缺失”，不跟着书签改路径；回到原路径后恢复。代价：macOS 书签能追踪移动，这里有意不用这个能力。
- Task 5：系统不再授予读取（解析失败且不是“文件不在”）时，状态不变、返回权限不足，与已有的 `refresh_source_presence` 对无权读取的处理一致。
- Task 5：非沙盒构建拿不到安全范围书签，`MacSourceAccess` 退回普通书签（首字节 `B`）；沙盒构建用安全范围书签（首字节 `S`）并在读取期间用 `SourceScope` 打开。代价：普通书签不限制进程对其他路径的读取；限制来自沙盒本身。
- Task 5：空访问凭证在 `add_photo` 处拒绝（权限不足）。20 处测试调用原来传空凭证，改为非空测试凭证；`test_project_paths` 里“空凭证可以入库”的断言按 S-10 改为拒绝。
- Task 6：“未收到验证码”是用户动作（`report_not_received`），说明原因并允许重新获取，不自动重发。理由：需求第 223 行只要求可重发；自动重发会多扣短信额度。代价：如果产品要求倒计时后自动重发，需加计时。
- Task 6：`UserNotice` 文案按“场景 + 结果码”取，表在 `kernel/support/src/user_notice.cpp`。文案没有官方原文来源，按需求与运维清单的“用户必须能区分的原因”写；接 test 版实测后可替换，测试只断言互不相同与不空。顺带覆盖 O-16（四种结果文案不混用，`test_o16_notices_do_not_share_text`）。
- Task 6：O-15 的“能力需要更新”以片段版本高于本程序 `phase_one_parameter_modes` 中该能力的最高 schema 版本判断；三种状态可以同时出现，各出一条提示。
- Task 6：壳层这一步只到显示模型（`ShellDisplay::post_notice`/`notices`，换页与登录成功清空）；Qt 控件显示提示随 Task 7 的页面拆分接入。代价：在 Task 7 前界面上还看不见提示。
- Task 7：文案表用 ASCII 键在运行时查（`page_text`、`page_list`、`page_item`），不为几百条文案各起一个编译期常量名。找不到的键返回空并记下，冒烟检查在 `missing_page_copy()` 非空时失败（退出码 6），五个页面的冒烟测试都跑。代价：键拼错要到冒烟测试才发现，编译期不报。
- Task 7：精修色彩面板的显示顺序、显示名和参数绑定合成 `ui/panels` 里的一张表（`refine_color_rows`），绑定查询（`refine_color_binding`、`refine_subgroup_binding`）查同一张表；后期调色复用其分组标题。
- Task 7：`kernel/workspace` 的 `WorkspaceDisplay` 是 `ShellDisplay` 的第二份，只有自己的测试在用，删除；图库显示控制（视图、每行张数、三种显示状态、全效果）移进 `ShellDisplay`，改用条目编号（`grid`、`show-raw` 等）而不是显示文字；区域条目的逐条核对移到 `test_shell_display`。`guide_copy` 保留，缩略图标识字样由文案表引用它。
- Task 7：门禁按“字面量相同”判重复：`ui` 与 `kernel/workspace` 里同一段中文字面量只能在一个文件；两处共用的词放 `ui/shell/shared_words.hpp`（`预设`、`旋转`）。为此 `pixcake_ui_panels` 链接 `pixcake_ui_shell`，`pixcake_ui_shell` 私有链接 `pixcake_workspace`。代价：不同控件恰好同名也要共用一个常量；将来两者文字分开时要拆常量。
- Task 7：拆分前后在五个页面上导出控件树（类名、对象名、无障碍名与说明、提示、启用、文字、图标有无、选中、占位文字、滑杆范围与值、分组标题）逐行比较，差异 0 行（登录 19、主页 51、图库 52、精修 1270、创意 80 行）。导出代码是临时加的，没有提交。
- Task 7：门禁写成 C++ 测试 `test_shell_sources`（按码点数行宽、跳过注释取字面量），不放进 `cmake -P` 规则。页签点击经控制器换页的检查放在壳层冒烟里（`check_tab_navigation`，退出码 7），因为规则 9 不允许测试目标链接 Qt Widgets。变异：页面写中文文案、页面改显示模型、超长行、`ui` 里重复字面量、少一个页面文件、控制器不转发请求，均失败。
- Task 7：Q-11 的“快捷键数据来自同一张表”：壳层还没有快捷键，没有可核对的表；架构里写明实现时放进文案表。窗口在页面上方显示当前页的提示（Task 6 的 Qt 一侧）。
- Task 8：远端回答的像素定为 RGBA8（每像素 4 字节），字节数不等于 宽 × 高 × 4 判坏返回（图片损坏）。这是本任务唯一先红后绿的修复：原来尺寸不符的结果会被写进 creative/ 并记成功。代价：如果真实远端返回编码图片（PNG 等），要改成先解码再核尺寸。
- Task 8：远端失败后点数退不退，需求没有写（只写了崩溃与生成中取消不退点）；新测试不断言失败后的点数，只断言结果码、“失败”状态、没有半个文件、上一份好结果不动、不第二次扣点。
- Task 8：磁盘满和重复提交的行为原来已经成立（新测试一写就过），补的是覆盖；变异“忽略写盘失败”得 2 处失败，“去掉尺寸检查”得 3 处失败。
- Task 8：发现 Task 3 的崩溃测试不稳定（40 次里 8 次失败）：fork 后不 exec 的子进程里打开 SQLite，在 macOS 上随机段错误（信号 11）。改为测试程序用 `posix_spawn` 启动自己（`--crash-child <目录>`）跑子进程角色；改后 60 次 0 失败。另外两个用 fork 的测试（单实例、跨进程提交）只用 POSIX 文件调用，各 40 次 0 失败，不改。之前几次干净工作树全量通过是碰巧。
- Task 8：T-03 列的其余异常（权限不足、文件截断、校验失败、取消）已有替身测试，分别在 editdoc、persist、recovery、project、exportq 的测试里；本任务只补生成链路缺的“尺寸不符”“磁盘满”“重复提交”。
