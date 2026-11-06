# 参数模式补全 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

日期：2026-11-05
状态：已完成
分支：`codex/desktop-official-layout`，基线 `d1c0e12`

**Goal:** 让参数模式成为范围、步进、默认值、可选值、禁用原因的唯一来源，并把导出品质改为结构化、按模式校验，关闭第三轮审核的打回项 2 余项（C-05、C-31/Q-02 标识、B-32）。

**Architecture:** 在 `kernel/capability` 的 `ParameterMode` 上增加三类事实：枚举可选值、步进/默认值是否有来源、禁用规则。新增 `color.global/profile`（配置文件）枚举参数，用禁用规则表达“黑白档禁用鲜艳度、饱和度”。`EditDocument` 预览与提交按禁用规则拒绝。导出设置从自由字符串改为 `ExportSettings` 结构，规范编码后仍存入原有 `settings_snapshot` 列，SQLite 表结构不变。注册表外的能力标识并回所属能力。

**Tech Stack:** C++17、CMake/CTest。

**Spec:**
- 第三轮审核记录（打回项 2，C-05、C-31、Q-02、B-32、B-33）及后续裁决
- 契约清单 C-05、§4 标识表、§5 段末规则；边界清单 B-32、B-47
- 需求：§10.3 第 419 行（配置文件：标准、质感、黑白、基础，默认标准；黑白档禁用影调鲜艳度、饱和度并把 HSL 换成黑白组）；§19 第 861 行（品质二选一，0–14 整数）；§11.5 第 630 行、§12.5 第 557 行（眼神光位置与旋转 -100–100，默认 0）；§12.1 第 592 行（光影均匀明度 -100–100，默认 0）；§12.6 第 638 行（角度 -45–45）

## Global Constraints

- 需求或契约没写的步进、默认值，标为“待确认”：校验时只查范围、不查步进；界面无障碍名写“步进待确认”“默认待确认”。不补数字。
- 有明确步进的保持校验：曝光 0.05；色温、色调、11 项影调 1（C-36–C-39）；导出品质为整数（需求 §19）。
- 配置文件可选值的稳定标识：`standard`（标准）、`texture`（质感）、`monochrome`（黑白）、`basic`（基础），默认 `standard`。自定义 `.xmp` 配置文件不在本计划内。
- 黑白档的 HSL 切换依赖 HSL 参数模式；HSL 范围尚未实测，本计划只登记规则缺口，不实现。
- 标识只用契约 §4 已注册的能力：眼神光归入 `portrait.feature`、宠物眼神光归入 `pet`、光影均匀归入 `image.background`。`mask/feather` 是 `kernel/mask` 的蒙版属性刻度，不是能力片段，不得写入精修文档。
- 删除同义双名 `color_temperature`、`color_tint`。
- 导出：品质与文件大小限制最多给一个（二选一）；品质 0–14 整数；大小限制正整数（B-47）；格式为 `original`/`jpg`/`tiff`；未声明的键拒绝；同一键出现两次拒绝。

## Review Focus

1. 旧文档里已经保存了 `color_temperature` 等别名或旧标识的片段：装载不能失败，按未知参数原样保留。
2. 配置文件切到黑白后，已经提交的鲜艳度值不能被清掉，只是不可再改；切回标准后可以继续编辑。
3. 导出设置字符串里有空格、重复键、`quality=` 空值、`quality=3;size_limit_kb=100` 同时出现：都拒绝，且已有任务的设置不变。
4. 步进待确认的参数输入小数（如羽化 `12.5`）：按范围接受，不按“整数步进”拒绝。
5. SQLite 里已经存在的旧导出事件（例如 `copies=2`、`quality=8`）重新打开时：能读出来，不会让队列加载失败。

---

## 文件

- Modify: `kernel/capability/include/pixcake/capability/capability_registry.hpp`、`kernel/capability/src/capability_registry.cpp`
- Modify: `kernel/editdoc/src/edit_document.cpp`（预览/提交检查禁用规则）
- Create: `kernel/exportq/include/pixcake/exportq/export_settings.hpp`、`kernel/exportq/src/export_settings.cpp`；Modify: `kernel/exportq/src/export_queue.cpp`、`kernel/exportq/CMakeLists.txt`
- Modify: `ui/panels/src/slider_spec.cpp`（待确认的步进/默认值文字）
- Test: `tests/kernel/capability/test_capability_registry.cpp`、`tests/kernel/editdoc/test_edit_document.cpp`、`tests/kernel/exportq/test_export_queue.cpp`、`tests/ui/test_slider_spec.cpp`

### Task 1: 参数模式字段与标识整理

**Interfaces（Produces）:**
```cpp
struct ParameterDisableRule {
  std::string when_capability_id;   // 条件参数所在能力
  std::string when_parameter_id;    // 条件参数
  std::string when_choice;          // 条件取值（枚举标识）
  std::string reason;               // 用户可见的禁用原因
};
struct ParameterMode {               // 现有字段保留，新增：
  std::vector<std::string> choices;  // 非空即为枚举参数；min/max/step 不使用
  std::string default_choice;
  bool step_confirmed = true;
  bool default_confirmed = true;
  std::vector<ParameterDisableRule> disable_rules;
};
```
- `validate_parameter_display_value`：枚举参数只接受 `choices` 中的值；数值参数先查范围，`step_confirmed` 为真时再查步进。
- 模式表变更：
  - 新增 `color.global/profile`，可选值和默认值按全局约束；
  - `color.global` 的 `vibrance`、`saturation` 带禁用规则 `{color.global, profile, monochrome, "黑白配置文件不调节鲜艳度和饱和度"}`；
  - `color.match/amount`、`color.style/amount`、`mask/feather`、眼神光三项、光影均匀：`step_confirmed=false`；
  - `crop/angle`：`step_confirmed=false`、`default_confirmed=false`；
  - 眼神光改为 `portrait.feature/eye_glint_{vertical,horizontal,rotation}`，宠物眼神光改为 `pet/eye_glint_{vertical,horizontal,rotation}`，光影均匀改为 `image.background/light_uniform_brightness`；
  - 删除 `color_temperature`、`color_tint`。

- [x] **Step 1: 写失败测试**（`test_capability_registry.cpp`）：
  - `test_c05_profile_is_an_enum_parameter`：`profile` 接受 `monochrome`、拒绝 `黑白` 和 `1`；默认 `standard`。
  - `test_c31_parameter_modes_use_registered_capabilities`：除 `mask` 外，每个模式的 `capability_id` 都能在 `CapabilityRegistry::phase_one().find` 中找到；`find_parameter_mode("color.global","color_temperature") == nullptr`。
  - `test_b33_unconfirmed_step_checks_range_only`：`mask/feather` 接受 `12.5`、拒绝 `100.5`；`exposure` 仍拒绝 `0.03`。
- [x] **Step 2:** 运行并确认失败。
- [x] **Step 3:** 实现。
- [x] **Step 4:** 全量 CTest 通过（`test_render_description`、`gpu` 测试用到参数模式，必须一起通过）。

### Task 2: 禁用规则生效

**Interfaces:**
- Produces: `std::optional<std::string> parameter_disabled_reason(std::string_view capability_id, std::string_view parameter_id, const std::function<std::optional<std::string>(std::string_view, std::string_view)>& committed_value);`（`committed_value` 返回条件参数的已提交值；没提交时按 `default_choice` 判断）
- `EditDocument::set_preview` 与 `commit_preview`：目标参数有禁用原因时返回 `StateConflict`，不改预览、不写盘。

- [x] **Step 1: 写失败测试**：
  - `test_capability_registry::test_c05_monochrome_disables_vibrance_and_saturation`：条件值为 `monochrome` 时两项都返回原因，`contrast` 返回空；条件值为 `standard` 时都返回空。
  - `test_edit_document::test_c05_monochrome_profile_blocks_vibrance_edits`：先提交 `vibrance=20`，再提交 `profile=monochrome`；之后 `set_preview(vibrance)` 返回 `StateConflict`，已提交的 `vibrance` 仍是 `20`；改回 `standard` 后可以再改。
  - `test_edit_document::test_q02_mask_scope_is_not_a_document_fragment`：`commit_preview(...,"mask","feather",...)` 返回 `StateConflict`。
- [x] **Step 2:** 确认失败。
- [x] **Step 3:** 实现。
- [x] **Step 4:** 全量 CTest 通过。

### Task 3: 导出设置结构化

**Interfaces（Produces，`pixcake/exportq/export_settings.hpp`）:**
```cpp
enum class ExportFormat { Original, Jpg, Tiff };
struct ExportSettings {
  ExportFormat format = ExportFormat::Original;
  std::optional<int> quality;                   // 0..14，参数模式 export/quality
  std::optional<std::uint64_t> size_limit_kb;   // > 0
  bool convert_to_srgb = false;
  int print_copies = 0;                         // 0 表示不打印
};
ResultCode parse_export_settings(const std::string& text, ExportSettings& out);
std::string encode_export_settings(const ExportSettings& settings);  // 规范顺序：format;quality|size_limit_kb;srgb;copies
```
- 文本格式：以 `;` 分隔的 `key=value`；键为 `format`、`quality`、`size_limit_kb`、`srgb`（`0`/`1`）、`copies`（非负整数）；空字符串表示全部默认。
- 品质的范围来自新增参数模式 `export/quality`（0–14，步进 1，默认值待确认）。
- `ExportQueue::enqueue`：用 `parse_export_settings` 替换 `valid_quality_setting`，失败返回 `StateConflict`，并以 `encode_export_settings` 的结果存入快照。
- 旧事件兼容：装载事件时不重新校验已有的 `settings_snapshot`（只在入队时校验）。

- [x] **Step 1: 写失败测试**（`test_export_queue.cpp`）：
  - `test_b32_export_settings_parse_strictly`：接受 `""`、`"format=jpg;quality=14"`、`"format=jpg;size_limit_kb=500"`、`"copies=2"`；拒绝 `"quality=15"`、`"quality="`、`"quality=3.5"`、`"quality=3;quality=4"`、`"quality=3;size_limit_kb=100"`、`"size_limit_kb=0"`、`"format=png"`、`"unknown=1"`、`"quality = 3"`。
  - `test_b32_enqueue_rejects_duplicate_quality`：`enqueue` 带 `"quality=3;quality=99"` 返回 `StateConflict`，队列里没有这个任务。
  - `test_b32_enqueue_stores_canonical_settings`：`"quality=8;format=jpg"` 入队后快照为 `"format=jpg;quality=8"`。
  - 现有断言 `stored.settings_snapshot == "quality=8"` 和 `== "copies=2"` 改为规范编码（`"format=original;quality=8"`、`"format=original;copies=2"`）。
- [x] **Step 2:** 确认失败。
- [x] **Step 3:** 实现。
- [x] **Step 4:** 全量 CTest 通过（含 `test_exportd`、SQLite 导出日志相关测试）。

### Task 4: 界面文字、证据与提交

- [x] **Step 1: 写失败测试**（`test_slider_spec.cpp`）：`test_b33_unconfirmed_step_and_default_are_labelled`：羽化的无障碍名为 `羽化，范围 0 到 100，步进待确认，默认 0`；角度为 `角度滑杆，范围 -45 到 45，步进待确认，默认待确认`；曝光不变。
- [x] **Step 2:** 实现 `slider_spec` 的文字分支，全量 CTest 通过。
- [x] **Step 3:** 写证据 `docs/superpowers/evidence/2026-11-05-parameter-mode-completion.md`：改动、测试、新的稳定标识、仍未关闭的项（HSL 黑白组切换、自定义 `.xmp`、水印与排版设置结构化、原 Free 分组范围实测）。在架构文档 §7 标识表下补一句眼神光、光影均匀、配置文件的参数归属。
- [x] **Step 4:** 勾选、`git diff --check`、提交 `feat(kernel): complete parameter modes and structure export settings`。
