# 参数模式补全验收记录：2026-11-05

计划：`docs/superpowers/plans/2026-11-05-parameter-mode-completion.md`
对应：第三轮审核打回项 2 的余项（C-05、C-31/Q-02 标识、B-32、B-33）

## 改动

- `kernel/capability`：`ParameterMode` 新增以下字段：
  - `choices` / `default_choice`：枚举参数的可选值和默认值；
  - `step_confirmed` / `default_confirmed`：步进、默认值是否有需求或契约来源；
  - `disable_rules`：在什么条件下禁用这个参数，以及禁用原因。

  新增 `parameter_disabled_reason`。参数校验改为：
  - 枚举参数只接受可选值；
  - 步进有来源的参数，同时检查范围和步进；
  - 步进待确认的参数，只检查范围。
- 参数模式表的调整：
  - 新增 `color.global/profile`：标准、质感、黑白、基础，默认标准（需求第 10.3 节）。
  - `color.global` 的鲜艳度、饱和度加上黑白档禁用规则，原因文字为“黑白配置文件不调节鲜艳度和饱和度”。
  - 眼神光并入 `portrait.feature`，宠物眼神光并入 `pet`，光影均匀并入 `image.background`；删除同义双名 `color_temperature`、`color_tint`。
  - 以下参数的步进标为待确认：追色/色彩风格程度、蒙版羽化、眼神光、光影均匀、裁剪角度。裁剪角度的默认值也标为待确认。
  - 新增 `export/quality`：0–14 的整数，默认值待确认。
- `kernel/editdoc`：
  - 被禁用的参数，`set_preview` 和 `commit_preview` 都返回 `StateConflict`；已经提交的值保留不动。
  - `commit_preview` 只接受注册表里的能力，`mask` 和 `export` 不能写成文档片段。
- `kernel/render`：枚举参数编译为“稳定标识 + 可选值序号”，例如 `monochrome` → 2。没有这项改动的话，黑白档会把整个全图色彩操作判成“参数值不符合参数模式”。
- `kernel/exportq`：
  - 新增 `ExportSettings`，包含 `parse_export_settings` 和 `encode_export_settings`。
  - 入队时严格解析：键只能是 `format`、`quality`、`size_limit_kb`、`srgb`、`copies`，重复键、空值、越界值、品质和大小限制同时给出都会被拒绝；通过后以规范编码存入快照。
  - SQLite 表结构不变。已有的旧事件装载时不重新校验。
- `ui/panels`：参数的步进或默认值待确认时，无障碍名写“步进待确认”“默认待确认”。
- 架构文档能力表后补充了上述参数的归属。

## 测试（RED → GREEN）

| 测试 | RED 时的表现 |
| --- | --- |
| `test_capability_registry::test_c05_profile_is_an_enum_parameter`、`test_c31_parameter_modes_use_registered_capabilities`、`test_b33_unconfirmed_step_checks_range_only` | 编译失败，字段不存在 |
| `test_capability_registry::test_c05_monochrome_disables_vibrance_and_saturation` | 编译失败，函数不存在 |
| `test_render_description::test_c05_profile_choice_is_rendered_as_choice_index` | 操作被禁用，`values` 为空 |
| `test_edit_document::test_c05_monochrome_profile_blocks_vibrance_edits` | 黑白档下鲜艳度仍可预览和提交 |
| `test_edit_document::test_q02_mask_scope_is_not_a_document_fragment` | `mask` 片段被写进文档 |
| `test_export_queue::test_b32_export_settings_parse_strictly`、`test_b32_enqueue_rejects_duplicate_quality`、`test_b32_enqueue_stores_canonical_settings` | 编译失败，头文件不存在 |
| `test_slider_spec::test_b33_unconfirmed_step_and_default_are_labelled` | 无障碍名仍写“步进 1”“默认 0” |

验证：全量构建成功；`ctest -j8 --output-on-failure` **52/52 通过**；`git diff --check` 通过。

已有测试按新合同更新的断言：
- 导出快照改为规范编码（`format=original;quality=8`、`format=original;copies=2`）；
- 眼神光改用新标识；
- 羽化的无障碍名改为“步进待确认”。

## 行为变化

- 设置字符串只要格式不规范（例如带空格），旧代码能入队，现在会被拒绝。
- 精修文档只能写入注册表里的能力片段。旧文档里已有的其他片段仍按未知片段原样保留。

## 仍未关闭

- 黑白档“把 HSL 换成黑白组”：HSL 还没有参数模式（范围待实测），规则只能先登记。
- 自定义 `.xmp` 配置文件；水印、排版、色彩空间之外的导出设置还没有结构化。
- 原 Free 分组、色阶、创意色彩的范围实测（按用户安排放在后续）。
- 禁用原因还没有显示在壳层上（壳层滑杆目前全部只读）。
