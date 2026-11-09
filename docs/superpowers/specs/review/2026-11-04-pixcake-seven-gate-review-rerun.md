# 审核记录：2026-11-04，当前工作树七关复审

> 历史记录说明（2026-11-09）：本文只记录当时七关复审的事实和结论。Free 标记不构成当前能力排除；PhotoCraft-backed 能力、旧 PixCake 公式和当前完成状态以[PhotoCraft 能力来源与 PixCake 移植规范](../2026-11-09-photocraft-reference-and-porting-policy.md)及状态矩阵为准。

## 结论

**打回重写。** 当前工作树可以独立构建，已有 29 个 CTest 全部通过，但第 1 关仍有契约否决项，且多个产品责任模块没有实现。测试全绿不能把页面模型、元数据注册表或内核状态模型当成完整桌面功能。

本次复审依据：

- `docs/superpowers/specs/review/2026-11-02-pixcake-review-index.md`
- `docs/superpowers/specs/review/2026-11-02-pixcake-contract-checklist.md`
- `docs/superpowers/specs/review/2026-11-02-pixcake-boundary-checklist.md`
- `docs/superpowers/specs/review/2026-11-02-pixcake-safelist.md`
- `docs/superpowers/specs/review/2026-11-02-pixcake-performance-checklist.md`
- `docs/superpowers/specs/review/2026-11-02-pixcake-quality-checklist.md`
- `docs/superpowers/specs/review/2026-11-02-pixcake-test-checklist.md`
- `docs/superpowers/specs/review/2026-11-02-pixcake-ops-checklist.md`

结论含义严格按总则：**通过** 只表示代码与测试对得上该行；**不通过** 表示已有实现违反或缺少该行；**未实现** 表示负责模块还没有；命中否决项即不能把本轮标为通过。

## 验证命令

```text
cmake -S . -B /private/tmp/pixcake-fix-20261105 -DCMAKE_BUILD_TYPE=Debug
cmake --build /private/tmp/pixcake-fix-20261105 --parallel 8
ctest --test-dir /private/tmp/pixcake-fix-20261105 --output-on-failure
git diff --check
```

结果：**29/29 CTest 通过，7.74 秒；`git diff --check` 通过。** 构建包含 6 个 Qt 壳层 smoke、21 个内核/存储测试和 2 个架构门禁。

本轮已修复并复验：`tests/arch/check_dependencies.cmake` 现在扫描实际的 `kernel/capability` 目录；SQLite 的 `PRAGMA table_info` 只接受显式白名单表名；账本和导出日志加载批次信息改为单条 `LEFT JOIN`，不再对每个事件执行一次批次查询。

## 第 1 关：业务契约

**结论：不通过，且有否决项。**

| 结论 | 编号 | 证据与差异 |
| --- | --- | --- |
| 通过（已有子路径） | C-01、C-02、C-03、C-09、C-14、C-15、C-24、C-40、C-41、C-42、C-43、C-44、C-45、C-46、C-47、C-48、C-49、C-50、C-54 | `WorkspaceDisplay::home/entered_project` 默认打开三个显示项；当时的 `CapabilityRegistry::phase_one` 尚未为 Free 能力注册绘制（这是代码事实，不是当前范围裁决）；`ExportQueue` 不因队列变空自动关闭极速导出；项目索引、回收站、虚拟副本、预设类型/名称、同步、账本、导出快照和历史已有对应测试。证据包括 `test_workspace_display`、`test_capability_registry`、`test_preset_library`、`test_sync`、`test_ledger`、`test_export_queue`、`test_history`。这些结论只覆盖已有子路径。 |
| 不通过 | C-10、C-21、C-23、C-25、C-26、C-35、C-61 | `EditDocument` 的 `set_preview/commit_preview/commit_fragment_replacement`、`HistoryLog`、`LayerDocument` 仍有公开写入口；`CommandGate` 只在当前进程按路径共享 mutex，不能成为所有入口的统一提交边界。`PresetLibrary::apply` 已显式接收极速导出门闩，但这不能覆盖其它公开入口。参数模式没有统一数据结构，结果码、持久化和未知片段保护也没有覆盖所有写入路径。能力注册表只检查管线重复，不验证依赖环和环路日志。 |
| 不通过 | C-36、C-37、C-38、C-39 | `validate_parameter_display_value` 在 `kernel/capability/src/capability_registry.cpp:119-156` 只覆盖 `color.global` 的部分参数以及 `color.match/style.amount`。色温、色调、影调的其它能力参数、蒙版/裁剪/人物/眼神光等契约范围没有统一模式来源。现有校验是硬编码分支，不是“参数模式是界面唯一来源”。 |
| 未实现 | C-04、C-05、C-06（白平衡以外的生成路径）、C-11、C-12、C-18、C-19、C-27、C-30、C-31、C-32、C-33、C-34、C-51、C-52、C-53、C-55、C-56、C-57、C-58、C-59、C-60、C-62 | 当前仓库没有 `kernel/import`、`kernel/render`、`kernel/generate`、`kernel/config`、`kernel/cli`、`apps/renderd`、`apps/generated`、`gpu` 或 `services`。没有真实导入解码、能力编译、预览/导出共享帧、生成式任务描述、kernel-cli、OpenGL 冻结接口或可替换语义部位模型。`ui/shell` 和 `apps/shell` 只有页面与禁用控件，不能作为这些模块的实现证据。 |

第 1 关已经存在的否决项包括：公开写入口可以绕过统一门闩、参数契约不完整、进程职责和导入/生成/渲染链路缺失。后面的测试或页面 smoke 不能把这些项改成通过。

## 第 2 关：边界条件

**结论：不通过。已有持久化/队列子路径有进展，但大量边界仍未实现。**

| 结论 | 编号 | 证据与差异 |
| --- | --- | --- |
| 通过（已有子路径） | B-01、B-03、B-04、B-08、B-20、B-21（预设名）、B-29、B-30、B-31、B-60（单进程 gate）、B-63（导出/账本已测路径）、B-64、B-66、B-68、B-73（单进程文档）、B-83、B-84、B-85、B-92、B-93、B-95、B-96、B-97、B-98、B-99、B-100、B-101、B-102、B-109、B-111、B-116 | `test_command_gate`、`test_atomic_file`、`test_document_recovery`、`test_export_queue`、`test_ledger`、`test_recycle_bin` 等已验证缺字段、空白预设、网格 1–7、备份/隔离、导出临时文件、任务快照、重复记账和普通原图缺失路径。最近加入的导出事件事务校验、临时文件设备/inode/大小校验、原子替换恢复重试和大师精选资格前置校验已在相应测试中关闭旧反例。 |
| 不通过 | B-27、B-32、B-33、B-44 | 显示值和导出 `quality` 仍不是由完整参数模式统一验证；代码只在少数参数上检查范围/步进，导出设置仍是可解析字符串。未知或尚未声明的参数不能靠“默认允许”进入正式文档或任务。 |
| 不通过 | B-52、B-60（跨进程）、B-62、B-67、B-73（跨进程）、B-111/O-21 的完整失败矩阵 | 自动保存路径没有 2 秒超时/耗时判定，`CommandGate` 的 mutex 和修订注册表只在一个进程地址空间内；第二实例和两套 SQLite 没有单实例锁。极速导出只挡住接入门闩的路径，公开文档/历史/图层入口仍可绕过。原子替换的已有测试覆盖正常注入顺序，但没有把所有真实文件系统竞态纳入自动化。 |
| 未实现 | B-02、B-05–B-07、B-09–B-15、B-22–B-26、B-34–B-55、B-65、B-69–B-72、B-74、B-80–B-82、B-86、B-90–B-91、B-103–B-108、B-112–B-115、B-117 | 没有导入验真/尺寸检查、登录和短信超时、真实画布共享内存、第三方坏返回、生成确认/崩溃恢复、单实例、配置缓存、打印设备、全身美型/纯色背景可用性判断或语义部位模型。页面中的“导入/登录/AI 工具”只是禁用或空区域。 |

一个仍然存在的磁盘一致性风险是 `ProjectLibrary::delete_source_file`：它先调用 `FileSourceDeleter::remove_source` 删除磁盘文件，再调用 `store_.in_transaction` 更新索引（`kernel/project/src/project_library.cpp:543-563`）。如果后一个事务失败，磁盘副作用无法补偿，项目索引和原图事实会分裂；现有测试没有覆盖这个失败顺序。

## 第 3 关：安全 SafeList

**结论：不通过。SQL 参数绑定和无真实凭证的现有样本通过，但输入验真、动态 SQL、路径/身份、鉴权入口仍有缺口。**

| 结论 | 编号 | 证据与差异 |
| --- | --- | --- |
| 通过（已有入口） | S-02、S-16、S-17、S-20–S-24 | 文件操作没有 shell 拼接；仓库扫描未发现真实令牌/私钥/支付凭证；`LogRecord` 结构不接收像素缓冲或整份文档，现有 `CommandGate`/`ProjectLibrary` 日志只写结果码、路径、修订和能力标识。 |
| 通过（本轮修复） | S-01 | `storage/sqlite/src/sqlite_project_store.cpp:137-155` 和 `sqlite_export_log.cpp:113-135` 现在把允许的表名映射为固定 SQL 常量，未知表名直接拒绝；其余用户数据仍走 `sqlite3_bind_*`。 |
| 不通过 | S-06、S-07、S-09、S-10、S-11 | 预设名已有 Unicode 空白和 UTF-8 基础校验，但没有完整用户可见字符/字素簇定义；`ProjectLibrary::add_photo` 只检查绝对路径和文件身份，未检查文件格式、内容、尺寸或可解码性。导出路径有规范化和身份校验子路径，但源文件删除的磁盘与索引事务仍可能分裂。 |
| 未实现 | S-03–S-05、S-08、S-12–S-15、S-18、S-19、S-25、S-27–S-32 | 没有 XMP/CUBE 解析、真实导入解码、IPC/共享内存长度校验、配置服务、快捷键自定义、登录会话、单实例、更新包校验、外部购买链接或商家成员授权。 |

## 第 4 关：性能

**结论：不通过。**

| 结论 | 编号 | 证据与差异 |
| --- | --- | --- |
| 通过（有限索引/预览子路径） | P-02（项目索引子路径）、P-06、部分 P-12 | `SqliteProjectStore` 对项目/星级/旗标/色标有索引，`EditDocument::set_preview` 不编码、不写盘，`test_c24_previews_do_not_write_or_bump_revision` 通过。未扩展到完整筛选字段、缩略图和导出状态。 |
| 通过（本轮修复） | P-01 | `SqliteLedgerLog::load` 和 `SqliteExportLog::load` 现在通过 `LEFT JOIN` 一次读取批次信息，不再按事件执行 N+1 查询。 |
| 不通过 | P-07、P-21、P-22、P-23、P-25 | `CreditLedger` 每个操作都重新 fold 全部事件；`CommandGate` 的串行区和单实例保护没有跨进程共享。架构门禁不等于运行时版本条件。 |
| 不通过 | P-11、P-15、P-16、P-18 | 队列记录了 `phase/duration_ms/deduplicated` 字段，但 `event_from` 没有真实计时、预览合并或去重指标；自动保存没有 2 秒控制；极速导出只覆盖已接入入口。 |
| 未实现 | P-03–P-05、P-08–P-10、P-13、P-14、P-17、P-19、P-20、P-24 | 没有 `cache/thumbs` 缩略图生命周期、渲染进程/共享帧、配置缓存、生成并发/取消、完整分辨率导出隔离或样片库 2000 张场景。 |

## 第 5 关：代码质量

**结论：不通过。**

| 结论 | 编号 | 证据与差异 |
| --- | --- | --- |
| 通过（有限模块） | Q-02、Q-03、Q-07、Q-08、Q-09、Q-10、Q-17、Q-19、Q-30 | 能力标识、历史内存折叠、账本、导出快照、图层模型、当时的 Free 排除假设和极速导出保持规则已有清晰类型与测试；Free 范围结论已由后续裁决取代。 |
| 不通过 | Q-01、Q-04、Q-05、Q-06、Q-11–Q-15、Q-18、Q-20、Q-22、Q-23、Q-25、Q-31 | `WorkspaceDisplay`、`ui/shell` 和 `apps/shell/main.cpp` 重复维护大量页面/控件文字；参数范围、步进、默认值没有统一模式；`LogSink`/结果码没有接入导出、账本和恢复入口；`CommandGate` 不能约束所有公开写入口；导出任务虽存 `document_snapshot`，但没有 renderer/CLI 消费它的端到端链路。 |
| 未实现/不适用 | Q-21、Q-24、Q-26–Q-29、Q-32–Q-33 | 蒙版部位、快捷键表、IPC 字段、GPU/native、生成进程和后续阶段入口尚未形成完整模块；本记录当时把 Free 深层能力按旧架构排除，当前应按 PhotoCraft 来源矩阵重新判定，不能用本行代替现行状态。 |

架构门禁路径问题已在本轮修正为 `kernel/capability`；但门禁仍然不能替代运行时的跨进程锁、配置和协议测试。

## 第 6 关：测试

**结论：不通过。**

| 结论 | 编号 | 证据与差异 |
| --- | --- | --- |
| 通过（已有模块） | T-01、T-06、T-10 | 独立构建的 29/29 CTest 通过；内核目标不链接 Qt；账本测试使用临时 SQLite/测试替身，不连接真实支付。 |
| 不通过 | T-02、T-03、T-04、T-05、T-08 | 现有测试覆盖的是已实现内核子集，未覆盖清单中的全部 B 行、第三方坏返回、登录/导入/IPC/生成/单实例/完整恢复组合。恢复测试分散在 `tests/recovery` 与内核测试中，缺少导出、账本、生成进程组合的统一恢复场景。 |
| 通过（本轮修复，范围有限） | T-07 | 架构门禁现在扫描实际的 `kernel/capability` 目录并通过；仍需为所有授权目标增加更细的依赖边界测试。 |
| 必须补 | T-09 | 127 个测试函数中只有约 40 个名称带 `bXX/cXX/pXX` 形式的清单编号；多数测试无法从名称追溯到审核行。 |
| 未实现 | T-11 | 没有生成模型和像素管线，因此只能审入口/档位/计费/结果形状；这些端到端入口本身也不存在。 |

CTest 全绿是本次验证的真实结果，但不能替代缺失模块的集成测试，也不能覆盖当前代码中没有负责人的边界。

## 第 7 关：日志、指标、回滚

**结论：不通过。**

| 结论 | 编号 | 证据与差异 |
| --- | --- | --- |
| 通过（有限子路径） | O-02、O-03、O-28、O-32、O-33 | 现有日志结构不含像素/凭证；预览中间值不写盘；账本排队取消/生成中取消/重复扣费和回收站 30 天有测试。 |
| 不通过 | O-01、O-04、O-05 | `LogSink` 只有 `CommandGate` 和 `ProjectLibrary` 两个调用方（`rg LogSink kernel storage apps ui` 可复核）；导出、账本、原子替换、隔离、恢复没有统一的持久应用日志，也没有进程/任务/文档标识和开始/完成事件。 |
| 未实现 | O-06–O-12 | 没有预览合并数、自动保存耗时、导出/生成终态、恢复次数、磁盘满次数或重复记账拦截的运行时计数。队列结构里的 `duration_ms`/`deduplicated` 字段没有形成真实指标。 |
| 不通过或未实现 | O-13–O-20 | 结果码标签存在，但壳层按钮和任务列表没有登录、导入、原图缺失、配置更新、导出/生成失败的真实用户反馈链路；`ExportQueue::failure_reason` 只提供内核查询，不是用户可见任务状态。 |
| 通过（有限子路径） | O-21、O-22、O-24、O-27、O-29、O-32 | 原子文件、导出身份/临时文件、账本缓存修复和已有测试路径能保留最后一份好数据、已校验成品和账本事件。 |
| 不通过/未实现 | O-23、O-25、O-26、O-30、O-31、O-34、O-35 | 没有生成结果确认/重启补落层、更新器、卸载器、未知字段/版本停用的完整用户提示；`ProjectLibrary::delete_source_file` 的“先删磁盘、后写索引”顺序仍可能在索引失败时破坏原图事实。 |

## 打回项与复审条件

1. 把所有文档、历史、图层、预设和导出写入口收进同一套可验证的命令/门闩/修订提交边界，且跨进程使用持久版本条件；公开方法不能绕过它。
2. 将显示范围、步进、默认值、禁用原因和能力资格收进真正的参数模式，并覆盖契约 C-36–C-39 及 B-27/B-32/B-33 的全部已实现参数。
3. 修复项目删除的磁盘与索引原子性：删除失败或索引事务失败时必须保留可恢复状态，并补上相应故障注入测试。
4. 修正 SafeList 的动态 SQL 白名单、导入内容/尺寸验真、路径书签/身份和真实单实例/会话校验。
5. 按性能清单消除账本/导出加载的 N+1 查询，补齐缩略图、渲染/生成调度、配置缓存和跨进程取消/超时。
6. 补齐或明确标记为未实现的导入、配置、渲染、生成、IPC、kernel-cli、GPU/native、真实日志/指标、更新器和卸载器；页面 smoke 不得再作为这些功能的证据。
7. 把 F/B/C/O 故障组合加入对应自动化测试镜像，测试名带清单编号，修正 `kernel/capabilities` 门禁路径后重新运行全量构建、CTest、架构门禁和跨句柄/恢复探针。

在上述否决项关闭、未实现模块完成或被明确排除并重新审核前，当前项目不能标记为“审核通过”或“计划已全部实现”。
