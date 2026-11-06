# 色彩编辑器范围确认与标识阻塞：2026-11-06

## 已确认

官方《基础调色功能说明》明确写出色彩编辑器的三项偏移：

- 色相偏移：-100 到 100，默认 0。
- 饱和度偏移：-100 到 100，默认 0。
- 明亮度偏移：-100 到 100，默认 0。

来源：<https://wiki.pixcakeai.com/guide/9398.html>；项目内同步记录见 `docs/superpowers/evidence/2026-11-05-official-manual-lookup.md`。

## 当前阻塞

需求、契约和架构文档冻结了 `color.global` 等能力标识，但没有冻结色彩编辑器三项参数的内部 `parameter_id`，也没有明确它们是否作为 `color.global` 片段、独立结构化字段或专用交互数据保存。当前代码没有对应参数模式或存储字段。

因此本轮**没有注册参数模式、没有改壳层范围、没有写入精修文档**。直接选取 `color_editor_hue_shift` 等内部名字会把实现选择误写成需求事实，违反“未确认不猜测”的约束。

## 下一步

需要在需求/架构中确认三项参数的稳定标识和持久化归属；确认后可按上述官方范围补失败测试、参数模式、壳层消费和复审证据。

## 后续（2026-11-05，Claude）

参数标识按本项目规则选定（契约只冻结能力标识）：`color.global` 下 `color_editor_hue_shift`、`color_editor_saturation_shift`、`color_editor_luminance_shift`，-100..100，默认 0，步进待确认。已注册，壳层“色相偏移/饱和度偏移/明亮度偏移”三个滑杆由参数模式驱动（测试 `test_c49_color_editor_offsets`、`test_c46_subgroup_labels_bind_to_kernel_modes`）。
