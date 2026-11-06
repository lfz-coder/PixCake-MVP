# 审核记录：2026-11-06，七关第六轮复审（HEAD `30e0835`）

## 结论

**打回重写。** 第五轮复审条件 1–3 中，P-12、P-23 **关闭**；Q-06 **未关闭**（`layerdoc` 解码选区时还留着一处写死的 `1000`）；C-31 **部分落地、未关闭**（20 个能力都有实现架构 7.2 全部操作的模块，但只有 `color.global`、`detail` 是独立目录，其余 18 个共用通用实现）。两个否决探针在 HEAD 上仍然成立。第 1 关仍有 C-31、C-30、C-33 三条否决行，总结论不变。

依据：`2026-11-02-pixcake-review-index.md` 与七份清单；对照 `2026-11-06-pixcake-seven-gate-review-r5.md`。整改计划：`docs/superpowers/plans/2026-11-06-r5-conditions-1-3.md`（含 Rulings）；本轮计划：`docs/superpowers/plans/2026-11-06-seven-gate-review-r6.md`。只读审核；探针在 scratchpad，未提交。

**复审方式（增量）**：`4eb78fc..30e0835`（45 个文件，+2214/−259）。改动过证据文件的行重新取证，其余行沿用第五轮结论，表中写“沿用 r5”。本轮的审核者也是整改代码的作者，没有独立审核者；这一点由用户判断是否足够。

## 验证命令

```text
cmake -S . -B <scratchpad>/build-r6 -DCMAKE_BUILD_TYPE=Debug   # 全新目录
cmake --build <scratchpad>/build-r6 --parallel 8
ctest --test-dir <scratchpad>/build-r6 --output-on-failure
git diff --check
```

结果：**构建成功（4 条链接器重复库警告，与第五轮同类）；68/68 CTest 通过，23.5 秒；`git diff --check` 通过。** GPU 两个 readback 测试实际执行（`test_gpu_shader_path` 输出 `GPU shader path active`，`test_gpu_backend_native` 输出 `native GPU backend active`）。CTest 新增：`test_photo_query`、`test_capability_modules`。

测试函数 258 个，名称带清单编号 128 个（49.6%；同一口径下第五轮基线 `4eb78fc` 为 237 / 107）。

## 本轮探针

| 探针 | 场景 | 实际结果 | 判定 |
| --- | --- | --- | --- |
| `probe_cross_handle_commit`（按 HEAD 重编） | 两句柄在修订 0 装载；A 提交 `1.00`，B 以过期修订提交 `-1.00` | A 成功；B 状态冲突；磁盘修订 1、`1.00` | 成立 |
| 同上（参数段） | 曝光 `0.03`、`5.05`、`abc`；对比度 `1.5`；未声明参数；`hsl/hue_red` | 提交全部状态冲突；色温 `100` 接受 | 成立 |
| `probe_cross_process_gate` × 20 | 两进程同时以修订 0 提交 `2.00` / `-2.00` | 20/20 恰好一方成功、另一方状态冲突；磁盘修订 1 且等于成功方（20/20 核对）；两种顺序各出现 6 / 14 次 | 成立 |

## 第五轮复审条件核对

| # | 第五轮条件 | 本轮判定 | 证据 |
| --- | --- | --- | --- |
| 1 | 能力独立模块（C-31），先从 `color.global`、`detail` 做起 | **部分落地，未关闭** | 接口 `CapabilityModule`（`kernel/capability/include/pixcake/capability/capability_module.hpp`）覆盖 7.2 的九项操作；通用实现 `ParameterBackedModule`（`src/capability_module.cpp`）。`kernel/capabilities/color_global`（按配置文件决定哪组参数参与绘制）与 `kernel/capabilities/detail`（按阶段编译、颗粒效果为 0 时其余颗粒参数不画）是独立目录和类；`module_set` 给描述表 20 个条目各一个模块。`render::compile` 交给模块编译（`render_description.cpp`，`test_c31_render_compiles_through_capability_modules`）。`test_capability_modules` 10 个测试；变异 4 例均被测试拦下（计划 Task 4）。未关闭的原因见第 1 关。 |
| 2 | 参数来源统一（Q-06） | **未关闭** | 参数模式新增 `layer/opacity` 0–100、`mask/selection_feather` 0–1000；`set_opacity`、`set_mask_feather`、`set_selection` 与图层解码改查参数模式（`layer_document.cpp` 的 `within_mode`、`mode_maximum`），`test_q06_layer_ranges_come_from_parameter_modes`。**遗漏**：选区记录解码仍是 `feather > 1000`（`kernel/layerdoc/src/layer_document.cpp:711`）。 |
| 3a | 照片筛选排序字段与索引（P-12） | **关闭** | 照片表新增格式（准入时按内容识别）、拍摄时间、修图状态、编辑时间、导出状态；文件大小复用 `source_size`。8 个 `(project_id, 列, id)` 复合索引（`kPhotoQueryIndexes`）。`query_photos` 支持星级区间、旗标、色标、格式、修图状态、导出状态的交集筛选与需求 §6.3 六种排序。查询列只有图库元数据，不读精修参数正文。`test_p12_query_uses_project_indexes` 用 `EXPLAIN QUERY PLAN` 证明六种排序各走对应索引、没有临时排序，两种状态筛选走状态索引；去掉建索引语句后 14 处失败。旧库补列并按扩展名回填格式：`test_p12_old_library_gains_columns`。 |
| 3b | 账本增量（P-23） | **关闭** | `CreditLedger` 持有当前状态快照，`open` 时折叠一次，之后每追加一条事件只应用这一条（`append`）；余额、任务状态、是否扣过点都读快照。`test_p23_cached_snapshot_matches_full_fold`：600 步随机操作，每步与“用同一事件序列重开”比较余额、张数、全部任务状态；`test_p23_balance_queries_do_not_refold`：2 万事件、2 万次余额查询限 1 秒（原实现超过 120 秒）。变异“快照漏应用笔触事件”得 303 处失败。 |

## 七关增量判定

### 第 1 关：业务契约 — 不通过，有否决项

| 结论 | 编号 | 证据与差异 |
| --- | --- | --- |
| 通过 | C-05 | 沿用 r5 的通过结论，并加强：`ColorGlobalModule` 在黑白配置文件下不画 HSL、颜色编辑器、鲜艳度、饱和度，画黑白组；其他配置文件相反，数据不删（`test_c31_color_global_draws_by_profile`、`test_c31_render_compiles_through_capability_modules`）。 |
| **否决：不通过** | C-31 | 条件 1 部分落地。仍不通过的依据：(1) 清单要求“每个……精修能力是独立模块”，架构第 154 行“每个精修能力一个子目录”；20 个能力中 18 个是同一个通用类的实例，没有自己的目录。(2) 预设与批量同步仍直接复制、替换片段，没有经过模块的提取与写回（整改计划 Rulings 第 3 条）。(3) 禁用原因只实现了“坏片”；“非纯色、缺少素材、检测未完成”没有任何能力给出（人像、证件照、换天空等依赖检测或素材）。 |
| **否决：不通过** | C-30 | 沿用 r5。进展：能力片段正文编码只剩一处定义（`kernel/capability/fragment_codec`，`editdoc` 改为调用），字节格式不变；四进程共享字段定义与生成进程仍没有。 |
| **否决：不通过** | C-33 | 沿用 r5。 |
| 未实现 | C-02（部分） | 沿用 r5。 |
| 沿用 r5 | 其余行 | |

### 第 2 关：边界条件 — 不通过，有否决项

| 结论 | 编号 | 证据与差异 |
| --- | --- | --- |
| 通过 | B-33 | 不透明度、蒙版羽化、选区羽化的拒绝越界改由参数模式判定（`test_q06_layer_ranges_come_from_parameter_modes`、`test_layer_document`；去掉两条参数模式后两个测试分别 4、20 处失败）。裁剪角度、追色与风格程度沿用 r5。 |
| 通过 | B-52、B-60、B-62、B-67、B-73、B-32 | 沿用 r5，探针复核成立。 |
| 沿用 r5 | 其余行 | 大量登录、画布、生成、打印、更新器相关行仍为未实现。 |

### 第 3 关：安全 — 不通过，有否决项

| 结论 | 编号 | 证据与差异 |
| --- | --- | --- |
| 通过 | S-01 | 重新核对本轮新增的动态 SQL（`photo_query_sql`）：列名只来自源码常量（排序列走 `sort_column` 白名单，筛选列是调用处的字面量），所有取值用 `?` 绑定；旧库回填格式用绑定参数。没有拼接调用方文字。 |
| 通过 | S-07、S-29、S-33、S-34 | 沿用 r5。 |
| **否决：不通过** | S-10、S-25 | 沿用 r5。 |

### 第 4 关：性能 — 本轮无否决项（其余行沿用 r5）

| 结论 | 编号 | 证据与差异 |
| --- | --- | --- |
| 通过 | P-12 | 见条件 3a。 |
| 通过 | P-23 | 见条件 3b。 |
| 通过 | P-16 | 沿用 r5。 |
| 沿用 r5 | 其余行 | 第五轮本关的否决行只有 P-12、P-23，本轮均关闭；本关其余行沿用 r5，没有新的否决。 |

### 第 5 关：代码质量 — 不通过，有否决项

| 结论 | 编号 | 证据与差异 |
| --- | --- | --- |
| **否决：不通过** | Q-06 | 见条件 2：`layer_document.cpp:711` 选区解码仍写死 `1000`。其余三处编辑路径与图层解码已改查参数模式。 |
| **否决：不通过** | Q-11、Q-12、Q-25 | 沿用 r5。 |
| 通过 | Q-02、Q-34、Q-35 | 沿用 r5。 |
| 必须补 | Q-04 | 沿用 r5。 |

### 第 6 关：测试 — 不通过，有否决项

| 结论 | 编号 | 证据与差异 |
| --- | --- | --- |
| 通过 | T-08、T-22 | 沿用 r5。 |
| 必须补 | T-09 | 49.6%（第五轮 45.1%）。 |
| **否决：不通过** | T-02、T-03 | 沿用 r5。 |

### 第 7 关：日志、指标、回滚 — 不通过，有否决项

| 结论 | 编号 | 证据与差异 |
| --- | --- | --- |
| 通过 | O-04 | 沿用 r5。 |
| **否决：不通过** | O-13–O-15 | 沿用 r5。 |
| 沿用 r5 | 其余行 | |

## 本轮新发现

1. **Q-06 遗漏**：`kernel/layerdoc/src/layer_document.cpp:711` 解码选区记录时用常数 `1000` 判越界，没有查 `mask/selection_feather`。整改时只搜了第五轮列出的三行。
2. **修图状态、导出状态没有写入方**：`ProjectLibrary::mark_photo_edited`、`mark_photo_exported` 只在测试里调用；精修提交和导出成功都没有接上。按这两个状态筛选，在接线前只会命中“未编辑、未导出”。拍摄时间导入时写 0（内核不读 EXIF），按拍摄时间排序目前等同导入顺序。P-12 的索引与查询已具备，这是需求 §6.2、§6.4 的功能缺口（整改计划 Rulings 已写明）。
3. **账本移动后的对象**：`CreditLedger` 的移动构造与移动赋值取默认实现，被移走的对象快照指针为空，再调用任何查询会空指针解引用。以前被移走的对象只是没有事件，可以安全查询。目前没有产品代码使用账本，属于次要问题。
4. **门禁规则 2 扫错目录（本轮已修）**：`check_dependencies.cmake` 规则 2 原来扫 `kernel/capability/*`（实际只有 include、src 两个目录），与架构第 646 行的 `kernel/capabilities/` 不符，等于从未生效。本轮改为扫 `kernel/capabilities/*`，并新增规则 2b（能力目录只包含、只链接架构第 196 行规则 4 允许的模块）；三种临时违规（互相包含、包含渲染头、链接渲染库）都被拦下。
5. **行为改动**：(1) `editdoc` 判定参数是否禁用时改为“预览值优先，其次已提交值”，与模块编译一致；在预览里切到黑白配置文件后，HSL 预览立即被拒绝。(2) 颗粒效果为 0 时其余颗粒参数不可调节（参数模式 `when_zero` 规则，来源为 2026-11-05 用户给出）。CTest 没有回归。
6. **观察**：蒙版密度没有参数模式，需求没有给范围，`set_mask_density` 只拒绝负数；壳层颗粒滑杆仍未绑定 `detail/grain_*`（第五轮观察 2，未变）；显示刻度的轨道两端颜色没有来源，`colors_confirmed` 为假。

## 总结论

**打回重写。** 第五轮条件 3（P-12、P-23）关闭，第 4 关不再有否决项。条件 1 已有可用的模块接口、两个专门模块和渲染接线，但按清单原文仍未达到“每个能力独立模块”；条件 2 留了一处常数。第 1 关的 C-31、C-30、C-33 仍是否决行。

## 打回项与复审条件（本轮）

1. **Q-06 收尾**：`layer_document.cpp:711` 改查 `mask/selection_feather`，补一个解码越界的测试。
2. **C-31 续做**：其余 18 个能力按架构第 154 行各有自己的目录和模块类（或由用户裁定通用实现是否可以算独立模块，并写进上位文档）；预设与批量同步改经模块的提取与写回；给依赖检测、素材、纯色的能力实现对应的禁用原因。
3. **状态接线**：精修提交成功后调用 `mark_photo_edited`，导出成功后调用 `mark_photo_exported`；拍摄时间由解码适配器回填。
4. **账本移动语义**：移动后的对象保留一个空快照，或明确禁止移动。
5. 第五轮条件 4–7（S-25、S-10；C-30、C-33、Q-25；Q-11、Q-12、O-13–O-15；T-02、T-03、T-09）仍然有效。

条件 1、3、4 工作量小，可以和条件 2 的第一批能力一起做后再复审。
