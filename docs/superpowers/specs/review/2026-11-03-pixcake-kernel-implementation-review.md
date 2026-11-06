# 审核记录：2026-11-03，已落地的内核计划

**结论：打回重写。** 最先要挡住三类损失：误删用户文件、重复扣费，以及最后一次成功保存的编辑或项目变得不可用。独立构建的 22 个 CTest 全部通过；下面的反例来自当前工作区的真实 SQLite、临时目录及注明的故障注入。部分现有测试断言的正是错误行为。

**审查基线：** `kernel-project-library`，HEAD `2a54249`，含审查开始时工作区未提交的图层羽化限制、相应测试和规范改动。对照[审核总则](2026-11-02-pixcake-review-index.md)、[阶段需求](../2026-11-02-pixcake-desktop-phase1-design.md)、[架构](../2026-11-02-pixcake-kernel-architecture-design.md)。没有修改实现、既有测试、计划或清单。

**范围：** [14 份计划](../../plans/)对应的精修文档核心、项目库、图片标记、虚拟副本、复制已提交文档、单图历史、四类预设、批量同步、账本、导出队列、图层文档、工作区展示、工作区手册文案、能力注册表。工作区是页面数据，能力注册表是元数据；没有将它们当成已交付的桌面端或绘制效果。已实现入口违反合同记「不通过」，尚无负责模块记「未实现」。多项合同只对明确标出的本轮子路径给出通过，其他部分单列。

## 打回项与复审条件

| 项 | 合同与责任位置 | 触发、实际结果和后果 | 复审要看到的行为 |
| --- | --- | --- | --- |
| R01 双扣点 | C-47、B-63/B-64、P-23；架构 §6.11、§10–11；[LedgerBook::persist](../../../../kernel/ledger/src/ledger_book.cpp#L59) | 初始 100 点，同一份 SQLite 上开两个 LedgerBook，都看到报价 30 的 T 在排队。两者依次调用 `start_generating("T")`，都返回成功。重开有两条 start，余额 **40**，应为 **70**。另起 100 点账本，同一笔触在两个句柄各提交一次，余额 **80**，应为 **90**。不需要两个线程同时运行。 | 在同一数据库事务内按最新状态判断、追加和更新缓存，持久事件能拒绝重复。只锁单个 C++ 对象或重算缓存不能修复事件。 |
| R02 导出越界 | S-09、C-50；[absolute_destination / enqueue](../../../../kernel/exportq/src/export_queue.cpp#L110) | 在临时目录选择 selected，任务给出 selected/../outside.jpg，入队、写入都成功，文件实际出现在所选目录之外。恢复时直接使用事件中的路径，也不重新验证。 | 持有用户选择的根或目录句柄，约束入队和重放的实际解析结果（含目录软链）；不能只校验绝对路径前缀。 |
| R03 导出误删、误认 | C-48、B-99、O-22/O-35；架构 §11；[recover_one](../../../../kernel/exportq/src/export_queue.cpp#L529)、[commit_product](../../../../kernel/exportq/src/export_queue.cpp#L365) | ① 换名失败、临时文件尚在；用户把另一张好图放到目标路径，恢复返回成功、任务失败，**好图被删除**。现有 test_rename_failure_and_crash_points 还要求删掉它。② 新任务开始前就存在同字节文件：不写成品，只在看到它之后补两条事件，即返回成功并产生成品事实。 | 提交必须能证明文件属于该任务；用不覆盖发布及可验证的文件身份处理碰撞。只凭路径和内容校验和不能取得所有权；不应为已有文件补造开始事件、触发结算。 |
| R04 图层文件升级 | C-16、B-33、O-21/O-27；[decode_layer](../../../../kernel/layerdoc/src/layer_document.cpp#L146) | 用 HEAD 版本实际建背景和人物图层、加蒙版、设羽化 1001、保存。当前工作区打开这个校验正确的 PXLD v1：返回文档损坏、文件移入 isolate、无备份时只见 **0 个图层**。不是要求新编辑允许 1001。 | 新编辑仍拒绝 101；读取已写出的 v1 时迁移或单独停用该字段，保留其他图层、生成记录和可恢复原件。不能把整份旧文件判坏。 |
| R05 历史分支修订倒退 | C-23/C-54、B-60/B-73；架构 §6.4、§6.8；[HistoryLog::select](../../../../kernel/history/src/history_log.cpp#L48) | 提交到 r3 → 选 r1 → 改曝光并提交。磁盘文件从 **r3 变成 r2**；随后 record_committed 返回状态冲突，文件已经写了，历史却没记这一笔。 | 历史选中的内容与当前写入代数分开；下一次成功提交必须高于 r3。不能在历史拒绝追加后仍报告写盘成功。 |
| R06 原图和副本串历史 | C-41/C-54；需求 §7、§8.3；[HistoryLog::record_committed](../../../../kernel/history/src/history_log.cpp#L10)、[copy_committed](../../../../kernel/editdoc/src/edit_document.cpp#L358) | 副本文件复制了原图的 image_id，HistoryLog 以它作唯一分组键。实测原、复制品各自有文件，副本曝光 2.00 的历史可成功选到曝光 1.00 的原图内存文档上。 | 分开计费用的原图身份和编辑实例身份。保留副本继承参数的行为，历史查询、提交和任务目标按各自身份区分。 |
| R07 写回清掉不可读片段 | C-17/C-25/C-26、B-84/B-85、O-34；[commit_fragment_replacement](../../../../kernel/editdoc/src/edit_document.cpp#L698)、[commit_baked_reset](../../../../kernel/editdoc/src/edit_document.cpp#L742) | 可读上限 v1、目标 color.global 已是 v99。通过 apply_checked 写 v1，返回成功，重开前 v99 字节已被替换。空来源还会直接删除它。色彩预设同样会删除未在预设中提供的旧 detail；烘焙会删掉全部能力记录，包括未知标识。 | 版本和未知字段保护要覆盖预览以外的每条写路；已禁用片段原文须保留。不能仅让滑杆不可点，或把不可理解的片段当作已烘焙。 |
| R08 导出的修订不可找回 | C-49、B-66、P-10；架构 §6.4、§6.12；[ExportJob](../../../../kernel/exportq/include/pixcake/exportq/export_queue.hpp#L14) | 入队 r1，继续改到 r3，再重开队列。队列中只有整数 1 和设置，磁盘正式文件 r3、备份 r2；没有 r1 正文、不可变引用或副本的文档标识。失败重试无法用该任务复原当时的参数。 | 入队时持有能在重启后解析的不可变文档快照（或版本化引用），测试比较真实参数，而不只是比较整数 1。 |
| R09 一个导出堵住整队 | B-68、O-17；[ExportBook::open](../../../../kernel/exportq/src/export_book.cpp#L47)、[临时文件换名](../../../../kernel/exportq/src/export_queue.cpp#L426) | 目标目录不存在，正常提交返回输入输出失败，状态停在「提交到目标路径」。重开又尝试同一换名，open 失败、不交出队列；一个目标有效的排队任务也无法取出。跨卷 rename 遇到 EXDEV 同样不会因重试而成功。 | 将单任务目的地故障与事件库故障分开；失败项可说明和重试，其余项可继续。跨卷仍须保证目标不出现半张成品。 |
| R10 加速绕过订阅 | C-47；需求 §17.3；[CreditLedger::start_generating](../../../../kernel/ledger/src/credit_ledger.cpp#L210) | 大师精选同一非高算力工具，未订阅、已用满 3 次。第 4 次普通启动被拒；选择加速后扣 5 点并成功进入生成中。 | 先验证使用资格，再决定加速价格；不能把已付加速费当作已订阅。 |
| R11 忙碌时仍能烘焙 | C-28/C-29、B-70；需求 §16、§21；[apply_to_original](../../../../kernel/layerdoc/src/layer_document.cpp#L805) | set_batch_design(true) 后，传同一张图及正确修订，仍能更新原精修，返回成功、清掉片段并落盘。 | 在实际写入口拒绝占用并保证修订、片段、文件均不变。只禁用界面按钮无效。 |
| R12 重试找错图 | C-45、B-68；[retry_failed](../../../../kernel/sync/src/sync_batch.cpp#L132) | A 成功、B 失败，用户重排为 B、A 再重试。函数按下标读旧结果：B 标成功但仍 r0；A 又写成下一修订。 | 以稳定目标身份对应旧结果；或在写前验证整个目标序列没变。不能只比两个数组的长度。 |
| R13 编辑入口没有按文档串行 | C-21/C-23、B-60/B-62；架构 §6.2；[CommandGate::submit](../../../../kernel/command/src/command_gate.cpp#L15) | 对同一路径上两个 r0 副本各建一个 gate，依次提交不同曝光，两次都成功、都写 r1，第二次覆盖第一次。极速开关打开时，直接走现有 PresetLibrary::apply 也成功写下一修订。 | 同一份文档须共享状态与提交串行区；已实现的预设、同步等编辑动作接入同一命令检查。不能在各窗口或每个 gate 上各加一把锁。 |
| R14 权限、回滚与时限 | C-10/C-13/C-61、B-52/B-111/B-115；[open_durable_file](../../../../kernel/persist/src/atomic_file.cpp#L111)、[atomic_replace](../../../../kernel/persist/src/atomic_file.cpp#L95)、[导出校验](../../../../kernel/exportq/src/export_queue.cpp#L437) | 临时目录撤去搜索权限：probe_file 报权限不足，而 EditDocument::load 报 absent 且无结果码。导出换名后注入一次读权限错误，完整成品被删并改报 I/O 失败。另测目录刷盘失败后恢复备份换名也失败：正式路径空、旧内容只在 .bak。注入 2080 ms 刷文件延迟，提交 **2085 ms 后仍报成功**。 | 区分不存在、未能校验、已证实损坏；校验读错时保留文件、原因和重试机会。回滚也须检查并持久化结果；超过时限不能直接成功或在发布后仅改返回码。 |
| R15 范围与元数据 | C-31/C-36–C-39/C-43/C-44，B-04/B-20/B-27/B-31/B-32；[命令提交](../../../../kernel/editdoc/src/edit_document.cpp#L660)、[注册表](../../../../kernel/capability/src/capability_registry.cpp#L14)、[预设校验](../../../../kernel/preset/src/preset_library.cpp#L91)、[设置网格](../../../../kernel/workspace/src/workspace_display.cpp#L200) | 曝光 99.00 成功进文件；导出接受 quality=99。注册表说 color.style 不能进预设，但同一内核实际接受；祛边也被标为不能进预设，与需求 §13.1 相反。20 个分解形式的 é 被拒，非法 UTF-8 C0 AF 反被接受。网格设为 4 再输入 0，返回冲突、仍为 4；需求 §6.4 默认开启的三个图库显示项却无初值。 | 数值按所属能力/导出模式校验，再提交；元数据与预设使用同一资格源；字符按可见字素计；网格夹到 1–7。手册未列默认值不能推翻需求已给出的值。 |
| R16 批量成本 | P-01；[SqliteExportLog::write_cache](../../../../storage/sqlite/src/sqlite_export_log.cpp#L325)、[write_marks](../../../../kernel/project/src/project_library.cpp#L31) | 2000 个导出任务仅入队，实测 SQLite 对 export_task_cache 做 **2,001,000 次 INSERT**，未开始渲染；每次事件删全表再重插。一次给 2000 张图打星，实测 **2000 次单图 SELECT + 2000 次 UPDATE**。每次滑杆命令读取极速开关还会重放全队列。 | 正常追加只更新受影响的缓存行；全量折叠留给打开/修复，开关读取应为常量成本。项目按批校验并更新。不能只扩大缓存或把循环搬到后台。 |
| R17 恢复记录与构建检查 | O-01/O-04/O-07/O-08/O-12、T-07；[ExportEvent](../../../../kernel/exportq/include/pixcake/exportq/export_queue.hpp#L24)、[LogRecord](../../../../kernel/support/include/pixcake/support/log_sink.hpp#L10)、[CTest 注册](../../../../tests/CMakeLists.txt#L97) | 导出失败只保存 fail，重开无从区分磁盘满和权限失败；隔离、阻止重复扣费和自动保存耗时无相应记录。架构脚本只作为 CTest，且第 9 条按目录豁免，目录里的其他目标也被放过。 | 持久记录必要的阶段、原因、耗时和去重结果；构建能因违规目标而失败。不要记录像素或凭证，也不要靠解析错误文案恢复原因。 |
| R18 项目库升级中断后打不开 | B-114、O-25；[SqliteProjectStore::open](../../../../storage/sqlite/src/sqlite_project_store.cpp#L286) | 用现有 v2 测试结构和一张照片，SQLite 授权回调只拒绝第 2 条 ALTER。首次打开报权限不足；取消故障后仍报 I/O 失败。检查库：版本仍为 2，rating 已存在，flag 不存在，照片行还在。下一次总在重复加 rating 处失败。 | 在同一事务内完成全部 DDL 和版本更新；中途失败应能直接重试。不能只把版本号改成 3 来跳过剩下的列。 |
| R19 源文件删除错对象 | S-10/S-11、O-35；需求 §3.3；[delete_source_file](../../../../kernel/project/src/project_library.cpp#L535)、[FileSourceDeleter](../../../../kernel/project/src/file_source_deleter.cpp#L12) | 临时目录 A、B 各有不同的 orig。经指向 A 的目录软链接登记照片和凭证，然后将链接改指 B。删除同一照片 ID 返回成功；**A 的原文件留下，B 的文件消失**。删除接口只传路径，无法使用入库凭证核实对象。 | 删除已确认的源文件时使用平台授权和可验证的文件身份；身份发生变化要拒绝或重新确认。不能只比较路径字符串或重新解析到 B 后继续删。 |
| R20 删除失败但原文件已消失 | C-10/C-61、B-115；需求 §7；[文件删除](../../../../kernel/project/src/project_library.cpp#L535)、[回收站事务](../../../../kernel/project/src/project_library.cpp#L549) | 第二个 SQLite 连接持有写事务；用户执行已确认的从磁盘删除。命令报 I/O 失败，而磁盘原件已消失、回收站无该图片、`source_status` 仍返回成功。和 R19 无关，不需要软链接。 | 删除前持久记录可恢复的意图，或在确定能维护索引前避免不可逆副作用；重启后可完成或明确说明部分成功。仅改报错文案不能恢复一致性。 |
| R21 同批副本分次完成会多扣张数 | C-41/C-48、B-110；需求 §7/§17.1；[settle_exports](../../../../kernel/ledger/src/credit_ledger.cpp#L359) | 一个导出操作预先排入同一原图的 A、B 两个普通成品。A 成功后用队列的成品清单结算；B 成功后再次用 A+B 的清单结算。初始 5 张，实测 **5 → 4 → 3**，同批应为 **4**。账本只存结算过的 export_id，没有用户这次导出的批次身份。 | 持久识别「本次导出 + 原图」，允许分次完成、重试和重启；新发起的另一次导出仍可正常计费。只在一次函数调用里分组或全局永远按原图去重都不对。 |
| R22 隔离后下次打开遗失恢复标志 | B-08/B-100、O-15/O-34；[open_durable_file](../../../../kernel/persist/src/atomic_file.cpp#L111) | 单份损坏的精修文件、没有备份。第一次 load 报文档损坏、明确标记恢复为空、留下 isolate 原件；关闭且不另存，再次 load 却报 absent、无恢复标志。结果与从未编辑过的新图片相同。 | 在不覆盖隔离原件前提下，持久识别已经恢复为空的文档；连续两次打开都能给出正确状态。不能把所有初次打开都报成损坏。 |

以下七张表逐项对照清单。表中的 R 编号指向上表的函数、输入/返回、禁止的补丁及复审条件；测试名在仓库中可直接检索。对于尚无入口的条目，「未实现」是证据，不作为本轮已交付代码的新增缺陷。

## 第 1 关：业务契约

| 编号 | 结论 | 证据 | 差异 |
| --- | --- | --- | --- |
| C-01 | 不通过 | R15；需求 §6.4；test_library_controls | 工作区测试把“默认都开”固定成未设置。 |
| C-02、C-06、C-07（Free 排除部分） | 通过 | capability_registry.cpp / test_pipeline；现有构建目标 | detail 两个阶段不绘制；没有 Free 组的模式、着色器、AI 校准或快捷键实现。白平衡、允许的按键另记未实现。 |
| C-03 | 通过 | ExportQueue::set_fast_export / test_snapshot_and_fast_export_stay | 队列清空不会解除开关；实际写入口绕开开关见 C-21。 |
| C-04、C-30、C-34、C-55、C-62 | 未实现 | 未实现：生成/渲染进程、跳转、任务描述、语义分割执行 | 落层内核类型、工具名和管线表不能证明跨进程行为。 |
| C-05、C-06（白平衡）、C-07（快捷键）、C-27、C-33 | 未实现 | 未实现：参数模式、编译与编辑器 | 黑白档禁用、精准控制切换及合法快捷键均未验收。 |
| C-08 | 未实现 | 未实现：services/config | 测试报价不是默认点数表或配置缓存。 |
| C-09 | 通过 | CreditLedger::start_generating / test_free_queue_switches_and_master_allowance | 两套策略在测试传入 9.3.0 取值后独立生效；实际配置下发见 C-08。 |
| C-10、C-13、C-61 | 不通过 | R03、R09、R12、R14 | 误认成品、未写报成功、权限误报缺失或 I/O；结果码标签存在不等于操作正确。 |
| C-11、C-12、C-19 | 未实现 | 未实现：导入解码与网络服务；test_support 仅枚举标签 | 格式、损坏、断网的用户操作没有运行证据。 |
| C-14、C-15、C-20 | 通过 | test_b93_disk_full_keeps_formal_file、test_source_presence、test_free_queue_switches_and_master_allowance | 抽到的写满、已入库路径丢失、余额不足各返对应码；并发及其他 I/O 路径另判。 |
| C-16、C-17 | 不通过 | R04、R07；test_b84_too_new_fragment_is_not_rewritten 只测单值提交 | 旧图层文件误判损坏，批量替换不保留过新片段。 |
| C-18 | 未实现 | ExportQueue::cancel、CreditLedger::cancel 可记录取消；无工作进程返回通路 | 接受取消命令本身的 Ok 不等于被中止操作报告 Cancelled。 |
| C-21、C-22、C-23 | 不通过 | R05、R11、R13；test_b60_simultaneous_commits_serialize_to_one_revision 共享同一 gate | 过期修订、占用和极速门闩在部分公开写入口失效。 |
| C-24 | 通过 | EditDocument::set_preview、encode / test_c24_previews_do_not_write_or_bump_revision | 文档侧中间值仅在内存；渲染槽和画布未实现。 |
| C-25、C-26 | 不通过 | R07、R14；正常格式另见 test_envelope_round_trip | 预设/同步可丢不可读片段；超时写入仍报成功。 |
| C-28、C-29 | 不通过 | R11；test_place_result_shapes_once、test_apply_bakes_the_original_and_a_new_document_has_no_pixels | 形状和正常去重有测试，批量占用仍能写原精修；.tsq 像素及原图左侧登记明确未实现。 |
| C-31 | 不通过 | R15；CapabilityRegistry::phase_one；需求 §10.2、§13.1 | 已交付的能力资格值就有冲突；独立能力实现/完整接口未实现。 |
| C-32 | 未实现 | phase_one_pipeline / test_pipeline 只验证固定阶段表 | 预览与导出共同执行、尺寸策略未实现。 |
| C-35 | 不通过 | capability_registry.cpp:81 validate_phase_one_pipeline；架构 §7.4 | 只查非 detail 的重复 ID，没有依赖边和环检测，未接启动与日志。不能把重复元素当作环检测的覆盖。 |
| C-36、C-37、C-38、C-39 | 不通过 | R15；commit_preview 透传任意字符串，实测 exposure=99.00 | 已有公开提交路径不验证显示范围；各能力模式和轨道未实现。不应在通用信封硬编码所有滑杆。 |
| C-40 | 通过 | ProjectLibrary::add_photo / test_project_paths、test_source_presence | 已建索引只记绝对路径、凭证与元数据，不复制原图；真正的导入格式/可读性筛选未实现。 |
| C-41 | 不通过 | R06、R21；test_creating_a_copy_duplicates_the_committed_document、test_shares_original_without_a_charge | 文件可独立保存，但历史串图；同批原图和副本分次完成时也可能多扣张数。 |
| C-42 | 通过 | ProjectLibrary::purge_expired_trash / test_under_thirty_days_can_be_restored、test_expired_index_is_removed_without_deleting_the_file | 正好到期清索引，不删源文件。 |
| C-43、C-44 | 不通过 | R07、R15；test_c43_color_replaces_and_ai_color_stacks、test_c44_name_counts_characters_not_bytes | 普通四类替换和覆盖预检有证据；遗留片段、预设能力资格和名称边界不满足。 |
| C-45 | 不通过 | R07、R12；test_checked_sync_copies_only_selected_fragments | 基本勾选及跳过成功项能跑，重排或高版本目标会错写。 |
| C-46 | 通过 | CreditLedger / test_balances_stay_separate_and_queue_cancel_is_free、test_generating_cancel_and_crash_do_not_refund | 两套余额和基本状态分开；扣费正确性见 C-47。 |
| C-47、C-48 | 不通过 | R01、R03、R10、R21；test_product_facts_match_one_settlement | 可能双扣点、绕订阅；非本任务成品也可产生可扣张事实，同批分次结算还会多扣张。 |
| C-49、C-50 | 不通过 | R02、R03、R08、R09、R14 | 整数修订不构成耐久快照；发布、重启和所有权有反例。 |
| C-51、C-52、C-53、C-56、C-57、C-58、C-59 | 未实现 | 未实现：自定义工具栏/收藏、样片库、快捷键、kernel-cli、GPU 外观和画布 | 页面文字、保留的阶段号不算执行入口。 |
| C-54 | 不通过 | R05、R06；test_b101_reopen_folds_to_last_save | 重启折叠有证据，回退再改和副本历史失败。 |
| C-60 | 不通过 | R17；tests/arch/check_dependencies.cmake 第 9 条；arch_dependencies 当前通过 | 现有目标未链接 Qt，但依赖禁令不能按目标名阻止所有违规。 |

## 第 2 关：边界条件

| 编号 | 结论 | 证据 | 差异 |
| --- | --- | --- | --- |
| B-01 | 通过 | CommandGate::submit / test_b01_missing_fields_do_not_write | 缺字段时不写盘、不增加修订。 |
| B-02、B-05、B-06、B-07、B-09、B-10、B-13、B-14 | 未实现 | 未实现：导入/搜索、账号、配置缓存、素材引用解析、人物检测及生成工具参数、整项目导出入口 | 工作区有展示文字；没有这些操作的执行结果。 |
| B-03（队列入参） | 通过 | ExportQueue::enqueue / test_commit_once_and_reject_a_different_destination；导出探针的空队列对照 | 没有原图编号不能建任务；空选择的壳层命令尚未实现。 |
| B-04、B-20、B-21（预设名） | 不通过 | R15；PresetLibrary::save / test_c44_name_counts_characters_not_bytes | 全角空格可以保存；把一个可见字拆成多个码位会错误触顶。普通 21 个汉字的拒绝另有覆盖。 |
| B-08 | 不通过 | R22；test_b08_posix_truncate_without_backup_is_marked_empty | 只覆盖首次打开；第二次打开丢失恢复标志。 |
| B-11（队列） | 通过 | ExportQueue::request_print / test_cancel_removes_temp_and_print_is_unavailable | 没有打印机时仍有成品，并明确返回不可用事实；没有实际打印。 |
| B-12（页面） | 通过 | WorkspaceDisplay::logged_out / test_workspace_display | 登录页拒绝 open_project 和 show 切页；服务侧登录会话与权限检查未实现。 |
| B-15、B-28（标记） | 通过 | ProjectLibrary::set_photo_ratings/flags/colors / test_marks_accept_only_the_documented_values | 空选择、值非法、批中坏 ID 均不改其他图。筛选、排序与键盘切换未实现。 |
| B-21（工具栏/收藏色）、B-22、B-23、B-24、B-25、B-26 | 未实现 | 未实现：自定义布局、样片库、证件照素材导入、按人数生成 | 不能用工作区标签或测试报价替代。 |
| B-27、B-31、B-32 | 不通过 | R15；test_library_controls | 曝光 99.00、品质 99 可进入已提交文档/任务；网格越界留在旧值而不夹到端点。 |
| B-29（历史、图层）、B-30 | 通过 | test_b29_bad_index_keeps_revision_and_neighbors、test_blend_opacity_lock_and_index、test_only_the_expired_photo_is_purged、test_under_thirty_days_can_be_restored | 有无副作用和临界点断言；蒙版子项索引未实现。 |
| B-33（已实现的编辑入口） | 通过 | LayerDocument::set_opacity/set_mask_feather/set_selection / test_group_mask_selection_and_batch_design、test_blend_opacity_lock_and_index | 新编辑拒绝非法值；旧文件读入回归单列 R04。追色、裁剪参数模式未实现。 |
| B-34、B-35、B-36、B-40、B-41、B-43、B-44、B-45、B-46、B-48 | 未实现 | 未实现：解码/挑图、全图蒙版、裁剪、画布、控件换算、按人报价、像素缓冲与 IPC | 创意图层蒙版不等于精修全图蒙版。 |
| B-37（树和已声明的 ID） | 通过 | mask_sync_rows、capability_sync_rows、apply_checked / test_tree_reads_attributes_from_owners、test_checked_sync_copies_only_selected_fragments | 局部追色和画笔行禁用并有原因，直接勾选拒绝。解析实际画笔蒙版、从不透明局部片段中排除它，尚未实现。 |
| B-42、B-47（导出设置入口） | 不通过 | ExportJob::settings_snapshot、ExportQueue::enqueue；test_cancel_removes_temp_and_print_is_unavailable 只传字符串 | 负责校验排版行列、目标大小的模式未实现；队列可持久化非法设置，不能据此宣称已校验。 |
| B-50、B-51、B-53（停止工作进程）、B-54、B-55 | 未实现 | 未实现：net、线程调度与进程取消/超时；test_generating_cancel_and_crash_do_not_refund 只验证账本 | 没有对在途生成或渲染的超时测试。 |
| B-52、B-60、B-62、B-63、B-64、B-66、B-68、B-70、B-73 | 不通过 | R01、R05、R07–R14；test_b60_simultaneous_commits_serialize_to_one_revision、test_reopen_repairs_cache_and_finishes_rename | 共享一个 gate 的测试挡不住分别打开的同一文件；加锁、去重、批量失败、忙碌门闩、时限和快照均有反例。 |
| B-61（文档） | 通过 | CommandGate::submit / test_b61_commit_writes_latest_preview；EditDocument / test_b61_commit_keeps_latest_display_value | 单一 gate 内保存最后预览值；跨 gate 见 B-60。 |
| B-65、B-67、B-69、B-71、B-72、B-74、B-80、B-81、B-82、B-86 | 未实现 | 未实现：生成结果确认/清理、单实例、壳层模态、渲染槽、账号、配置；test_place_result_shapes_once 只验落层去重 | 同一结果两次落层的单模块测试不能证明取消后迟到、崩溃时跨进程补落层。 |
| B-83（已解析的输入） | 通过 | PresetLibrary::import_preset / test_overwrite_and_import_do_not_change_old_rows | 调用方给出新版/已删除事实时拒绝且保留旧清单；真实口令解析未实现。 |
| B-84、B-85 | 不通过 | R07；test_b84_too_new_fragment_is_not_rewritten、test_b85_unknown_inner_field_survives_commit | 单值修改的保护存在，整片替换/烘焙不具备同一保护。 |
| B-90、B-91 | 未实现 | 未实现：逐图解码、原件损坏识别/导出集成；PosixFileSystem::probe_file 只检查路径和文件类型 | 已入库的路径不存在和文件内容损坏是两种不同场景。 |
| B-92（原图/文档）、B-93（单次写满/拒绝） | 通过 | test_source_presence、test_b93_disk_full_keeps_formal_file、test_b93_permission_denied_keeps_formal_file | 原图移走/放回不改原编辑文件；测试中的写满/权限错误均保留旧正式文件。缩略图未实现；回滚再失败另列。 |
| B-94 | 不通过 | ExportQueue::commit_product 建缓存目录分支；test_disk_full_then_retry_uses_the_snapshot 只把错误注入 write_new | 若 create_directory 返回磁盘满，任务仍为「渲染」，retry 返回状态冲突。见下面独立探针。 |
| B-95、B-96、B-97、B-98（本任务单独正常恢复） | 通过 | test_rename_failure_and_crash_points、test_reopen_repairs_cache_and_finishes_rename；探针在 temp-begin 后/commit-begin 前中断 | 未写回排队；写中正常清理并失败；写完可换名；已确认不重写。路径碰撞、清理本身失败不能沿用此结论。 |
| B-99、B-100 | 不通过 | R03、R04、R22；test_b100_posix_truncate_restores_backup_document | 会删除不属于任务的文件，或把有效旧图层隔离；一般 PXED 截断且有备份可恢复。 |
| B-101（正常落盘）、B-102（拒绝删源）、B-103（账本取消）、B-104（未扣点排队） | 通过 | test_b101_reopen_folds_to_last_save、test_delete_source_file、test_balances_stay_separate_and_queue_cancel_is_free、test_generating_cancel_and_crash_do_not_refund | 有正常重开、明确权限拒绝及纯状态机证据；跨连接扣点、其他删除失败另列。 |
| B-105、B-106 | 未实现 | CreditLedger::crash、LayerDocument::place_result 是分开的；test_reopen_replays_events_and_repairs_cache 明确期望重开仍是「生成中」 | 手动报 crash 能转为失败；实测只留 start 后重开仍是生成中、余额不退。本轮没有辨认已死工作进程并自动收尾、确认文件、补落层的恢复入口。不能在任意读者打开账本时就把活任务判死。 |
| B-107、B-108、B-113、B-117 | 未实现 | 未实现：纯色判断、全身美型、退出询问/终止、预训练模型输出合并 | 占位文字及默认能力表均不算实现。 |
| B-109、B-110、B-111、B-114（库迁移）、B-115 | 不通过 | R03、R14、R18、R20、R21；test_product_facts_match_one_settlement、test_version_two_gains_default_marks | 非本任务成品可触发计费；分次完成可再扣；回滚失败、迁移中断和磁盘/索引分歧没有可靠恢复。应用更新器未实现。 |
| B-112 | 不通过 | [restore_photos](../../../../kernel/project/src/project_library.cpp#L424)、test_restore_photos_fails_while_the_project_is_trashed；需求 §7 | 整项恢复、删原图带副本、恢复副本带原图分别有测试；从回收站进入已删除项目只还原选中图片却整次拒绝。先恢复整个项目会把未选图片也还原。 |
| B-116 | 通过 | EditDocument::encode/decode、atomic_replace / test_envelope_round_trip、test_c17_newer_file_format_is_document_corrupt、test_b100_posix_truncate_restores_backup_document | PXED、小端 TLV、正文校验和、未知记录及临时/备份/隔离位置有证据；此行仅判断布局。 |

## 第 3 关：安全 SafeList

| 编号 | 结论 | 证据 | 差异 |
| --- | --- | --- | --- |
| S-01、S-02 | 通过 | 三个 sqlite 适配器的 Statement/bind_text/bind_blob；PosixFileSystem、FileSourceDeleter；test_project_paths、test_export_queue、test_ledger | 用户文本是绑定参数；移动、删除走文件 API。SQL 模板和表结构是常量。路径授权另见 S-09–S-11。 |
| S-03、S-04、S-05 | 未实现 | 未实现：XMP、CUBE、真实分享口令、配置解码、项目/预设搜索 | 工作区记录搜索框用途，不能证明解析器安全。 |
| S-06 | 不通过 | R15；PresetLibrary::save / test_c44_name_counts_characters_not_bytes | 长度按码位，非法 UTF-8 和全角空白可通过；其他自由文本的负责模块未实现。 |
| S-07、S-08、S-12、S-13、S-14、S-15 | 未实现 | 未实现：真实导入/解码、IPC、配置服务、可自定义快捷键 | add_photo 是规则明确的索引入口，不是导入文件验真器。 |
| S-09 | 不通过 | R02；[temp_path](../../../../kernel/exportq/src/export_queue.cpp#L203)、[恢复清理](../../../../kernel/exportq/src/export_queue.cpp#L493) | 入队可越过目标根；另外，将导出 ID 为 `../owned_by_user` 的 temp-begin 日志交给重放，实测恢复成功并删除了 cache/exports **外面**的测试文件。恢复必须像入队一样验证路径/编号。 |
| S-10、S-11 | 不通过 | R19、R20；ProjectLibrary::delete_source_file / test_project_paths、test_file_source_deleter_rejects_relative_paths | 凭证能保存，正常权限拒绝有处理，但没有用于绑定待删原件。两个路径都是绝对路径也可能删错对象。 |
| S-16、S-17、S-20 | 通过 | 本轮源码、测试和仓库文本文档的常见密钥/私钥/真实手机号模式扫描；test_ledger、test_source_presence | 未发现这些模式的真实凭证；计费测试只用内存或临时 SQLite，不接支付。这是对本仓库样本的结论。 |
| S-18、S-19、S-25、S-27、S-28、S-29、S-30、S-31、S-32 | 未实现 | 未实现：会话、分享服务、进程入口及单实例锁、更新器、系统浏览器和商家授权 | 不能从登录页面模型推导出服务鉴权。账本多句柄问题 R01 即使有单实例壳也仍须处理。 |
| S-21、S-22、S-23、S-24（现有诊断调用） | 通过 | CommandGate::submit、ProjectLibrary::note / test_source_presence、test_command_gate | 现有调用只传结果、路径、修订或能力标识；测试用凭证不出现在日志。崩溃转储和账号服务未实现。 |
| S-26 | 通过 | CreditLedger、LedgerBook、ExportQueue::products / test_balances_stay_separate_and_queue_cancel_is_free、test_product_facts_match_one_settlement | 现有余额变更归账本，队列只给成品事实；重复或错误扣费另见 R01/R03/R21。 |

## 第 4 关：性能

| 编号 | 结论 | 证据 | 差异 |
| --- | --- | --- | --- |
| P-01 | 不通过 | R16；实际 SQLite update_hook / trace_v2 | 批量打星 2000 次逐图 SELECT、2000 次 UPDATE。2000 次导出入队重插 2,001,000 行任务缓存，尚未开始渲染。 |
| P-02（索引） | 通过 | SqliteProjectStore::list_photos/list_projects / test_project_paths、test_source_presence | SELECT 列中没有参数正文或原图像素；含访问凭证元数据。缩略图绘制未实现。 |
| P-03、P-04、P-05、P-09、P-13、P-14、P-15、P-17、P-19、P-20 | 未实现 | 未实现：缩略图缓存、渲染/生成/界面调度、配置网络、样片列表 | 没有真实窗口帧时或 GPU 资源测量。不能据此声称滑杆流畅或大项目已验收。 |
| P-06（预览写入） | 通过 | test_c24_previews_do_not_write_or_bump_revision、test_c24_preview_is_not_written | 单个预览值只改内存；门闩读却会折叠整段导出日志，见 R16。渲染线程响应未测。 |
| P-07、P-22 | 不通过 | R05、R13；CommandGate::submit / test_b60_simultaneous_commits_serialize_to_one_revision | 锁随 gate 实例，而不是随文档；两个 gate 对同一路径均可发布 r1。 |
| P-08、P-23 | 不通过 | R01；LedgerBook::persist、SqliteLedgerLog::append；双句柄探针 | 事务只包已算好的事件和缓存，不串行校验任务状态。 |
| P-10 | 不通过 | R08；ExportJob、test_disk_full_then_retry_uses_the_snapshot | 保存的修订号无法解析到原修订正文；全分辨率编码与 4000 边长预览未实现。 |
| P-11 | 不通过 | [临时写失败分支](../../../../kernel/exportq/src/export_queue.cpp#L398)、[终态跳过恢复](../../../../kernel/exportq/src/export_queue.cpp#L481)；export_boundaries 探针 | 注入刷临时文件磁盘满、清理权限不足，状态仍是失败；重开 recover 成功，临时文件仍在。应保留待清理状态，不能吞掉 unlink 错误。 |
| P-12（已有索引） | 通过 | SqliteProjectStore 的 idx_photo_project/rating/flag/color，list_photos / test_new_photo_has_empty_marks、test_version_two_gains_default_marks；下方 SQL 计划抽查 | 只判已有项目/标记字段；格式、修图/导出状态和时间排序的查询及索引未实现。 |
| P-16、P-18、P-21 | 不通过 | R08、R13、R14；test_b62_fast_export_stays_until_the_switch_is_turned_off | 超过两秒仍成功，预设写入口可绕过极速开关。单 gate 的预览不进文件，但跨入口没有同一份文档主人。 |
| P-24、P-25 | 未实现 | 未实现：取消/超时/结果确认的跨进程关联、桌面单实例管理 | 单个账本的取消和单份图层文档的结果编号去重各有测试；跨句柄的反例见 R01。 |

## 第 5 关：代码质量

| 编号 | 结论 | 证据 | 差异 |
| --- | --- | --- | --- |
| Q-01、Q-11 | 不通过 | [工作区页面区域](../../../../kernel/workspace/src/workspace_display.cpp#L98)、[手册文案](../../../../kernel/workspace/src/guide_copy.cpp#L31)、[管线数据](../../../../kernel/capability/src/capability_registry.cpp#L54) / test_regions、test_guide_copy、test_pipeline；架构 §5、§7.4 | 目标有 pixcake_ 前缀，但布局区域、角落、工具顺序和页面文案在 kernel/workspace，架构分给 ui/shell；默认顺序放在 capability 而非 pipeline。改页面布局不应要求改内核。 |
| Q-02、Q-03、Q-04 | 通过 | CapabilityRegistry::phase_one、EditDocument::commit_preview、ExportQueue::enqueue、CreditLedger::settle_exports / test_capability_registry、test_envelope_round_trip | 持久能力 ID 与中文标签分开；公开写操作可区分预览、提交、入队和结算。 |
| Q-05 | 不通过 | result_code_label / test_support；R09、R14、R20 | 枚举语义齐全，真实读权限错误却变成不存在/一般 I/O，失败也不能说明实际副作用。 |
| Q-06、Q-20、Q-21、Q-23、Q-24、Q-25 | 未实现 | 未实现：能力参数模式、按模式绘制的控件、语义分类表、快捷键、手动增量及 IPC | 不把缺少第二套表当成已经实现单一数据源；R15 是现有行为的问题。 |
| Q-07、Q-08、Q-09、Q-10、Q-12、Q-13 | 通过 | HistoryLog、CreditLedger、ExportQueue、LayerDocument、CommandGate / test_history、test_ledger、test_export_queue、test_layer_document、arch_dependencies | 就职责分工，撤销、扣费、成品发布、混合模式各有主人；现有类没有同时改文档和画控件，gate 不做 SQL 或图像计算。功能正确性分别另判。 |
| Q-14、Q-16、Q-18、Q-19（公开注释） | 通过 | EditDocument、CreditLedger、atomic_file.hpp、ExportQueue 头文件；test_c24_preview_is_not_written、test_c10_success_only_after_directory_sync、test_snapshot_and_fast_export_stay | 注释在执行规则的类型旁边；R01/R05/R14 说明实现未完全守住，不能由注释替代验收。 |
| Q-15、Q-17 | 不通过 | EditDocument、CapabilityRegistry 的公开头；test_pipeline 只验证不绘制 | 缺少显示值存储/换算边界及 Free 旧字节只保留不绘制的公开说明。先修正 R07，再补说明。 |
| Q-22（已实现账本） | 通过 | FreeQueuePolicy、CreditLedger::start_generating/stroke / test_free_queue_switches_and_master_allowance、test_accelerate_is_charged_once_and_strokes_dedupe | 测试传入报价/两套开关；10 点是架构明确的逐笔规则；实际默认价格表未实现。 |
| Q-26、Q-27、Q-28、Q-29、Q-32、Q-33 | 不适用 | 架构 §3.1/§17 的禁止项；检查构建目标、现有源码和 test_pipeline | 没有能力动态库、native 绘制、五官拆库、云协议、界面全分辨率执行或 Free 着色器；后续阶段的入口文案不是深层实现。 |
| Q-30 | 通过 | HistoryLog::record_committed/reopen_folded / test_b101_reopen_folds_to_last_save、test_history_sources_do_not_touch_sqlite_or_qt | 撤销链在内存里；磁盘保存单文档。 |
| Q-31 | 不通过 | R08；ExportJob / test_snapshot_and_fast_export_stay | 没有发现现有渲染器故意跟随新滑杆；但任务只保存数字，重启后不能保证按旧修订执行。 |
