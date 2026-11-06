# 审核记录：2026-11-03，已实现的内核计划复审

**结论：打回重写。** 导出仍可能覆盖其他文件；取消、订阅资格和结算在部分顺序下会失效；旧版保存的有效图层文件可能被当成损坏。下面的反例均只操作临时文件或测试账本。

**基线与范围：** 分支 `kernel-project-library`，当前 HEAD `4a1e665`（包含 `4d68d5b`、`62cb4dc`、`128fc94` 的账本、导出目标和同步重试修复），并纳入工作区已有的图层羽化、规范和测试修改。覆盖 [14 份已落地的功能计划及修复计划](../../plans/)：文档核心、项目库、图片标记、虚拟副本、复制已提交文档、历史、预设、同步、账本、导出、图层、工作区展示、手册文案、能力注册表。以[总则](2026-11-02-pixcake-review-index.md)、[需求](../2026-11-02-pixcake-desktop-phase1-design.md)和[架构](../2026-11-02-pixcake-kernel-architecture-design.md)裁决。工作区展示是数据模型，能力注册表是元数据，没有将它们计为实际桌面或像素功能。

**与[上一轮记录](2026-11-03-pixcake-kernel-implementation-review.md)的区别：** 复核了真实 SQLite 双连接和文件系统的最小反例；只将仍能重现的行为列为当前问题。R01 的两种重复扣点已挡住：100 点、报价 30，第二次 start 返回状态冲突，重放余额 70、只有一次 start；同一笔触重放余额 90。R10 的普通第四次加速后**启动**现在会被拒绝，但预扣和其他调用顺序仍有问题。R02/R03 中，普通入队的字面 `..` 被拒、预先存在的同内容文件不再被认作成品；换名失败后出现的其他文件也会保留。它们不能替代下列另外几个崩溃点的验证。

## 打回项：触发条件、后果及复审要求

| 编号 | 证据与复现 | 复审要求 |
| --- | --- | --- |
| F01 否决：重放可删除缓存目录以外的文件 | S-09、O-35。[临时路径](../../../../kernel/exportq/src/export_queue.cpp#L218)、[恢复清理](../../../../kernel/exportq/src/export_queue.cpp#L497)。向临时 SQLite 导出事件库写入 ID 为 `../owned_by_user` 的 enqueue → prepare → render → temp-begin。重新打开 ExportBook 返回成功，`cache/owned_by_user` 被删除。该 ID 无法经正常入队；漏洞在持久输入的恢复边界。另测所选目录经目录软链接指向别处，正常发布成功写到另一个目录。 | 持久化本次授权的目录身份，重放时再次验证 ID 和实际目录；清理只对能证明属于该任务的临时文件生效。仅在入队拒绝 `..` 不够。 |
| F02 否决：导出发布会覆盖刚出现的用户文件 | C-50、S-09、O-35。[最终换名](../../../../kernel/exportq/src/export_queue.cpp#L430)调用可覆盖的 [POSIX rename](../../../../kernel/persist/src/posix_file_system.cpp#L126)。在目标检查为空之后、换名之前，在临时目录放入另一份不同字节的文件；发布仍返回成功，原字节消失。 | 用文件系统保证不覆盖的发布操作；碰撞保留目标、返回可区分的结果。不论入队或提交开头检查多少次，都要验最终这一步。 |
| F03 否决：已经校验的成品被记为失败 | B-98、O-22。[recover_one](../../../../kernel/exportq/src/export_queue.cpp#L533)。只让 commit-end 事件写入失败，此前换名、目录刷盘、目标回读校验均成功。重开：4 字节成品在，队列状态「失败」，成品事实 0 条。反向故障：临时文件在重启前从 4 字节被截为 1 字节，恢复先换名再验证；在换名后再次中断，目标可见 1 字节。 | 持久记录文件所有权，恢复前核实临时数据，能区分已发布成品、无关文件及半成品；已校验成品应给一次成功事实，不重写。不能把“目标存在”统一译成成功或失败。 |
| F04 否决：原图删除没有绑定被确认的文件 | S-10/S-11、O-35。[删除入口](../../../../kernel/project/src/project_library.cpp#L535)只给删除端口传路径。经指向 A 的目录软链接登记照片和凭证，再改指 B；删除返回成功，A 原件还在、B 的同名文件消失。另一条路径：擦除旧项目后新照片复用了旧 ID，用旧 ID 提交删除会删掉新原件。另持 SQLite 写锁时，命令报 I/O 失败但磁盘原件已删、无回收站行。 | 将确认绑定到不可复用的图片身份及实际文件；文件或授权变了就拒绝。记录可恢复的删除意图，使磁盘副作用与索引在重启后可核对。 |
| F05 否决：订阅资格与加速扣费分离 | C-47、需求 §17.1、§17.3。[加速](../../../../kernel/ledger/src/credit_ledger.cpp#L248)先扣；[开始生成](../../../../kernel/ledger/src/credit_ledger.cpp#L205)才查资格。未订阅且已用完三次：第 4 次扣 5 点后不能启动，100→95。若前三次都先加速，则 start 不记使用次数，实测四次都进入生成中；已用完三次后改批量生成也能启动。 | 在产生付费事件前核验使用资格，先分清能否用，再决定是否免费或按哪种价格收费。覆盖前三次和第四次的排队、加速、批量；不要以退款补偿正常会被拒的操作。 |
| F06 否决：同批虚拟副本分次完成会多扣张数 | C-41/C-48、B-110。[结算分组](../../../../kernel/ledger/src/credit_ledger.cpp#L365)只对本次传入且未结算的 export_id 分组。同批原图 A 和副本 B 已排队；A 完成结算后 5→4，B 完成用 A+B 再结算变成 3，应仍为 4。 | 持久区分“这次导出 + 原图”，重试、分次完成和重启不另扣；另一次新发起的导出仍可计费。 |
| F07 否决：旧句柄能恢复已取消的导出，开户修复也能写回旧余额 | B-53、B-63、P-23、O-24。[导出事件追加](../../../../kernel/exportq/src/export_book.cpp#L7)没有校验当前状态。两个连接都见同一任务排队，B 取消成功，A 仍能准备、渲染、发布，重开为成功，成品事实 1 条。[账本打开](../../../../kernel/ledger/src/ledger_book.cpp#L29)先读事件，后在另一个事务修缓存；在两者之间成功入账 20 点，事件余额 120，被“修复”的缓存是 100。另测 A 追加后 B 写无关任务，连续重试仍冲突，重开 B 才成功。 | 基于同一事务或有版本条件的快照判断状态、修缓存；跨连接不能复活取消任务，也不能用过时的余额覆盖新缓存。 |
| F08 否决：收紧羽化范围使有效的旧图层文件被隔离 | C-16/C-28、B-33、O-21/O-27。[PXLD 读取](../../../../kernel/layerdoc/src/layer_document.cpp#L146)。用提交版本的实际写入器保存两层、带蒙版、羽化 1001 的 PXLD v1；工作区读取器遇到一个旧字段就判整份文档损坏，原件进 isolate，没备份时返回 0 层。新输入 100 可用、101 被拒是正确的。 | 分开新编辑校验和旧格式读取；对有校验的旧值迁移或局部停用，保留其他图层和生成记录，不退回到允许输入 101。 |
| F09 否决：历史或第二个写入口可以覆盖更新的修订 | C-23/C-41/C-54、B-60/B-73。[选中历史](../../../../kernel/history/src/history_log.cpp#L48)替换整个文档；r3 → 选 r1 → 改值，磁盘实际变成 r2，此后历史追加才报冲突。[命令门闩](../../../../kernel/command/src/command_gate.cpp#L15)按实例加锁，两个打开同一文件的 gate 各自能提交 r1。原图与副本的历史还使用相同 image_id。 | 编辑实例和计费原图分开；内容可以回退，已发布修订不能倒退或重复；同一文档的写入口共享提交检查。 |
| F10 否决：整片同步和烘焙不保护过新或未知内容 | C-17/C-25/C-26、B-70/B-84/B-85。[整片替换](../../../../kernel/editdoc/src/edit_document.cpp#L698)、[烘焙](../../../../kernel/editdoc/src/edit_document.cpp#L742)。本机只支持 v1，已有 v99 的目标可被同步成功覆盖为 v1。对过去已提交的旧预览，整片同步后仍可不再拖动就写回原值。批量设计期间仍可[更新原精修](../../../../kernel/layerdoc/src/layer_document.cpp#L807)，清掉片段。 | 在所有写入口保护过新/未知字段、占用和修订；只替换已理解、该操作负责的片段，并清理失效预览。 |
| F11 否决：库迁移或持久化第二次失败时无法可靠恢复 | B-08/B-100/B-111/B-114/B-115。[v2→v3](../../../../storage/sqlite/src/sqlite_project_store.cpp#L286)中只拒绝第 2 条 ALTER，重开仍失败：版本 2，rating 已在，flag 不在；无需损坏 SQLite。另测[文档回滚](../../../../kernel/persist/src/atomic_file.cpp#L95)时恢复备份也失败，正式路径空。单份坏文档首次隔离后不另存，第二次打开误报从未存在。 | DDL 和版本一起提交；区分缺失、权限和已隔离，回滚本身失败也要返回并可恢复。禁止仅改版本号或把缺失一律当恢复为空。 |
| F12 否决：导出只记修订号；单项故障使整队无法打开 | C-49、B-66/B-68/B-94。[ExportJob](../../../../kernel/exportq/include/pixcake/exportq/export_queue.hpp#L14)入队 r1，继续编辑至 r3，重开只剩正式 r3、备份 r2 和任务里的数字 1。一次目标目录缺失又可导致 [ExportBook::open](../../../../kernel/exportq/src/export_book.cpp#L46)失败，不交出队列，另一项正常任务也取不到。新建缓存目录报磁盘满时任务仍是「渲染」，不能 retry。 | 入队保存重启可读的不可变参数或引用；单任务失败保留理由并隔离，其他任务能继续。 |
| F13 否决：批量操作反复读写整份状态 | P-01/P-08、总则 §5。真实 SQLite：2000 个任务仅入队就重插 **2,001,000** 行 [export_task_cache](../../../../storage/sqlite/src/sqlite_export_log.cpp#L325)；批量打星为 2000 次逐图 SELECT 加 2000 次 UPDATE。账本新防重[每次加载全部历史只比较数量](../../../../kernel/ledger/src/ledger_book.cpp#L60)：2000 次入账累计从 SQLite 读 **1,999,000** 行事件。 | 逐项更新受影响的缓存，批量读标记；用常数成本的版本条件或唯一键防重，保留全量重放给打开和诊断。验 SQL 行数，不只比较耗时。 |

## 第 1 关：业务契约

表中“通过”仅指明列的已实现子路径。结果码名称或一个返回成功的调用，本身不证明其他子路径也满足合同。

| 编号 | 结论 | 证据 | 差异 |
| --- | --- | --- | --- |
| C-01 | 不通过 | [图库显示模型](../../../../kernel/workspace/include/pixcake/workspace/workspace_display.hpp#L54)、test_library_controls；需求 §6.4 | RAW、修图状态、导出状态应默认打开；新项目三个值均未设置。 |
| C-02、C-06、C-07（排除项） | 通过 | CapabilityRegistry::phase_one、test_pipeline、构建目标 | detail 保留阶段不绘制，内核未加入带 Free 的分组、AI 校准、对应快捷键。 |
| C-03 | 通过 | ExportQueue::set_fast_export、test_snapshot_and_fast_export_stay | 队列空闲不会关闭用户开关；绕过开关的写入口见 C-21。 |
| C-04、C-05、C-06（白平衡）、C-07（阶段一快捷键）、C-08、C-11、C-12、C-18、C-19、C-27、C-30、C-32、C-33、C-34 | 未实现 | 未实现：生成/渲染进程、能力模式/编译、配置服务、导入解码、网络及停止在途工作 | 元数据、页面名称、取消命令被接收，都不能证明端到端行为。 |
| C-09、C-20、C-46 | 通过 | CreditLedger；test_free_queue_switches_and_master_allowance、test_balances_stay_separate_and_queue_cancel_is_free | 两套版本开关独立，点数与张数分账，余额不足有独立结果；大师资格另判。 |
| C-10、C-13、C-16、C-17、C-21、C-22、C-23、C-25、C-26、C-61 | 不通过 | F02–F04、F07–F12；test_b60_simultaneous_commits_serialize_to_one_revision 只共享一个 gate | 权限被报为不存在或一般 I/O；过新片段、完成时限、跨入口互斥和成功事实有反例。 |
| C-14、C-15、C-24（文档侧） | 通过 | test_b93_disk_full_keeps_formal_file、test_source_presence、test_c24_previews_do_not_write_or_bump_revision | 注入写满与路径失踪各返对应码，单独拖动只改内存。 |
| C-28、C-29、C-41、C-54 | 不通过 | F06、F08–F10；test_place_result_shapes_once、test_b101_reopen_folds_to_last_save | 正常图层形状和重启历史折叠有覆盖；副本隔离、代数递增、占用期间拒绝写回不成立。.tsq 像素与库登记未实现。 |
| C-31、C-35、C-43、C-44、C-45 | 不通过 | [能力注册表](../../../../kernel/capability/src/capability_registry.cpp#L14)、[预设校验](../../../../kernel/preset/src/preset_library.cpp#L91)、F10；test_c43_color_replaces_and_ai_color_stacks、test_tree_reads_attributes_from_owners | color.style 在注册表不得进预设，在预设库却能保存；祛边也被标为不能进，与需求 §13.1 相反。注册表只查重复标识，未校验依赖环或记录原因。名称边界见 B-20；同步乱序修复与版本保护分开判。 |
| C-36、C-37、C-38、C-39 | 不通过 | [commit_preview](../../../../kernel/editdoc/src/edit_document.cpp#L660)、test_b61_commit_keeps_latest_display_value、F10 | 通用入口没有能力值域/步进校验，实测曝光 99.00 写进文档（合同 −5.00 至 5.00）。应由能力模式验证后再提交。 |
| C-40、C-42（索引与到期） | 通过 | ProjectLibrary；test_project_paths、test_expired_index_is_removed_without_deleting_the_file | 登记绝对路径、凭证和元数据，不复制原图；30 天到期只清索引。删除的身份验证独立判 S-11。 |
| C-47、C-48、C-49、C-50 | 不通过 | F02、F03、F05–F07、F12；test_product_facts_match_one_settlement | 双扣 start/stroke 的旧反例已关闭；多扣张、越权启动、丢失成品事实及快照仍可复现。 |
| C-51、C-52、C-53、C-55、C-56、C-57、C-58、C-59、C-62 | 未实现 | 未实现：自定义工具栏/收藏、样片、按键、生成任务描述、kernel-cli、GPU、共享画布、语义像素 | 不把页面文案计为功能实现。 |
| C-60 | 不通过 | tests/arch/check_dependencies.cmake 规则 9；见 T-07 违规目标探针 | 当前内核没有 Qt 依赖，但门禁按目录豁免，并非按三个授权目标判定。 |

## 第 2 关：边界

| 编号 | 结论 | 证据 | 差异 |
| --- | --- | --- | --- |
| B-01、B-03（入队）、B-11（打印可用性）、B-12（页面）、B-15（标记）、B-28 | 通过 | test_b01_missing_fields_do_not_write、ExportQueue::enqueue、test_cancel_removes_temp_and_print_is_unavailable、test_workspace_display、test_marks_accept_only_the_documented_values | 对空参数、空选择和非法标记可断言不写；无打印机可生成文件，未登录模型不能切页。壳层授权未实现。 |
| B-02、B-05、B-06、B-07、B-09、B-10、B-13、B-14、B-21（工具栏/收藏）、B-22、B-23、B-24、B-25、B-26 | 未实现 | 未实现：导入/搜索/账号、配置缓存、素材与人脸检测、人数报价、样片/自定义、整项目导出 | 未作桌面手工验收。 |
| B-04、B-20、B-21（预设名） | 不通过 | PresetLibrary::save、test_c44_name_counts_characters_not_bytes；实际输入 20 组 e + U+0301、C0 AF、全角空格 | 合法 20 个可见字被拒；非法 UTF-8 和纯空白被接受。 |
| B-08、B-100 | 不通过 | F08/F11；test_b08_posix_truncate_without_backup_is_marked_empty、test_b100_posix_truncate_restores_backup_document | 有备份的普通截断可恢复；无备份第二次打开丢失标记，旧图层又会被误隔离。 |
| B-27、B-32 | 不通过 | EditDocument::commit_preview、ExportQueue::enqueue；分别提交 exposure=99.00、quality=99 | 已知范围外值能进入正式文档或持久任务；导出设置未经所属模式验证。 |
| B-29（历史/图层）、B-30、B-33（已实现编辑）、B-37（选择树）、B-61（同一 gate） | 通过 | test_b29_bad_index_keeps_revision_and_neighbors、test_only_the_expired_photo_is_purged、test_group_mask_selection_and_batch_design、test_tree_reads_attributes_from_owners、test_b61_commit_writes_latest_preview | 拒绝坏下标/越界新羽化，能禁用画笔和局部追色行并给理由。旧 PXLD 的读取见 F08。真实蒙版解析及未做的滑杆不在此通过范围。 |
| B-31 | 不通过 | WorkspaceDisplay::set_grid_per_row、test_library_controls | 从每行 4 调到 0 返回冲突并留在 4，没有停在左/右界对应的端点。 |
| B-34、B-35、B-36、B-40、B-41、B-43、B-44、B-45、B-46、B-48、B-50、B-51、B-54、B-55 | 未实现 | 未实现：解码/挑图、精修全图蒙版、裁剪/换算、画布、真实配置与进程超时 | 图层和账本的单项测试不能证明这些场景。 |
| B-42、B-47 | 未实现 | 导出计划只把 settings_snapshot 存为字符串；无排版/尺寸模式或编码器 | 尚不能验除零或目标大小；已知非法品质进入队列单列 B-32。 |
| B-52、B-60、B-62、B-66、B-68、B-70、B-73 | 不通过 | F09/F10/F12；注入 2080 ms 刷盘延迟、两 gate、极速期间套预设及丢失目标目录的复现 | 保存超过 2 秒仍成功；单 gate 的检查挡不住其他入口，单任务可影响整队，修订号不能找回入队正文。 |
| B-53（导出）、B-63（导出） | 不通过 | F03/F07；test_rename_failure_and_crash_points 与双 SQLite 连接实验 | 一个已返回成功的取消可失效，已发布文件也可能变成失败。 |
| B-63（账本）、B-64、B-101（正常落盘）、B-102（权限拒绝）、B-103（普通排队/生成）、B-104 | 通过 | test_sqlite_handles_reject_stale_duplicate_charge、真实双连接笔触重放、test_b101_reopen_folds_to_last_save、test_delete_source_file、test_generating_cancel_and_crash_do_not_refund | 单笔 30 点重放留 70，单笔 10 点留 90；正常排队取消不扣、已扣不退。额外扣加速费单列 F05。 |
| B-65、B-67、B-69、B-71、B-72、B-74、B-80、B-81、B-82、B-86、B-90、B-91 | 未实现 | 未实现：进程生命周期与确认、单实例、网络/账号、导入验真、原件损坏识别 | 仅能检查路径是否在场，不能验像素或协调已死工作进程。 |
| B-83（已解析的导入） | 通过 | PresetLibrary::import_preset、test_overwrite_and_import_do_not_change_old_rows | 新版或已删除标记不改旧列表；口令网络解析未实现。 |
| B-84、B-85 | 不通过 | F10；test_b84_too_new_fragment_is_not_rewritten、test_b85_unknown_inner_field_survives_commit | 单滑杆写回有保护，整片替换/历史重选/烘焙没有相同保护。 |
| B-92、B-93（首次写入错误）、B-95、B-96（正常清理）、B-97（完好临时文件）、B-109（无成品） | 通过 | test_source_presence、test_b93_disk_full_keeps_formal_file、test_rename_failure_and_crash_points、test_product_facts_match_one_settlement；临时探针在 temp-begin 后及 commit-begin 前终止 | 原图移走不损参数，写满保留旧文件；普通中断能清缓存、失败或完成换名；没有成品事实时不扣张。 |
| B-94、B-98、B-99、B-110、B-111、B-114（迁移）、B-115 | 不通过 | F01–F04/F06/F11/F12 | 碰撞、截断、迁移/回滚中断、校验后终止、分次结算均有独立反例。 |
| B-105、B-106、B-107、B-108、B-113、B-117 | 未实现 | 未实现：生成进程确认/重启、像素能力、退出流程、分部位模型 | 只留下 start 后重开账本仍“生成中”；缺少辨别工作进程生死的组件，不能把任意开户当成进程崩溃。 |
| B-112 | 不通过 | [restore_photos](../../../../kernel/project/src/project_library.cpp#L424)、test_restore_photos_fails_while_the_project_is_trashed；需求 §7 | 整项目恢复与恢复副本带回原图有测试；从回收站双击已删项目只还原选中图片却整次被拒。 |
| B-116 | 通过 | EditDocument::encode/decode，test_envelope_round_trip、test_c17_newer_file_format_is_document_corrupt、test_b100_posix_truncate_restores_backup_document | 已验 PXED 字节布局、魔数、版本、校验和和隔离命名；不代表所有业务写入口保留内容。 |

## 第 3 关：安全 SafeList

| 编号 | 结论 | 证据 | 差异 |
| --- | --- | --- | --- |
| S-01、S-02 | 通过 | SQLite Statement/bind_*；PosixFileSystem、FileSourceDeleter | SQL 参数绑定、文件操作没有 shell 拼接。路径授权与文件身份仍见 S-09–S-11。 |
| S-03、S-04、S-05、S-07、S-08、S-12、S-13、S-14、S-15 | 未实现 | 未实现：XMP/CUBE/真实导入解码、IPC、配置服务、快捷键和搜索 | 不能用索引入口或元数据模型代替输入验真。 |
| S-06 | 不通过 | PresetLibrary::save | 非法 UTF-8、全角空白和组合字符边界没有按可见字符合同拒绝。 |
| S-09 | 不通过 | F01/F02；ExportQueue::temp_path、POSIX rename | 入队修正常见 `..`，但持久化事件可绕过；普通路径经软链接可逃出选择目录；发布最后一步可覆盖竞态中出现的用户文件。 |
| S-10、S-11 | 不通过 | F04；ProjectLibrary::delete_source_file、FileSourceDeleter | 删除只拿路径，不验证入库凭证或文件身份；路径复用、软链接换向及索引写锁故障会造成磁盘与索引分裂。 |
| S-16、S-17、S-20 | 通过 | 仓库源码/文档/测试扫描；测试账本用临时库 | 未发现真实私钥、令牌或支付连接。 |
| S-18、S-19、S-25、S-26（异常路径）、S-27、S-28、S-29、S-30、S-31、S-32 | 未实现或不通过 | 未实现登录会话、单实例、更新器、外部购买和商家授权；账本正常余额路径归 LedgerBook | 账本正常扣费责任清晰，但 F05/F07 说明资格和跨句柄状态仍可绕过。 |
| S-21、S-22、S-23、S-24 | 通过（现有入口范围） | LogSink/CommandGate/ProjectLibrary 的记录只含路径、结果码、修订或能力 ID | 当前记录没有像素和凭证；导出、账本、恢复等入口没有完整诊断日志，转入 O-01。 |

## 第 4 关：性能

| 编号 | 结论 | 证据 | 差异 |
| --- | --- | --- | --- |
| P-01、P-08、P-23 | 不通过 | F07、F13；2000 次账本写入累计读取 1,999,000 行事件，导出任务缓存插入 2,001,000 行 | 事务内全量折叠和全表删重，不能按受影响行或版本条件增量更新。 |
| P-02、P-12 | 通过（索引子路径） | SqliteProjectStore 的 project/rating/flag/color 索引和 EXPLAIN QUERY PLAN | 列表没有读参数正文或原图像素；其他筛选字段尚未实现。 |
| P-03、P-04、P-05、P-09、P-10、P-13、P-14、P-17、P-19、P-20、P-24、P-25 | 未实现 | 未实现缩略图、渲染调度、配置缓存、跨进程取消、单实例和样片库 | 没有真实窗口/GPU/进程场景的证据。 |
| P-06 | 通过 | test_c24_previews_do_not_write_or_bump_revision | 预览不编码、不写库；导出快照问题另见 P-10。 |
| P-07、P-21、P-22 | 不通过 | F09；CommandGate 每个实例各有 mutex，两个 gate 可各提交 r1 | 同一文档的所有写入口没有共享串行区。 |
| P-11 | 不通过 | F12；temp 清理失败时仍留临时文件，恢复状态不能稳定表达可重试 | 失败出口缺少清理计数/上限和用户可见原因。 |
| P-15、P-16、P-18 | 不通过 | 2080ms 刷盘探针仍返回成功；预设可绕过极速开关 | 已实现的自动保存和占用路径不满足 2 秒及统一门闩。 |

## 第 5 关：代码质量

| 编号 | 结论 | 证据 | 差异 |
| --- | --- | --- | --- |
| Q-01、Q-11 | 不通过 | kernel/workspace 的页面布局与架构目录责任冲突；CMake 目标仍缺壳层分层 | 工作区是内核模型，窗口和菜单责任尚未移到 ui/shell。 |
| Q-02、Q-03、Q-04、Q-07、Q-08、Q-09、Q-10、Q-12、Q-13 | 通过（职责和命名子路径） | CapabilityRegistry、HistoryLog、CreditLedger、ExportQueue、LayerDocument、CommandGate 及架构检查 | 这些类边界清晰；F09/F10/F12 说明实现正确性仍不满足契约。 |
| Q-05 | 不通过 | result_code_label 与 F04/F11 的实际副作用不一致 | 权限、缺失、回滚失败和已隔离状态未可靠区分。 |
| Q-06、Q-20、Q-21、Q-23、Q-24、Q-25 | 未实现 | 未实现统一参数模式、轨道/默认值、蒙版部位、快捷键和 IPC 帧 | 不能从注册表字段推导完整能力实现。 |
| Q-14、Q-15、Q-17、Q-18 | 不通过 | F08–F11；EditDocument/LayerDocument/atomic_file | 显示值写入方式基本正确，但范围、旧格式迁移及五步替换二次失败没有守住。 |
| Q-19、Q-22、Q-30 | 通过 | fast-export latch、ledger 单笔扣费/笔触去重、HistoryLog 内存折叠 | 仅覆盖现有正常路径；跨句柄和资格异常见 F05/F07。 |
| Q-26、Q-27、Q-28、Q-29、Q-32、Q-33 | 不适用 | 架构 §17 排除项；当前没有这些深层实现 | 若后续代码加入排除项，应改判不通过。 |
| Q-31 | 不通过 | ExportJob 只有 document_revision 整数，无可重启正文/引用 | 重试不能保证使用入队时的真正参数。 |

## 第 6 关：测试

| 编号 | 结论 | 证据 | 差异 |
| --- | --- | --- | --- |
| T-01 | 通过（已有内核子集） | 22 个目标的单元测试；编辑、账本、队列、图层和项目库均有无窗口断言 | 未覆盖全部契约值域和跨进程路径。 |
| T-02、T-03 | 不通过 | F01–F13 的故障注入探针在 CTest 外复现；现有测试仍有与旧合同冲突的导出恢复断言 | 边界和异常没有全部纳入自动化；部分测试只断言状态机，不断言副作用一致性。 |
| T-04 | 不通过 | T-12、T-17–T-21 所需 kernel-cli/进程/单实例入口不存在 | 相关模块未实现，不能以 22 个库测试替代集成场景。 |
| T-05 | 不通过 | 隔离快照 CTest 22/22 通过，但 F01–F13 仍失败；最近提交后同步乱序反例已修复，仍有其他反例 | 编译和 CTest 绿不等于合同回归通过。 |
| T-06、T-10 | 通过 | 测试目标不链接 Qt；账本仅用临时 SQLite/内存 | 符合测试镜像和测试计费要求。 |
| T-07 | 不通过 | tests/arch/check_dependencies.cmake 规则 9 按路径豁免；未授权目录下的 Qt CMake 探针仍通过 | 门禁应按明确的三个授权目标判定。 |
| T-08 | 不通过 | 截断恢复在 tests/recovery，但导出换名、账本重放和跨进程恢复主要在 tests/kernel | 恢复测试没有覆盖完整 T-13–T-17 组合。 |
| T-09 | 必须补 | 多数测试名只有行为描述，没有 C/B 编号；最近新增测试也未统一编号 | 需要补契约/边界编号，方便逐行复审。 |
| T-11 | 未实现 | 生成模型和像素管线不存在 | 目前没有逐点比较问题，但入口/档位/结果形状的集成验收也不存在。 |

## 第 7 关：日志、指标、回滚

| 编号 | 结论 | 证据 | 差异 |
| --- | --- | --- | --- |
| O-01、O-04、O-05 | 不通过 | 仅 CommandGate/ProjectLibrary 写 LogSink；导出、账本、图层恢复没有模块、任务/文档标识的持久诊断记录 | 无法从日志数出导出阶段、重复扣费、隔离或恢复事件。 |
| O-02、O-03 | 通过（已有日志入口） | LogRecord 不含像素、凭证或整份文档，允许记录路径 | 仅限已有入口；后续日志必须沿用同样约束。 |
| O-06、O-07、O-08、O-09、O-10、O-11、O-12 | 未实现 | 未实现合并预览、保存耗时、导出/生成终态、恢复次数、磁盘满和去重计数 | 现有测试输出不是运行时指标。 |
| O-13、O-14、O-15、O-17、O-18、O-20 | 未实现 | 未实现登录、导入、用户界面任务列表和后续阶段入口状态 | 结果码枚举不能替代用户可见文案。 |
| O-16、O-19 | 不通过 | 结果码标签存在；F04 及导出/账本异常没有统一用户原因 | 点数不足、状态冲突、文件权限、网络和磁盘副作用未端到端区分。 |
| O-21、O-22、O-24、O-25、O-27、O-29、O-34、O-35 | 不通过 | F01–F04、F07–F12；atomic_file、ExportQueue、SqliteProjectStore | 误删/覆盖、已校验成品变失败、旧缓存覆盖、迁移中断、旧图层隔离和删除错对象均破坏回滚合同。 |
| O-23、O-30、O-31 | 未实现 | 未实现生成像素确认、重启补落层和迟到结果清理 | 账本 start/crash 状态机不足以证明生成恢复。 |
| O-28、O-32 | 通过（有限子路径） | 预览只在内存；排队取消不扣、生成中取消不退、重复 start/stroke 已防住 | F05 的加速先扣后拒仍造成错误扣点。 |
| O-33 | 通过（项目索引子路径） | 回收站 30 天和索引清除测试 | 虚拟副本的所有关联恢复/删除组合仍需按需求复审。 |
| O-26 | 未实现 | 没有卸载器 | 无法证明默认保留项目库、缓存和原图。 |

## 测试与状态

复审使用命令：

```text
cmake -S . -B <isolated-build> -DCMAKE_BUILD_TYPE=Debug
cmake --build <isolated-build> --parallel 8
ctest --test-dir <isolated-build> --output-on-failure
```

在包含 `128fc94`、`62cb4dc`、`4d68d5b` 的隔离源码快照上，结果为 **22/22 CTest 通过，7.03 秒**。另外运行了临时 SQLite、POSIX/FakeFileSystem 故障探针；F01–F13 的结果不依赖 UI。当前工作区仍有未提交的规范、图层修改和本复审记录，不能把它们误认为已提交实现。

**复审结论：打回重写。** 先完成 F01–F13 的失败测试和实现，再更新对应原计划复选框；不要通过修改清单、放宽结果码或继续扩大现有测试的“通过”范围来消除问题。
