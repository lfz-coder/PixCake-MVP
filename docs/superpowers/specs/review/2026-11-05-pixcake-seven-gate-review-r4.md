# 审核记录：2026-11-05，七关第四轮复审（HEAD `7eaa48a`）

## 结论

**打回重写。** 第 1 关仍有否决项：C-31（能力不是独立模块，且新增了契约 §4 之外的标识 `grain`）、C-35（依赖环路不拒绝编辑、不写日志）、C-05（黑白档没有黑白组）、C-36–C-39 段末规则（契约写“追色和色彩风格程度 0 到 200”，代码是 0–100，文档未同步；其中追色没有任何实测或裁定来源）。

第三轮的 7 条复审条件：**4 条关闭，3 条部分关闭**。第三轮两个否决探针（跨句柄、跨进程丢失更新）已翻转：20 轮并发全部只有一方成功，磁盘保留成功的一方。

依据：`2026-11-02-pixcake-review-index.md` 与七份清单；对照 `2026-11-05-pixcake-seven-gate-review-r3.md` 的“打回项与复审条件”和“后续裁决”。计划：`docs/superpowers/plans/2026-11-05-seven-gate-review-r4.md`。本轮只读审核，未改产品代码、清单或上位文档；探针在会话 scratchpad，未提交。

**复审方式（增量）**：`1de349e..7eaa48a` 共 14 个提交、113 个文件。证据文件在这段改动里出现过的清单行重新取证；其余行沿用 r3 结论，表中写“沿用 r3”。沿用的行没有重新逐行读源码，这是本轮复审的边界。

## 验证命令

```text
cmake -S . -B <scratchpad>/build-r4 -DCMAKE_BUILD_TYPE=Debug
cmake --build <scratchpad>/build-r4 --parallel 8
ctest --test-dir <scratchpad>/build-r4 --output-on-failure
git diff --check
```

结果：**构建成功（2 条链接器重复库警告，`libpixcake_ledger.a`、`libpixcake_exportq.a`）；58/58 CTest 通过，19.4 秒；`git diff --check` 通过。** GPU 两个 readback 测试在本机桌面会话中实际执行（输出 `active`）。

当前模块：`kernel/{assets,capability,command,cull,editdoc,exportq,generate,history,import,ipc,layerdoc,ledger,manual,mask,persist,preset,preview,project,recipe,render,subject,support,sync,workspace}`、`services/{account,config,instance,logging}`、`apps/{exportd,renderd,shell}`、`gpu/{backend,facade,render}`、`storage/sqlite`、`ui/{panels,shell}`。仍不存在：`apps/kernel-cli`、生成进程、`cache/thumbs`。

测试函数 231 个，名称带清单编号 101 个（43.7%；口径：`void test_*` 函数名含 `_<字母><数字>` 编号段）。r3 为 40/170（23.5%）。

## 本轮探针

| 探针 | 场景 | 实际结果 | 判定 |
| --- | --- | --- | --- |
| `probe_cross_handle_commit`（r3 原样，按新接口重编） | 两个 `EditDocument` 句柄在修订 0 装载；A 提交曝光 `1.00`，B 以过期修订 0 提交 `-1.00` | A `成功`；**B `状态冲突`**；磁盘修订 1、曝光 `1.00` | C-23 关闭 |
| 同上（参数段） | 曝光 `0.03`、`5.05`、`abc`；对比度 `1.5`；未声明参数；`hsl/hue_red` | 全部 `状态冲突`；色温 `100` 被接受 | 与 r3 一致 |
| `probe_cross_process_gate` × 20 轮 | 两个进程各自 `CommandGate`，都在修订 0 装载，同时放行，分别提交曝光 `2.00` / `-2.00` | **20/20 轮恰好一方 `成功`（修订 1）、另一方 `状态冲突`（修订 0）；磁盘值等于成功方**；先后两种顺序都出现（9 次 / 11 次） | C-23、B-60、B-73 关闭 |

修复点：`b75c7e8`，提交在写锁内重读持久修订（`test_c23_check_durable_revision`）。`EditDocument::commit_*` 仍是公开方法，但检查在方法内部，不依赖 `CommandGate`，所以 r3 拒绝的“只在门闩里补一层”的写法没有出现。

## 第 1 关：业务契约

**结论：不通过，且有否决项。**

| 结论 | 编号 | 证据与差异 |
| --- | --- | --- |
| 通过（关闭 r3 否决） | C-23 | 见“本轮探针”。仓库测试：`test_c23_b73_stale_commit_preview_is_rejected`、`test_c23_b73_stale_fragment_replacement_is_rejected`、`test_c23_b73_stale_baked_reset_is_rejected`、`test_c23_b73_stale_layer_save_is_rejected`、`test_c23_stale_write_snapshot_is_rejected`、`test_b60_c23_cross_process_gate_rejects_stale_commit`。 |
| 通过（已有子路径） | C-21、C-25、C-26 | C-21：`SqliteFastExportLatch` 每次重读导出事件表（`test_b62_other_process_sees_fast_export`、`test_b62_fast_export_stays_until_the_switch_is_turned_off`）。C-25/C-26：多句柄下过期提交不再覆盖别人已提交的修订（同上）。 |
| 通过（关闭 r3 否决） | C-38、C-36、C-37、C-39（界面一侧） | 精修滑杆全部由 `ui/panels` 的 `slider_spec` 从参数模式生成（`apps/shell/main.cpp:271-284`），控件源码没有上下限常数；壳层冒烟 `check_refine_sliders` 要求 95 个滑杆与参数模式一致（`test_shell_app_smoke_refine`）；`test_c38_exposure_spec_comes_from_parameter_mode`、`test_c46_subgroup_labels_bind_to_kernel_modes`。图库 `library-grid-span` 的 1–7 是视图设置，来自需求第 295 行，不属于参数模式。 |
| **否决：不通过** | C-36–C-39 段末规则（契约清单第 99 行） | 契约写“追色和色彩风格程度 0 到 200、默认 100”，`color.match`、`color.style` 的 `amount` 已改为 0–100（`2cc2916`），只改了需求文档，没改契约清单和边界清单 B-33。按总则第 60 行，文档与代码冲突时本轮不通过。**另有来源问题**：用户裁定的是“创意色彩程度按官方”，实测证据（`2026-11-06-free-group-creative-amount-measurement.md`）只覆盖“AI 色彩风格”的程度；**AI 追色的程度没有实测、也没有裁定**，却一并改成了 0–100，需求文档里三处追色描述也被改动。两个 `amount` 的默认值 100 也没有实测（样本读到 64）。 |
| **否决：不通过** | C-31 | 沿用 r3：能力没有各自独立实现架构 §7.2 的提取、写回、编译、迁移，只有描述表和参数模式表。**新增**：`grain`（`2cc2916`）是契约 §4 第 74 行之外的能力标识；§4 第 413 行与架构第 429 行把颗粒归在 `detail`。同时注册表里一直没有 `detail` 描述（只作管线步骤），与 §4“本阶段注册并实现的标识”含 `detail` 不一致。r3 指出的 `portrait.eye_glint`、`pet.eye_glint`、`light.uniform` 已收回到既有能力（`test_c31_parameter_modes_use_registered_capabilities`）。 |
| **否决：不通过** | C-05 | 黑白档下鲜艳度、饱和度、24 项 HSL 不可编辑（`test_c05_monochrome_disables_vibrance_and_saturation`、`test_c43_hsl_modes_and_monochrome_switch`、`test_c05_monochrome_profile_blocks_vibrance_edits`）。缺“换成黑白组”：没有黑白组参数，界面也没有切换。 |
| **否决：不通过** | C-35 | 沿用 r3：`phase_one_pipeline_order` 遇环返回空，没有拒绝编辑和日志路径。日志证据 `2026-11-05-logging-and-metrics.md` 第 37 行自记为“仍未关闭”。 |
| 不通过（来源待入上位文档） | C-02 | 12 个分组不再因 Free 被排除（`ecf49dc`）。已注册：色阶 RGB 5 项、HSL 24 项、可选颜色 36 项 + 调整方式、颜色分级 13 项、色彩编辑器偏移 3 项、颗粒 7 项。其中 HSL 饱和度/明亮度、可选颜色、颗粒的范围来自用户在对话中给出的数值，已写进证据文件，但没有写进需求或架构附录 B；C-02 要求来源是“需求原文或实测”。曲线、细节、镜头调整、透视矫正、校准、AI 校准没有行为实现（按用户裁定暂缓），记未实现。 |
| 通过（已有子路径） | C-15 | `test_c15_missing_file_after_admission_is_not_added`。 |
| 沿用 r3 | 其余行 | C-01、C-03、C-08–C-14、C-16–C-20、C-22、C-24、C-27、C-28、C-32、C-40–C-52、C-54、C-57、C-59–C-61 通过（已有子路径）；C-30、C-33 不通过；C-04、C-06、C-07、C-29、C-34、C-53、C-55、C-56、C-58、C-62 未实现。 |

## 第 2 关：边界条件

**结论：不通过，且有否决项。**

| 结论 | 编号 | 证据与差异 |
| --- | --- | --- |
| 通过（关闭 r3 否决） | B-60、B-73（文档路径） | 见“本轮探针”；`test_b60_simultaneous_commits_serialize_to_one_revision`、`test_b60_two_gates_share_document_submission_state`、`test_b73_stale_revision_does_not_overwrite`、`test_b73_older_revision_does_not_append`。 |
| 通过 | B-62 | `test_b62_other_process_sees_fast_export`、`test_b62_empty_queue_keeps_fast_export`、`test_b62_unreadable_log_fails_closed`（读不到时按开启处理）。 |
| 通过（已有子路径） | B-67 | `services/instance`：`test_b67_second_claim_sees_running_primary`、`test_t21_second_launch_activates_primary`、`check_shell_single_instance.sh`。壳层目前还不打开数据库，“锁在开库之前”靠启动顺序保证，证据文件已注明。 |
| 通过 | B-32 | 导出设置改为结构化字段：`test_b32_export_settings_parse_strictly`、`test_b32_enqueue_rejects_duplicate_quality`、`test_b32_enqueue_stores_canonical_settings`。 |
| 通过（已有子路径） | B-27 | 界面端点来自参数模式（第 1 关 C-38）。 |
| **否决：不通过** | B-33 | 清单写“追色程度不是 0 到 200 → 拒绝”，代码拒绝 101–200（第 1 关）。另沿用 r3：图层不透明度、祛路人选区羽化 0–1000 不在 `phase_one_parameter_modes()`。 |
| 沿用 r3 | 其余行 | 通过、不通过（B-52：仍无 2 秒判定）、未实现各项与 r3 相同。 |

## 第 3 关：安全 SafeList

**结论：不通过，且有否决项。**

| 结论 | 编号 | 证据与差异 |
| --- | --- | --- |
| 通过 | S-07 | `add_photo` 只接收 `AdmittedSource`，构造函数私有，只有 `SourceAdmission` 能签发；写入前重新识别文件身份（`test_s07_admitted_source_is_required_to_add_photo`、`test_s07_replaced_file_after_admission_is_rejected`）。 |
| 通过 | S-33 | `source_probe` 手写例外记录（候选、排除理由、范围、测试向量、替换路径）：`docs/superpowers/evidence/2026-11-05-dependency-records.md`。 |
| **否决：不通过** | S-34 | Glad 已有版本、生成命令、SPDX 与三个文件的 SHA-256（`third_party/glad.lock.cmake`）；但许可证全文不在仓库、生成结果未复现、其他生成器未比较，证据第 21–24 行自记为发布前阻塞项。 |
| 通过（已有子路径） | S-29 | `test_s29_concurrent_claims_have_one_winner`、`test_s29_killed_primary_releases_lock`。 |
| 沿用 r3 | 其余行 | S-10、S-25 不通过；S-03、S-04、S-08、S-15、S-18、S-19、S-27、S-28、S-30–S-32 未实现。 |

## 第 4 关：性能

**结论：不通过，且有否决项。**

| 结论 | 编号 | 证据与差异 |
| --- | --- | --- |
| 通过 | P-07、P-22 | 文档写入按路径加锁，修订比较与写盘在同一锁内（第 1 关探针）；不同文档、账本、导出各自独立。 |
| 通过（已有子路径） | P-25、P-18 | 单实例锁（第 3 关 S-29）；极速导出跨进程可见（第 2 关 B-62）。壳层尚未开库。 |
| **否决：不通过** | P-16 | 写盘耗时已记入 `persist.write.finish`（`atomic_file.cpp:250-254`），但仍没有“超过 2 秒”的判定，提交是同步写盘。 |
| 沿用 r3 | 其余行 | P-12、P-23 不通过；P-03、P-04、P-08、P-09、P-11、P-14、P-15、P-17、P-19、P-20、P-24 未实现。 |

## 第 5 关：代码质量

**结论：不通过，且有否决项。**

| 结论 | 编号 | 证据与差异 |
| --- | --- | --- |
| 通过（关闭 r3 否决） | Q-06、Q-20 | 范围、步进、默认值只在 `kernel/capability` 的参数模式；壳层与 `ui/panels` 只做绑定（第 1 关 C-38）。 |
| **否决：不通过** | Q-02 | 新增能力标识 `grain` 不在契约 §4（见第 1 关 C-31）；`color.global` 同义双名 `temperature`/`color_temperature` 已收回（`test_c31_parameter_modes_use_registered_capabilities`）。 |
| **否决：不通过** | Q-35 | 同 S-34。 |
| 通过 | Q-34 | 同 S-33。 |
| 沿用 r3 | Q-25、Q-11、Q-12、Q-04 | 不通过：预览协议字段无共享定义；页面文字多处维护；`main.cpp` 仍有大量超长行。 |
| 沿用 r3 | 其余行 | Q-33 通过（没有为任何分组写无来源的着色器）；Q-21、Q-23 未实现。 |

## 第 6 关：测试

**结论：不通过，且有否决项。**

| 结论 | 编号 | 证据与差异 |
| --- | --- | --- |
| 通过 | T-01、T-05、T-20 | 58/58；GPU readback 在 native 与 Qt 桌面平台实际执行。 |
| 通过（已有子路径） | T-04、T-18、T-21 | r3 漏掉否决项的原因（没有跨进程测试）已补：`test_b60_c23_cross_process_gate_rejects_stale_commit`（`posix_spawn` 子进程）、`test_b62_*`、`test_t21_second_launch_activates_primary`。 |
| 必须补 | T-09 | 带编号 43.7%（r3 23.5%），仍有一半以上不能按编号追溯。 |
| **否决：不通过** | T-08 | `tests/recovery` 仍只有 `test_document_recovery.cpp`。 |
| **否决：不通过** | T-22 | 沿用 r3：没有第三方库集成测试（libvips 未接入；Glad 只有冒烟）。 |
| 沿用 r3 | 其余行 | T-02、T-03 不通过；T-11、T-12、T-17 未实现。 |

## 第 7 关：日志、指标、回滚

**结论：不通过，且有否决项。**

| 结论 | 编号 | 证据与差异 |
| --- | --- | --- |
| 通过（已有子路径） | O-01、O-05 | `LogRecord` 含时间、进程、模块、结果码、修订号/任务号（`test_o01_line_has_time_process_module_code`）；写盘与导出提交的开始/完成落持久日志（`test_o05_write_start_precedes_files_and_finish_follows`、`test_o05_export_commit_start_and_finish`）；renderd/exportd 健康检查写日志。生成落层未实现。 |
| 通过（已有子路径） | O-06、O-07、O-08、O-10、O-11、O-12 | `CountingLogSink` 按事件和结果码计数：`test_o06_replaced_preview_values_are_counted`、`test_o07_commit_logs_revision_and_code`（含耗时）、`test_o08_failed_export_records_reason`、`test_o10_backup_and_empty_recovery_are_logged`、`test_o11_disk_full_is_counted_each_time`、`test_o12_replayed_stroke_is_counted_once_charged`。 |
| 通过（已有子路径） | O-09 | `test_o09_cancel_position_and_charge_are_logged`（账本侧；生成进程未实现）。 |
| **否决：不通过** | O-04 | 文档隔离、空文档恢复、重复扣点有记录；**依赖环路没有**（同 C-35）。 |
| 沿用 r3 | 其余行 | O-02、O-03、O-21、O-22、O-24、O-27–O-29、O-32–O-35 通过；O-13–O-20 不通过或未实现；O-23、O-25、O-26、O-30、O-31 未实现。 |

## 打回项关闭对照表（对照 r3“打回项与复审条件”）

| # | r3 复审条件 | 本轮判定 | 证据 |
| --- | --- | --- | --- |
| 1 | 跨进程修订条件，补跨句柄/跨进程测试 | **关闭** | 两个探针翻转（20/20）；13 个带 C-23/B-60/B-73 编号的测试。 |
| 2 | C-02 冲突裁决 | **关闭** | 用户裁决 Free 只是计费标记；上位文档与清单已修订（`ecf49dc`）。 |
| 3 | 壳层参数来源 | **关闭** | 95 个滑杆由参数模式生成，冒烟逐个核对；控件源码无范围常数。 |
| 4 | 参数模式补全（禁用原因、黑白档、B-32、§4 外标识） | **部分关闭** | 禁用规则、黑白档的鲜艳度/饱和度/HSL、结构化导出设置、r3 指出的三个 §4 外标识都已处理。新问题：`grain` 是新的 §4 外标识；黑白组未实现；程度 0–100 与契约/边界清单冲突，追色无来源。 |
| 5 | 导入与依赖 | **部分关闭** | `add_photo` 旁路关闭（S-07）；`source_probe` 例外记录（S-33）。Glad 许可证全文与生成复现仍是阻塞（S-34/Q-35）。 |
| 6 | 可观测性 | **部分关闭** | 持久日志与计数覆盖写盘、导出、账本、预览、恢复、renderd/exportd。依赖环路（O-04/C-35）未关闭。 |
| 7 | 单实例与跨进程极速导出 | **关闭** | B-62、B-67、S-29、P-25 有测试；壳层开库时机待实际开库时复核。 |

## 本轮新发现

1. **新能力标识 `grain`（Q-02/C-31，否决）**：契约 §4 把颗粒归在 `detail`。用户说“颗粒跟白平衡、影调同级”，白平衡和影调是同一能力里的分组，这句话更接近“颗粒在面板上是独立分组”，不一定是“新能力”。我（实现者）当时理解成新能力，没有先对照 §4。修复方向：补 `detail` 描述，把颗粒参数放到 `detail` 下（如 `grain_amount` 等），撤回 `grain`；或由用户确认后先改契约 §4 和架构，再重审。
2. **AI 追色程度被一并改成 0–100（否决，无来源）**：实测和裁定都只覆盖 AI 色彩风格。修复方向：`color.match` 恢复 0–200 并还原需求文档里三处追色描述，或实测追色后再定；同时把风格的 0–100 写进契约清单第 99 行和边界清单 B-33。
3. **用户口头给出的范围尚未写进上位文档（C-02）**：HSL 饱和度/明亮度、可选颜色、颗粒。建议写进架构附录 B，注明“2026-11-05 用户给出”。
4. **两个 `amount` 的默认值 100 未经实测**（风格样本读到 64）。

## 总结论

**打回重写。** 第三轮最严重的否决（跨进程丢失更新）已经关闭，壳层参数来源和单实例也已关闭；但第 1 关仍有 C-31、C-35、C-05 和程度范围的文档冲突，第 3、5、6、7 关各有否决项。其中 `grain` 标识和追色程度是本轮新引入的问题。

## 打回项与复审条件（本轮）

1. **能力标识回到契约 §4**：补 `detail` 描述；颗粒参数挂到 `detail`；撤回 `grain`，或先改契约再重审。
2. **程度范围**：追色恢复 0–200 或补实测；风格 0–100 写进契约清单第 99 行、边界清单 B-33；默认值实测或标为待确认。
3. **黑白组**（C-05）：实现黑白配置文件下的黑白组参数和切换。
4. **依赖环路**（C-35/O-04）：遇环拒绝开始编辑并写一条可筛选的日志，补测试。
5. **Glad**（S-34/Q-35）：取得许可证全文、复现生成；需要用户授权联网安装生成器。
6. **2 秒写盘判定**（P-16、B-52）与 **`tests/recovery` 归集**（T-08）。
7. **用户给出的范围写进附录 B**（C-02）。

1、2、3、4 关闭后，重跑全量构建、CTest、本轮两个探针，再做第五轮复审。
