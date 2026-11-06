# 审核记录：2026-11-06，七关第七轮复审（HEAD `03b8cf5`）

## 结论

**打回重写。** 第六轮复审条件 1–4 **全部关闭**，C-31 本轮判**通过**。两个否决探针在 HEAD 上仍然成立。仍然打回，是因为第 1 关还有 C-30、C-33 两条否决行（第五轮条件 5，未在本轮范围内），以及第 5 关 Q-06 被重新判定：蒙版密度的默认值 100 写在 `layerdoc` 代码里，没有参数模式（第六轮把它记为观察，本轮按 Q-06 原文判定）。

依据：审核总则与七份清单；对照 `2026-11-06-pixcake-seven-gate-review-r6.md`。整改计划：`docs/superpowers/plans/2026-11-06-r6-conditions.md`（含 Rulings）；本轮计划：`docs/superpowers/plans/2026-11-06-seven-gate-review-r7.md`。只读审核；探针在 scratchpad，未提交。

**复审方式（增量）**：`30e0835..03b8cf5`。构建与测试在 HEAD 的干净工作树（`git worktree`）里做：工作目录里另一个会话的分割改动（`apps/renderd`、`tests/CMakeLists.txt` 里的 `test_segmentation_contract`）没有提交，不计入本轮。改动过证据文件的行重新取证，其余行沿用第六轮结论，表中写“沿用 r6”。审核者与整改作者是同一人，没有独立审核者。

## 验证命令

```text
git worktree add --detach <scratchpad>/wt-r7 HEAD
cmake -S <scratchpad>/wt-r7 -B <scratchpad>/build-r7 -DCMAKE_BUILD_TYPE=Debug   # 全新目录
cmake --build <scratchpad>/build-r7 --parallel 8
ctest --test-dir <scratchpad>/build-r7 --output-on-failure
git diff --check
```

结果：**构建成功（4 条链接器重复库警告，与第六轮同类）；69/69 CTest 通过，26.2 秒；`git diff --check` 通过。** GPU 两个 readback 测试实际执行（`GPU shader path active`、`native GPU backend active`）。CTest 新增：`test_photo_status`。

测试函数 275 个，名称带清单编号 145 个（52.7%；第六轮 258 / 128）。

## 本轮探针

| 探针 | 场景 | 实际结果 | 判定 |
| --- | --- | --- | --- |
| `probe_cross_handle_commit`（按 HEAD 重编） | 两句柄在修订 0 装载；A 提交 `1.00`，B 以过期修订提交 `-1.00` | A 成功；B 状态冲突；磁盘修订 1、`1.00` | 成立 |
| 同上（参数段） | 曝光 `0.03`、`5.05`、`abc`；对比度 `1.5`；未声明参数；`hsl/hue_red` | 提交全部状态冲突；色温 `100` 接受 | 成立 |
| `probe_cross_process_gate` × 20 | 两进程同时以修订 0 提交 `2.00` / `-2.00` | 20/20 恰好一方成功、另一方状态冲突；磁盘修订 1 且等于成功方（20/20 核对）；两种顺序各 11 / 9 次 | 成立 |

## 第六轮复审条件核对

| # | 第六轮条件 | 本轮判定 | 证据 |
| --- | --- | --- | --- |
| 1 | Q-06 收尾：选区解码查参数模式 | **关闭** | `layer_document.cpp` 选区记录解码改为 `within_mode("mask", "selection_feather", …)`；`test_q06_selection_feather_at_mode_maximum_round_trips`（存读 1000）；只改解码一行的变异使它失败。Q-06 本行另有新判定，见第 5 关。 |
| 2 | C-31 续做 | **关闭** | 见第 1 关 C-31。 |
| 3 | 状态接线 | **关闭（内核层）** | `CommandGate::set_commit_listener`；提交落盘成功后 `PhotoEditRecorder` 把绑定的图片记为“已编辑”并写编辑时间（`test_p12_commit_marks_photo_edited`）；预览、写盘失败、过期修订都不记（`test_p12_failed_commit_does_not_mark_edited`）；记录失败不改变提交结果，写 `project.status.failed`（`test_p12_status_write_failure_is_logged`）。`record_export_statuses` 把成品存在的导出任务投影为“已导出”（`test_p12_exported_products_mark_photos_exported`）。变异“提交前通知”得 3 处失败，“不看成品是否存在”得 1 处。遗留见本轮新发现 2。拍摄时间仍为 0，原因见整改计划 Rulings（内核不读 EXIF，解码适配器未接入）。 |
| 4 | 账本移动语义 | **关闭** | 移动构造与移动赋值给被移走的对象留下空事件和空快照；`test_p23_moved_from_ledger_is_empty`（改前段错误退出 139）。 |

## 七关增量判定

### 第 1 关：业务契约 — 不通过，有否决项

| 结论 | 编号 | 证据与差异 |
| --- | --- | --- |
| **通过** | C-31 | (1) 契约 §4 的 20 个能力在 `kernel/capabilities/<能力>/` 各有目录、头文件、源文件和工厂 `make_<能力>_module`；`module_set` 用“标识 → 工厂”表组装，没有工厂的标识没有模块（`test_c31_every_capability_has_its_own_directory`、`test_c31_every_registered_capability_has_a_module`；删掉一行工厂得 2 处失败）。(2) 架构 7.2 的九项操作由 `CapabilityModule` 定义、`ParameterBackedModule` 执行，另加生成入口与跳转编译；`detail` 在自己目录里派生（按阶段编译）。(3) 禁用原因按架构 7.2 新增的禁用条件表：只有 `portrait.body` 会坏片，五个人像能力检测未完成时不画，`image.sky` 缺素材不可用，`image.background` 的光影均匀在非纯色背景下无效果、不报错（`test_c31_only_body_reports_broken_image`、`test_c31_portrait_needs_detection`、`test_c31_sky_needs_assets`、`test_c31_solid_background_parameter_has_no_effect_on_other_backgrounds`）。(4) 九个跳到创意的入口按架构 7.5 编号表声明在对应能力上，编译成跳转（`test_c31_generation_entries_compile_to_jumps`）。(5) 预设、批量同步、粘贴经 `transfer_fragment`（模块迁移、提取、取值校验）写入（`test_c31_transfer_*`、`test_c31_preset_apply_validates_through_capability_modules`、`test_c31_sync_and_paste_validate_through_capability_modules`）。(6) 能力目录互不包含、只链接规则 4 允许的库（门禁规则 2、2b）。 |
| 通过 | C-05 | 沿用 r6；配置文件切换由参数模式禁用规则表达，`test_c31_color_global_draws_by_profile` 不变。 |
| **否决：不通过** | C-30、C-33 | 沿用 r6。 |
| 未实现 | C-02（部分） | 沿用 r6。 |
| 沿用 r6 | 其余行 | |

### 第 2 关：边界条件 — 不通过，有否决项

| 结论 | 编号 | 证据与差异 |
| --- | --- | --- |
| 通过 | B-33 | 沿用 r6，选区羽化的解码也改查参数模式（条件 1）。 |
| 沿用 r6 | 其余行 | |

### 第 3 关：安全 — 不通过，有否决项

| 结论 | 编号 | 证据与差异 |
| --- | --- | --- |
| 通过 | S-01 | 沿用 r6；本轮没有新增 SQL。 |
| **否决：不通过** | S-10、S-25 | 沿用 r6。 |

### 第 4 关：性能 — 本轮无否决项

| 结论 | 编号 | 证据与差异 |
| --- | --- | --- |
| 通过 | P-12 | 沿用 r6；修图状态、导出状态现在有写入方（条件 3）。 |
| 通过 | P-23 | 沿用 r6；移动后的对象可安全查询（条件 4）。 |
| 沿用 r6 | 其余行 | |

### 第 5 关：代码质量 — 不通过，有否决项

| 结论 | 编号 | 证据与差异 |
| --- | --- | --- |
| **否决：不通过（本轮重新判定）** | Q-06 | 第五、六轮列出的四处（不透明度、蒙版羽化、选区羽化的编辑与解码）都已来自参数模式。剩下蒙版密度：`layer_document.cpp:486` 新建蒙版时写死默认值 `100`，`set_mask_density` 只拒绝负数、没有上限，也没有参数模式。需求只写“图层蒙版可调密度和羽化”（第 728 行），没有范围和默认值。第六轮把它记为观察；按 Q-06 原文（滑杆的默认值应来自参数模式）判不通过。 |
| **否决：不通过** | Q-11、Q-12、Q-25 | 沿用 r6。 |
| 通过 | Q-02、Q-34、Q-35 | 沿用 r6。 |
| 必须补 | Q-04 | 沿用 r6。 |

### 第 6 关：测试 — 不通过，有否决项

| 结论 | 编号 | 证据与差异 |
| --- | --- | --- |
| 必须补 | T-09 | 52.7%。 |
| **否决：不通过** | T-02、T-03 | 沿用 r6。 |
| 沿用 r6 | 其余行 | |

### 第 7 关：日志、指标、回滚 — 不通过，有否决项

| 结论 | 编号 | 证据与差异 |
| --- | --- | --- |
| 通过 | O-04 | 沿用 r6。新增事件 `project.status.failed`（module=command）。 |
| **否决：不通过** | O-13–O-15 | 沿用 r6。 |

## 本轮新发现

1. **蒙版密度没有参数模式**（见第 5 关 Q-06）。需要 test 版实测或用户给出范围与默认值后登记 `mask/density`。
2. **不经命令入口的文档修改不记“已编辑”**：预设应用（`preset_library.cpp:271`）、批量同步与粘贴（`sync_batch.cpp:92、209`）、应用至精修的烘焙重置（`layer_document.cpp:858`）直接调用文档提交，不经过 `CommandGate`，所以不会通知 `CommitListener`。架构第 6.2 节要求用户发起的修改都从命令入口进入；这几条路径还没有对应的命令。
3. **图片条件在渲染端没有来源**：模块会按“检测未完成、缺少素材、非纯色背景”跳过绘制，但 `render::compile` 的读取方对这些条件一律返回“满足”。在接上人物检测（`kernel/subject`）、素材（`kernel/assets`）和纯色判断之前，渲染端不会因此跳过能力。
4. **并行会话隔离**：整改提交 `77ebff0` 一度带入了另一会话在 `tests/CMakeLists.txt` 里未提交的 `test_segmentation_contract` 注册（源文件未提交，干净检出会配置失败）；提交前已修正为只含本次改动，并核对过 HEAD 的 `tests/CMakeLists.txt` 引用的测试源文件都已入库。本轮构建改在干净工作树里做。
5. **观察**：轨道两端颜色仍无来源（`colors_confirmed` 为假）；壳层颗粒滑杆仍未绑定 `detail/grain_*`；应用层（`apps/`）还没有使用项目库、命令入口和导出队列，状态接线目前只在内核层成立。

## 总结论

**打回重写。** 第六轮的 4 条条件全部关闭，C-31 通过，第 4 关保持没有否决项。剩余否决行：第 1 关 C-30、C-33；第 3 关 S-10、S-25；第 5 关 Q-06（蒙版密度）、Q-11、Q-12、Q-25；第 6 关 T-02、T-03；第 7 关 O-13–O-15。

## 打回项与复审条件（本轮）

1. **蒙版密度**（Q-06）：取得范围与默认值来源（实测或用户给出），登记 `mask/density`，`layerdoc` 改查参数模式。
2. **修改都经命令入口**（架构 6.2）：预设应用、批量同步、粘贴、应用至精修改为命令，提交成功后同样通知 `CommitListener`。
3. **图片条件来源**：渲染读取方从 `kernel/subject`（检测状态）、`kernel/assets`（素材是否在本机）取条件；纯色判断的来源需要先在上位文档里定。
4. 第五轮条件 4–7（S-25、S-10；C-30、C-33、Q-25；Q-11、Q-12、O-13–O-15；T-02、T-03、T-09）仍然有效。其中 C-30、C-33 是第 1 关仅剩的两条否决行。
