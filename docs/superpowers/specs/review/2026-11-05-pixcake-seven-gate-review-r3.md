# 审核记录：2026-11-05，七关第三轮复审（HEAD `1de349e`）

## 结论

**打回重写。** 第 1 关仍有否决项（跨进程丢失更新 C-23/B-60/B-73；壳层 Free 分组界面 C-02；壳层写死滑杆范围 C-38）。上一轮 7 条打回项：1 条关闭、5 条部分关闭、1 条未关闭。详见文末“打回项关闭对照表”和“总结论”。

本次复审依据与上一轮相同：`2026-11-02-pixcake-review-index.md` 与七份清单；对照上一轮 `2026-11-04-pixcake-seven-gate-review-rerun.md` 的 7 条打回项。结论含义严格按总则；证据不足的行记“不通过（证据不足）”，不推定为通过。

计划：`docs/superpowers/plans/2026-11-05-seven-gate-review-r3.md`。本轮只读审核，未改产品代码；探针放在会话 scratchpad，未提交。

## 验证命令

```text
cmake -S . -B <scratchpad>/build-r3 -DCMAKE_BUILD_TYPE=Debug
cmake --build <scratchpad>/build-r3 --parallel 8
ctest --test-dir <scratchpad>/build-r3 --output-on-failure
git diff --check
```

结果：**构建成功；51/51 CTest 通过，15.39 秒；`git diff --check` 通过。** CTest 含 6 个 Qt 壳层 smoke、2 个架构门禁（`arch_dependencies`、`capability_ownership`）、7 个 GPU/renderd/exportd 测试和 36 个内核/服务/存储测试。

当前模块：`kernel/{assets,capability,command,cull,editdoc,exportq,generate,history,import,ipc,layerdoc,ledger,manual,mask,persist,preset,preview,project,recipe,render,subject,support,sync,workspace}`、`services/{account,config}`、`apps/{exportd,renderd,shell}`、`gpu/{backend,facade,render}`、`storage/sqlite`、`ui/shell`、`third_party/{glad,libvips.lock.cmake}`。仍不存在：`apps/kernel-cli`、生成进程、`cache/thumbs` 缩略图模块、更新器、卸载器。

测试函数 170 个，其中 40 个名称带清单编号（23.5%）。

## 本轮探针

| 探针 | 场景 | 实际结果 | 判定 |
| --- | --- | --- | --- |
| `probe_cross_handle_commit` | 同一进程两个 `EditDocument` 句柄都在修订 0 装载同一文件；A 提交曝光 `1.00`，B 以过期修订 0 提交 `-1.00` | A `Ok`；**B 也返回 `Ok`**；磁盘为修订 1、曝光 `-1.00`，A 的已提交结果被覆盖 | C-23 否决命中 |
| `probe_cross_process_gate` | 两个进程各自 `CommandGate`，都在修订 0 装载，先后 `preview`+`commit` 曝光 `2.00` / `-2.00` | **两个进程都返回 `Ok`、修订都为 1**；磁盘为 `-2.00`，前一个进程的提交丢失 | C-23 否决命中；打回项 1 未关闭 |
| 同上（参数段） | 曝光 `0.03`、`5.05`、`abc`；对比度 `1.5`；未声明参数；`hsl/hue_red` | 提交全部被拒（`StateConflict`）；色温 `100` 被接受 | 内核参数模式对 C-36–C-39 成立 |

`WriteScope` 的 `flock` 只串行化写入动作，提交前比较的是句柄内存中的修订号（`kernel/editdoc/src/edit_document.cpp:660,729,799`），没有在锁内重读磁盘修订；`CommandGate` 的修订表 `g_document_revisions` 是进程内静态变量（`kernel/command/src/command_gate.cpp:6-7`）。

## 第 1 关：业务契约

**结论：不通过，且有否决项。**

| 结论 | 编号 | 证据与差异 |
| --- | --- | --- |
| 通过（已有子路径） | C-01、C-03、C-09、C-10、C-14、C-15、C-24、C-40（导入协调器路径）、C-41、C-42、C-43、C-44、C-45、C-46、C-47、C-48、C-49、C-50、C-54、C-57、C-60 | 上一轮通过项复核仍成立。新增：C-09 `test_config_store` 断言 `client_version == "9.3.0"`、官方免点队列关闭、已订阅非高算力免点队列开启；C-57 `gpu/facade/include` 八个冻结类名与方法齐全、无 Qt 头；C-60 只有 `apps/shell`、`gpu/backend/qt` 链接 `Qt6::Gui`，`arch_dependencies` 通过。 |
| 通过（已有子路径） | C-08、C-11、C-12、C-13、C-16、C-17、C-18、C-19、C-20、C-22、C-61 | `services/config` 默认表与校验（`test_config_store`）；`kernel/import` 的 `probe_bytes/probe_source` 把损坏 PNG/JPEG/TIFF 判 `ImageCorrupt`、BMP 判 `UnsupportedFormat`、缺失判 `SourceMissing`、拒读判 `PermissionDenied`（`test_source_probe`）；结果码枚举与上一轮一致。 |
| **否决：不通过** | C-23 | 见“本轮探针”：过期修订的提交在跨句柄、跨进程场景都返回 `Ok` 并覆盖已写盘的新内容。合同要求“修订号过期返回状态冲突，不覆盖新写盘内容”。不接受的补丁：只在 `CommandGate` 的进程内表里再加一层检查——`EditDocument::commit_preview/commit_fragment_replacement/commit_baked_reset` 仍是公开入口，且进程内表跨不过进程。 |
| **否决：不通过** | C-02、C-05（HSL 部分） | `apps/shell/main.cpp:243-330` 为 Free 分组建了界面：色阶（黑场/灰场/白场/输出黑白场滑杆）、曲线（7 个子项）、HSL（8 色）、可选颜色（9 目标 × 4 通道）、颜色分级、色彩编辑器、细节、颗粒、镜头调整、透视矫正、校准、AI 校准按钮。虽然全部禁用，C-02 禁止“为这些分组建参数模式、界面、着色器……”。**冲突需上位裁决**：这些控件来自“官方界面优先”的视觉计划（`2026-11-05-refine-remaining-module-labels.md`、`confirmed-slider-ranges.md`）。按总则第 4 节，本轮先记冲突、结论不通过；要保留只读展示，必须先修订需求/架构/清单中 C-02 的“界面”范围再重审。 |
| **否决：不通过** | C-38、C-36–C-39 段末规则、C-31 段末规则 | 内核 `ParameterMode` 正确（曝光 -5.00–5.00 步进 0.05；程度 0–200 默认 100），但 `apps/shell/main.cpp:264-288` 在控件源码里写死另一套上下限：把“曝光”“程度”与其他影调一起设为 -100–100、步进 1、默认 0，并写进无障碍名。合同：“参数模式里的显示刻度是界面画滑杆的唯一来源。控件源码再写一份上下限，否决。”另外 `ui/shell/color_adjustment_panel.hpp` 已有从内核参数模式派生的消费模型，但壳层没有使用它。证据文件 `evidence/2026-11-05-refine-color-slider-bounds.md:35` 自承色阶、创意色彩程度等范围“来源是市面惯例，不是手册原文”，违反 §5“没在需求或附录 B 里出现的上下限，写死进代码就是否决”。 |
| 不通过 | C-21、C-25、C-26 | 极速导出门闩已登记到 `WriteScope`，单进程路径成立；但 C-23 的丢失更新同样让 C-25“正式文件仍是上一份完整内容”的语义在多句柄下失效（被覆盖的不是坏文件，而是别人已提交的修订）。C-26 只改自己片段在单句柄下成立。 |
| 不通过 | C-35 | `validate_phase_one_pipeline` 能识别重复与环路（`test_capability_registry` 断言 `"管线存在依赖环路"`），但唯一调用方 `phase_one_pipeline_order` 遇环只返回空数组；没有“拒绝开始编辑”和“写日志”的路径。 |
| 不通过 | C-31 | 只有 `CapabilityRegistry` 描述表与参数模式表（`kernel/capability/src/capability_registry.cpp`），没有每个能力独立模块实现架构第 7.2 节的提取、写回、编译、显示刻度、禁用原因、迁移。参数模式另用了 §4 注册表外的标识 `portrait.eye_glint`、`pet.eye_glint`、`light.uniform`、`mask`，并为同一参数登记了 `temperature`/`color_temperature`、`tint`/`color_tint` 两个名字；需上位文档确认这些标识。 |
| 不通过（证据不足） | C-05（黑白档）、C-33 | `ParameterMode` 没有禁用原因/能力资格字段；代码中找不到黑白配置文件禁用鲜艳度、饱和度的规则；找不到精准控制开关下追色单一来源的实现或测试。 |
| 通过（已有子路径） | C-27、C-28、C-32、C-59 | `kernel/render` 编译为纯数据描述（`test_render_description`）；`tsq_sibling_path`、图层文档测试；`kernel/ipc` 预览请求最长边 ≤ 4000、帧头 RGBA8 与行距校验（`test_preview_protocol`）；预览协调器只保留最新值（`test_preview_coordinator`）。只覆盖内核数据形状，不含真实像素。 |
| 不通过 | C-30 | `apps/renderd`、`apps/exportd` 已有进程边界与生命周期测试，但没有生成进程，也没有真实跨进程消息；“四个进程不各写一套字段定义”无法验证完整。 |
| 通过（已有子路径） | C-51、C-52 | `services/account` 第 6 个工具栏能力、第 7 个收藏色返回 `StateConflict`（`test_account_session`）；`kernel/assets` 单次 1000、总量 2000 上限与提示文字（`test_asset_catalog`）。壳层尚无对应入口。 |
| 未实现 | C-04、C-06（白平衡吸管/RAW 预设）、C-29（应用至精修的写回流程）、C-34、C-53、C-55（第三方工具接入）、C-56、C-58、C-62 | `kernel/generate` 只有任务描述与结果形状（`generation_task.hpp`），没有生成进程和落层命令；没有 `apps/kernel-cli`；快捷键表不存在（仅“查看快捷键”文字）；画布不消费共享内存帧；无语义部位模型。 |

## 第 2 关：边界条件

**结论：不通过，且有否决项。**

| 结论 | 编号 | 证据与差异 |
| --- | --- | --- |
| 通过（已有子路径） | B-01、B-03、B-04、B-08、B-20、B-21、B-22、B-29、B-30、B-31、B-63、B-64、B-66、B-68、B-83、B-84、B-85、B-92、B-93、B-95–B-101、B-109、B-111、B-116 | 上一轮通过项复核仍成立（`test_command_gate`、`test_atomic_file`、`test_document_recovery`、`test_export_queue`、`test_ledger`、`test_recycle_bin`、`test_preset_library`）。B-21/B-22 新增 `test_account_session::test_layout_limits_and_logout_scope`、`test_asset_catalog`。 |
| 通过（本轮新增子路径） | B-02、B-34、B-40、B-90 | `test_import_coordinator`：空导入列表返回 `StateConflict`、不新建项目；4 张批次中 1 张损坏只拒这一张、其余进入；`source_probe.cpp:58,144-149` 拒绝 0/负尺寸与乘法溢出（`test_source_probe`）。 |
| 通过（本轮新增子路径） | B-53、B-65、B-73（导出路径）、B-81（导出路径） | `test_export_queue::test_b63_stale_export_handle_cannot_publish_after_cancel`：两个 `SqliteExportLog` 句柄，B 取消后 A 的 `commit_product` 返回 `StateConflict`、目标文件不存在。上一轮的跨句柄反例已关闭。 |
| 通过（本轮新增子路径） | B-07、B-86 | `test_config_store::test_cache_fallback_and_replacement`：坏配置被拒（`StateConflict`），保留上一份有效快照或回退需求默认表。 |
| 通过（本轮新增子路径） | B-10、B-48、B-74 | `test_subject_store::test_pending_and_failed_do_not_expose_geometry`；`test_preview_protocol` 拒绝错误行距/尺寸；`test_preview_coordinator::test_latest_value_wins_without_writing`。均为内核数据层，没有真实画布。 |
| 通过（关闭打回项 3） | B-102、B-112（删除原图部分） | `ProjectLibrary::delete_source_file`（`kernel/project/src/project_library.cpp:517-571`）先在事务中写回收站与 `kPresenceDeletePending`，再删磁盘，再写终态；`test_delete_source_file_recovers_when_final_presence_write_fails` 注入终态写失败后，`startup()` 收敛为 `kPresenceMissing`；`test_delete_uses_enrolled_file_identity` 用登记的文件身份删除。遗留观察：`PermissionDenied` 时照片保持 `DeletePending` 直到下次启动扫描，用户可见状态需在壳层接入时复核。 |
| **否决：不通过** | B-60、B-73（文档路径） | 见“本轮探针”：两个进程在同一文档上都得到修订号 1（“不出现两个相同修订号”被违反），过期修订的提交覆盖了较新的已提交文档。 |
| 不通过 | B-62 | 极速导出门闩登记表是进程内静态表（`register_fast_export_latch`，`kernel/persist/include/pixcake/persist/write_lock.hpp`）；另一个进程的编辑/预览看不到这个开关。 |
| 不通过 | B-32 | `valid_quality_setting`（`kernel/exportq/src/export_queue.cpp:208-219`）仍在设置字符串里找第一个 `quality=`；没有 `quality=` 时直接放行，`quality=3;quality=99` 只校验第一个。导出设置仍不是参数模式驱动的结构。上一轮同一问题未修。 |
| 不通过 | B-27、B-33 | 内核参数模式拒绝范围外与步进错误值（探针）；但壳层控件源码自带另一套范围（第 1 关 C-38），界面层的端点不由参数模式决定。B-33 中图层不透明度、祛路人选区羽化 0–1000 不在 `phase_one_parameter_modes()` 里，需另行确认各自的校验来源。 |
| 不通过 | B-52 | 仍找不到“提交后 2 秒”判定或计时；提交是同步写盘，没有超时返回路径。 |
| 未实现 | B-67 | 没有单实例锁；`flock` 只用于每个文档路径的写锁。 |
| 未实现 | B-35 | `kernel/cull` 中没有 150 张挑图询问阈值。 |
| 未实现 | B-05、B-06、B-09、B-11–B-15、B-23–B-26、B-28（数字键）、B-36、B-37、B-41–B-43、B-45–B-47、B-50、B-51、B-54、B-55、B-61（跨进程）、B-69–B-72、B-80、B-82、B-91、B-94、B-103–B-108、B-110、B-113–B-115、B-117 | 没有登录/短信流程、真实画布、第三方生成接入、生成进程恢复、打印、排版、取色/直方图像素、更新器和语义部位模型；`kernel/generate` 的状态机测试只覆盖数据形状，不能代替生成进程与第三方异常路径。 |

## 第 3 关：安全 SafeList

**结论：不通过。**

| 结论 | 编号 | 证据与差异 |
| --- | --- | --- |
| 通过 | S-01、S-02、S-05、S-16、S-17、S-20–S-24 | `storage/sqlite/src/*` 中 `sqlite3_exec/prepare_v2` 只接收字符串常量，`PRAGMA table_info` 走固定常量映射（`sqlite_project_store.cpp:139-147`），未发现字符串拼接 SQL；产品代码无 `system/popen/exec*`；扫描 `kernel services apps storage gpu ui tests` 未发现令牌/私钥/口令样本；`LogRecord` 不含像素或整份文档。 |
| 通过（已有子路径） | S-06、S-09、S-11、S-12（头信息层）、S-13（预览协议层）、S-14、S-26 | 预设名 Unicode 空白与长度在 `kernel/preset` 拒绝；导出路径规范化与身份校验；源文件删除使用登记身份（第 2 关）；`source_probe` 先查宽高与溢出；`preview_protocol` 先校验最长边、行距、总字节；`services/config` 只读声明结构；余额只由账本事件改变。 |
| 不通过 | S-07 | 导入协调器路径会先 `probe_source` 再入库（`kernel/import/src/import_coordinator.cpp:20`），但 `ProjectLibrary::add_photo` 仍是公开入口，自身只检查绝对路径与文件身份，不验内容（`kernel/project/src/project_library.cpp:601-630`），且 `identify` 返回 `not_found` 时仍以 `kPresenceEditable` 写入（`project_library.cpp:628-634`）。“导入可信”可以被直接调用绕过。 |
| 不通过 | S-10 | 只有绝对路径与 `access_credential` 字段透传；没有安全范围书签的获取/续期实现，无法证明“不把任意路径的读取权扩成整个磁盘”。 |
| 不通过 | S-33 | `kernel/import/src/source_probe.cpp` 手写 PNG/JPEG/TIFF 标准格式头解析，用于生产导入准入。依赖规范第 4 节要求手写通用格式解析必须有候选比较与例外记录；`kernel-import-admission-closure` 与 `kernel-project-import-closure` 的计划和证据中没有这条记录。冲突说明：架构禁止 `kernel/` 包含图像库头，因此修复方向需上位裁决（补例外记录，或把内容验真移到 `apps/renderd` 的 `ImageDecoder`）。 |
| 不通过 | S-34 | `third_party/glad`（头注释为 glad 2.0.8、`gl:core=3.3`、SPDX `(WTFPL OR CC0-1.0) AND Apache-2.0`）没有依赖记录：缺上游地址/生成命令、许可证文本与 NOTICE、候选比较，`third_party/` 下也没有 LICENSE 文件。`libvips.lock.cmake` 记录了版本、提交、SHA256 和许可证，且尚未进入生产 CMake，符合规范第 5 节。 |
| 通过 | S-35、S-36 | `arch_dependencies` 门禁禁止 `kernel/` 引入 Qt/OpenGL/opencv/cuda/torch/onnxruntime；libvips 未接入运行时，构建不联网拉取依赖。 |
| 不通过 | S-25 | 壳层启动参数 `--page home/library/refine/creative` 直接以已登录展示状态进入业务页（`apps/shell/main.cpp:42-43`，`ShellDisplay::home()/entered_project()`），不经过会话。控件虽禁用，但“未登录只有登录页和帮助可达”没有由会话状态驱动；若该参数仅供开发/验收，应在发布构建中关闭并写明。 |
| 未实现 | S-03、S-04（CUBE/预设口令）、S-08、S-15、S-18、S-19、S-27–S-32 | 无 XMP/CUBE 解析、快捷键自定义、本地会话持久化与退出清理、kernel-cli、生成进程 IPC、单实例、更新包校验、外部购买链接与商家共享校验。`services/account` 只有内存会话模型。 |

## 第 4 关：性能

**结论：不通过。**

| 结论 | 编号 | 证据与差异 |
| --- | --- | --- |
| 通过（已有子路径） | P-01、P-02、P-06、P-21 | 账本与导出日志加载用单条 `LEFT JOIN`（`sqlite_ledger_log.cpp:62`、`sqlite_export_log.cpp:312`）；`list_photos` 只读索引字段，不读参数正文；预览写入不编码、不写盘（`test_c24_previews_do_not_write_or_bump_revision`）；导出只读任务快照。 |
| 通过（本轮新增子路径） | P-05、P-10（数据层）、P-13 | `kernel/preview` 只保留一个待画值、旧序号被替换（`test_preview_coordinator`）；`kernel/ipc` 预览请求与帧最长边 ≤ 4000；`services/config` 内存缓存与默认表回退（`test_config_store`）。 |
| 不通过 | P-12 | 只有项目、星级、旗标、色标、回收站索引（`sqlite_project_store.cpp:39-43`）；格式、修图状态、导出状态、文件名、拍摄时间、编辑时间没有索引或字段。 |
| 不通过 | P-07、P-22、P-25 | 文档写入按路径 `flock` 串行，不共用全局锁，这一半成立；但修订号的比较不在跨进程串行区内（第 1 关探针：两进程得到相同修订号）；没有单实例，两个进程可同时打开同一套 SQLite 写入。 |
| 不通过 | P-16、P-18 | 没有 2 秒写盘判定与耗时记录；极速导出门闩只在本进程登记。 |
| 不通过（观察） | P-23 | 事件不丢、同任务不重复记账有测试；但 `CreditLedger::creative_points/export_sheets` 每次查询都对全部事件重新 `fold`（`kernel/ledger/src/credit_ledger.cpp:116-121`），事件数随使用时长线性增长，需按 2000 张/长期账本量复核。 |
| 未实现 | P-03、P-04、P-08、P-09、P-11（GPU/共享内存部分）、P-14、P-15、P-17、P-19、P-20、P-24（生成路径） | 没有 `cache/thumbs` 缩略图生命周期；renderd 只有 reference 执行和进程边界，没有真实解码缓存与离屏上下文供画布读取；壳层滑杆全部禁用，无法验证 P-15；没有生成调度与并发上限；样片库没有像素路径。 |

## 第 5 关：代码质量

**结论：不通过。**

| 结论 | 编号 | 证据与差异 |
| --- | --- | --- |
| 通过（已有模块） | Q-01、Q-02（§4 列表内标识）、Q-03、Q-05、Q-07、Q-08、Q-09、Q-10、Q-13、Q-14、Q-15（内核）、Q-16、Q-17、Q-18、Q-19、Q-22、Q-24、Q-26–Q-32 | 所有 CMake 目标带 `pixcake_` 前缀；结果码枚举语义一致；`CommandGate` 只做分发与门闩，不含 SQL/OpenGL/色彩公式；内核文件存显示值；`detail` 只保留标识；配置默认值集中在 `services/config`；`manual` 增量由 `kernel/manual` 持有；GPU native 后端不含 Qt 类型（`arch_dependencies`）；表情/牙齿/眼睛/妆容/头发仍是 `portrait.feature` 参数组；撤销链不写盘。 |
| **否决：不通过** | Q-06、Q-20 | 滑杆范围、步进、默认值写在 `apps/shell/main.cpp:264-330` 的 lambda 常数里（见第 1 关 C-38），而不是来自参数模式；`ui/shell/color_adjustment_panel.hpp` 的参数模式消费模型未被壳层使用。 |
| 不通过 | Q-02（§4 外标识） | 参数模式使用 `portrait.eye_glint`、`pet.eye_glint`、`light.uniform`、`mask` 作为能力标识写入文档片段，与契约 §4 注册标识列表不一致；`temperature`/`color_temperature`、`tint`/`color_tint` 同义双名。需上位文档确认或改为既有能力内的参数。 |
| 不通过 | Q-11、Q-12、Q-04 | 页面文字同时维护在 `kernel/workspace/src/workspace_display.cpp`、`ui/shell/src/shell_display.cpp` 和 `apps/shell/main.cpp`（如“查看快捷键”“联系售后”三处各一份）；`main.cpp` 572 行中 26 行超过 300 字符，一个函数内同时搭页面、写死参数与无障碍文案，可读性与单一职责不足。 |
| 不通过 | Q-25 | `kernel/ipc` 预览协议与 `apps/renderd` 服务各自持有请求字段；目前一致，但没有共享字段定义或一致性测试来防止漂移。证据不足，记不通过。 |
| 不通过 | Q-34、Q-35 | 同第 3 关 S-33（手写图像头解析无例外记录）、S-34（Glad 无依赖记录与许可证文本）。 |
| 通过 | Q-33 | 没有为 Free 分组建参数模式或着色器（`phase_one_parameter_modes()` 不含色阶/曲线/HSL 等；`gpu/render` pass registry 无对应 program）。Free 分组的界面问题已在第 1 关 C-02 记录。 |
| 未实现 | Q-21、Q-23 | 蒙版部位枚举与快捷键表不存在。 |

## 第 6 关：测试

**结论：不通过。**

| 结论 | 编号 | 证据与差异 |
| --- | --- | --- |
| 通过 | T-01、T-06、T-07、T-10、T-20 | 51/51 CTest 通过；内核测试镜像被测目录，内核目标不链接 Qt；`tests/arch/check_dependencies.cmake` 含 rule 1–15，违反即构建失败；账本测试用临时 SQLite；`test_gpu_backend_C57_C60` 在 native 上下文实际执行并断言与 reference 输出一致，`test_gpu_backend_qt` 覆盖 Qt 后端。 |
| 通过（已有子路径） | T-13、T-14、T-15、T-16、T-19 | `test_atomic_file`、`test_document_recovery`、`test_edit_document`、`test_export_queue`、`test_source_presence` 覆盖写盘中断、截断隔离、未知字段保留、导出崩溃各阶段与原图路径恢复。 |
| 不通过 | T-02、T-03 | 第 2 关仍有大量 B 行没有用例；第三方坏返回、超时、生成进程崩溃没有测试替身场景。 |
| 不通过 | T-04、T-05、T-18 | 缺跨进程集成：本轮两个探针（跨句柄、跨进程提交）在仓库里没有对应测试，所以 CTest 全绿却漏掉 C-23/B-60 否决；极速导出跨进程场景没有测试。 |
| 不通过 | T-08 | `tests/recovery` 只有 `test_document_recovery.cpp`；导出、账本、删除原图的恢复测试分散在各模块测试中，没有统一的崩溃/重放组合镜像。 |
| 必须补 | T-09 | 170 个测试函数中 40 个名称带清单编号（23.5%），按本轮统计口径（`void test_*` 函数名）比例低于上一轮记录的约 40/127，新增测试大多不能按编号追溯。 |
| 不通过 | T-22 | libvips 尚未集成，因此没有第三方库集成测试；Glad 只有 smoke，没有版本锁定构建与不可用场景测试。 |
| 未实现 | T-11、T-12、T-17、T-21 | 无生成管线、`kernel-cli`、生成进程崩溃恢复和单实例。 |

## 第 7 关：日志、指标、回滚

**结论：不通过。**

| 结论 | 编号 | 证据与差异 |
| --- | --- | --- |
| 通过（已有子路径） | O-02、O-03、O-21、O-22、O-24、O-27、O-28、O-29、O-32、O-33、O-34、O-35 | 日志结构不含像素/凭证，路径可记；原子替换、隔离与备份、导出临时文件校验、账本重放修正缓存、回收站 30 天、未知字段保留与“参数已恢复为空”标记均有测试（`test_atomic_file`、`test_document_recovery`、`test_export_queue`、`test_ledger`、`test_recycle_bin`）。 |
| 不通过 | O-01、O-04、O-05 | 写 `LogRecord` 的仍只有 `CommandGate` 与 `ProjectLibrary`（`kernel/command/src/command_gate.cpp`、`kernel/project/src/project_library.cpp`）。导出提交、账本、原子替换、隔离/空文档恢复、管线环路、导入、renderd/exportd 都没有持久日志与开始/完成事件。 |
| 未实现 | O-06–O-12 | `kernel services apps` 中没有任何计数器/指标类型：预览合并数、自动保存耗时、导出/生成终态分布、恢复次数、磁盘满次数、重复记账拦截都无运行时记录。 |
| 不通过或未实现 | O-13–O-20 | 内核已有区分清楚的结果码与提示文字（如样片上限、`kDiskFileRemains`），但壳层所有业务按钮禁用，没有把这些结果呈现给用户的链路；登录/验证码流程不存在。 |
| 未实现 | O-23、O-25、O-26、O-30、O-31 | 无生成落层、更新器、卸载器。 |

## 打回项关闭对照表（对照 2026-11-04 复审）

| # | 上一轮打回项 | 本轮判定 | 证据 |
| --- | --- | --- | --- |
| 1 | 所有写入口收进同一套命令/门闩/修订提交边界，跨进程使用持久版本条件 | **未关闭** | 新增 `WriteScope`（路径 `flock`）与极速导出门闩登记，单进程路径成立；但修订比较只看句柄内存，`CommandGate` 修订表在进程内。探针：跨句柄、跨进程的过期提交都返回 `Ok` 并覆盖新内容。`EditDocument` 的 `commit_*` 仍是公开入口。 |
| 2 | 范围、步进、默认值、禁用原因、能力资格收进真正的参数模式，覆盖 C-36–C-39、B-27/B-32/B-33 | **部分关闭** | `ParameterMode` 已是内核唯一校验来源，C-36–C-39 与需求列出的其他范围正确，步进校验成立（探针）。缺：禁用原因/能力资格字段、黑白档规则；壳层另写一套冲突范围（曝光、程度写成 -100–100）；导出品质仍是字符串搜索（B-32）；不透明度、祛路人羽化不在模式表里。 |
| 3 | 项目删除的磁盘与索引原子性，补故障注入测试 | **关闭** | “删除待完成”意图先落库、按登记身份删除、终态写失败后启动收敛；`test_delete_source_file_recovers_when_final_presence_write_fails`、`test_delete_uses_enrolled_file_identity`。 |
| 4 | SafeList：动态 SQL 白名单、导入内容/尺寸验真、路径书签/身份、单实例/会话 | **部分关闭** | SQL 白名单保持；导入协调器先验内容与尺寸。缺：`ProjectLibrary::add_photo` 公开旁路；安全范围书签；单实例；持久会话。新增问题：手写格式解析没有依赖例外记录（S-33）。 |
| 5 | 消除 N+1，补缩略图、渲染/生成调度、配置缓存、跨进程取消/超时 | **部分关闭** | N+1 已消除；配置缓存与预览最新值合并已有；IPC 有取消消息编解码。缺：`cache/thumbs`、生成调度、跨进程超时、账本每次全量 fold 的复核。 |
| 6 | 补齐或明确标记未实现模块；页面 smoke 不得作为证据 | **部分关闭** | 已新增 `kernel/import`、`kernel/render`、`kernel/generate`、`kernel/ipc`、`services/config`、`services/account`、`apps/renderd`、`apps/exportd`、`gpu/backend/native`，各自验收记录都写明了范围。仍缺：`kernel-cli`、生成进程、持久日志/指标、更新器、卸载器。 |
| 7 | F/B/C/O 故障组合进自动化测试，测试名带编号，修正门禁路径后重跑 | **部分关闭** | 门禁路径已修正且通过，51/51 重跑通过。缺：跨进程组合测试（正是本轮否决项的漏网原因）；带编号测试比例 23.5%；`tests/recovery` 仍只有一个文件。 |

## 本轮新发现

1. **壳层 Free 分组界面（C-02，否决）**：精修面板为色阶、曲线、HSL、可选颜色、颜色分级、色彩编辑器、细节、颗粒、镜头调整、透视矫正、校准、AI 校准建了禁用控件。与“官方界面优先”的视觉计划冲突，需要上位裁决。
2. **壳层写死滑杆范围（C-38、Q-06/Q-20，否决）**：曝光、程度等写成 -100–100，与内核参数模式冲突；部分范围的证据自承来自“市面惯例”。
3. **手写图像头解析没有依赖例外记录（S-33/Q-34）**；**Glad 没有依赖记录与许可证文本（S-34/Q-35）**。
4. **注册表外的能力标识**：`portrait.eye_glint`、`pet.eye_glint`、`light.uniform`、`mask`，以及温度/色调同义双名（C-31/Q-02）。
5. **未登录可直接打开业务页面**（S-25）；**`add_photo` 在文件不存在时仍可写入**（S-07）。

## 总结论

**打回重写。** 第 1 关仍有否决项：C-23/B-60/B-73 的跨句柄、跨进程丢失更新（探针实证），以及壳层的 C-02、C-38 冲突。7 条打回项中 1 条关闭、5 条部分关闭、1 条未关闭。51/51 CTest 全绿是真实结果，但测试集中没有跨进程场景，所以不能证明这些否决项已关闭。

## 打回项与复审条件（本轮）

1. **跨进程修订条件**：提交在 `WriteScope` 内重读持久修订，过期即 `StateConflict`；`EditDocument`/`HistoryLog`/`LayerDocument` 的提交方法只经命令入口；补跨句柄、跨进程测试（测试名带 C-23、B-60、B-73）。
2. **C-02 冲突裁决**（需用户/上位文档决定）：要么修订需求、架构和清单，允许 Free 分组做只读展示；要么从壳层移除这些控件。裁决之前 C-02 保持不通过。
3. **壳层参数来源**：精修滑杆改由 `ui/shell` 的参数模式消费模型生成，删掉 `main.cpp` 中的范围常数；没有手册或附录 B 来源的范围不写进代码。
4. **参数模式补全**：加禁用原因与能力资格字段、黑白档规则；导出设置改成结构化字段并按模式校验 `quality`（B-32）；确认或收回 §4 之外的能力标识。
5. **导入与依赖**：`add_photo` 不再作为未验真的公开入口；为 `source_probe` 补依赖例外记录，或把内容验真移到解码适配器；补 Glad 的依赖记录与许可证文本。
6. **可观测性**：`LogSink` 接入导出、账本、原子替换、恢复和 renderd/exportd，补开始/完成事件和 O-06–O-12 计数。
7. **单实例与跨进程极速导出**（B-62、B-67、S-29、P-25）。

上述 1、3、4 关闭且 2 有裁决后，重新运行全量构建、CTest、架构门禁与本轮两个探针，再做第四轮复审。
