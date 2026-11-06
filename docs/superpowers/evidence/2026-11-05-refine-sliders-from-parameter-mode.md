# 精修滑杆改由参数模式生成验收记录：2026-11-05

计划：`docs/superpowers/plans/2026-11-05-refine-sliders-from-parameter-mode.md`
对应：第三轮审核 C-38、Q-06、Q-20 否决项（壳层控件源码写死滑杆范围）

## 改动

- 新增无 Qt 的 `ui/panels`（`pixcake_ui_panels`，链接 `pixcake_capability`），按架构 §5 的目录约定放置“由能力参数模式生成的面板”。
  - `refine_color_binding(label)`：色彩面板顶层白平衡与影调 14 个显示名到 `color.global/<参数>` 的绑定。
  - `refine_local_binding("羽化")` → `mask/feather`；`refine_crop_angle_binding()` → `crop/angle`。
  - `slider_spec(label, binding)`：从 `find_parameter_mode` 读取最小值、最大值、步进和默认值，推出小数位、整数刻度数、默认刻度、读数文字和无障碍名。找不到参数模式时返回未绑定的规格：刻度 0..0，读数 `—`，无障碍名 `<显示名>，范围待确认`。
- `apps/shell/main.cpp`：所有精修滑杆都由 `SliderSpec` 构造。顶层 14 项绑定内核参数；色阶、曲线、HSL、色彩编辑器、颜色分级、可选颜色、细节、颗粒、镜头调整、透视矫正、校准、创意色彩“程度”和滤镜“程度”目前没有参数模式，显示“范围待确认”。局部调色“羽化”、裁剪“角度滑杆”改为取自内核参数模式。源码中只剩图库网格的 `setRange(1, 7)`（需求第 6 节）。
- 精修页 `--smoke` 新增 `check_refine_sliders`：色彩面板里每支滑杆要么与 `slider_spec` 的结果完全一致（包括刻度数和默认刻度），要么是“范围待确认”且刻度为 0；绑定的滑杆必须正好 14 支，其中包含曝光。

## 测试

| 测试 | 断言 |
| --- | --- |
| `test_slider_spec::test_c38_exposure_spec_comes_from_parameter_mode` | 曝光：-5..5，步进 0.05，2 位小数，200 个刻度，默认刻度 100，读数 `0.00`，无障碍名 `曝光，范围 -5.00 到 5.00，步进 0.05，默认 0.00` |
| `test_slider_spec::test_c36_temperature_spec` | 色温：200 个刻度，默认刻度 100，无障碍名 `色温，范围 -100 到 100，步进 1，默认 0` |
| `test_slider_spec::test_c39_all_top_level_labels_match_kernel_modes` | 14 个显示名与内核参数模式逐项一致 |
| `test_slider_spec::test_c35_unbound_label_has_no_range` | 程度、黑场、色相-红色都未绑定，没有范围数字 |
| `test_slider_spec::test_c35_missing_kernel_parameter_is_unbound` | 绑定指向不存在的参数时退化为未绑定 |
| `test_slider_spec::test_c35_local_feather_and_crop_angle_come_from_parameter_modes` | 羽化 0..100 默认 0；裁剪角度 -45..45 |
| `test_shell_app_smoke_refine`（`check_refine_sliders`） | 见上 |

RED 证据：`test_slider_spec` 先因缺少头文件编译失败；精修页 smoke 在旧代码上失败，返回码 4，输出 `refine slider not from parameter mode: 程度，范围 -100 到 100，步进 1，默认 0`。

验证：`cmake --build` 成功；`ctest -j8 --output-on-failure` **52/52 通过**（新增 `test_slider_spec`）；`git diff --check` 通过。

## Jarvis 无障碍复核

执行机 0.1.586，辅助功能和屏幕录制权限可用；本会话没有连接 `jarvis-knowledge`，只读取了无障碍树。启动的 bundle 是 scratchpad 里的副本 `pixcake-refine.app`：可执行文件改名为 `pixcake_app_shell-refine`，让壳层默认打开精修页，并已自签名。

| 控件 | 读到的无障碍名 | 值 | 结果 |
| --- | --- | --- | --- |
| 曝光滑杆 | `曝光，范围 -5.00 到 5.00，步进 0.05，默认 0.00` | 100 | 符合 |
| 色温滑杆 | `色温，范围 -100 到 100，步进 1，默认 0` | 100 | 符合 |
| 色阶·黑场 | `黑场，范围待确认` | 0 | 符合 |
| 色阶·输出黑场 | `输出黑场，范围待确认` | 0 | 符合 |
| 滤镜·程度 | `程度，范围待确认` | 0 | 符合 |
| 细节·AI降噪-程度 | `AI降噪-程度，范围待确认` | 0 | 符合 |
| 局部调色·羽化、裁剪·角度滑杆 | 按名称查询 0 命中 | — | 无障碍树读不到，以单元测试为证；已写入 Jarvis `pending/` 待上报 |

## 仍未关闭

- 原 Free 分组、色阶和创意色彩还没有内核参数模式。它们的范围要先有需求原文或 test 版实测读数，再由内核注册；注册后壳层会自动显示，不需要再改壳层源码。
- 内核 `crop/angle` 的步进 1、默认 0 没有引用来源，需要在第 3 项复核参数模式时确认。
- 黑白档规则、禁用原因字段属于第 3 项。
- 视觉变化：未绑定的滑杆现在刻度为 0..0，圆点停在左端，不再居中。这是不猜范围的直接结果；拿到实测范围后会恢复正确位置。
