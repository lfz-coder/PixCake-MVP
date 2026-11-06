# 精修滑杆改由参数模式生成 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

日期：2026-11-05
状态：已完成
分支：`codex/desktop-official-layout`，基线 `ecf49dc`

**Goal:** 壳层精修色彩面板里每一支滑杆的范围、步进、默认值和读数只来自内核参数模式；没有参数模式的滑杆不显示任何范围数字，关闭第三轮审核的 C-38、Q-06、Q-20 否决项。

**Architecture:** 新建无 Qt 的 `ui/panels` 库（架构 §5：“由能力参数模式生成的面板”）。它提供两件事：(1) 精修色彩面板顶层滑杆的“显示名 → 能力标识/参数标识”绑定表；(2) 把绑定解析成 `SliderSpec`（整数刻度数、默认刻度、小数位、读数文字、无障碍名）。解析时从 `kernel/capability` 的 `find_parameter_mode` 读范围。`apps/shell` 只消费 `SliderSpec`，源码里不再出现任何滑杆上下限。

**Tech Stack:** C++17、Qt 6 Widgets（仅 `apps/shell`）、CMake/CTest。

**Spec:**
- `docs/superpowers/specs/review/2026-11-05-pixcake-seven-gate-review-r3.md`（第 1 关 C-38，第 5 关 Q-06/Q-20，后续裁决）
- 契约清单 §4 段末规则（“参数模式里的显示刻度是界面画滑杆的唯一来源。控件源码再写一份上下限，否决”）、§5 C-36–C-39 与段末规则
- 架构 §5 第 6 条、`ui/panels` 目录说明；架构附录 B 测量规则

## Global Constraints

- 滑杆范围、步进、默认值的唯一来源是 `pixcake::find_parameter_mode`；`apps/shell` 不得出现滑杆上下限常数。
- 没有参数模式的滑杆：禁用，刻度范围 0..0，读数显示 `—`，无障碍名 `<显示名>，范围待确认`。不得按相邻分组或“市面惯例”补范围。
- 曝光：-5.00 到 5.00，步进 0.05，默认 0.00（C-38）；色温、色调：-100 到 100，步进 1，默认 0（C-36、C-37）；其余 11 项影调：-100 到 100，步进 1，默认 0（C-39）。这些数值只在内核参数模式里出现。
- 绑定只用于色彩面板顶层的白平衡与影调 14 项；曲线、色彩编辑器等子分组里同名的“阴影”“高光”不得绑定到影调参数。
- 滤镜下方的“程度”不是追色/色彩风格程度，没有来源，保持未绑定。
- `ui/` 不得包含 `gpu/backend`（rule 8）；只有 `pixcake_app_shell` 链接 Qt（rule 9）。
- 测试名带清单编号。

## Review Focus

1. 子分组里与影调同名的滑杆（曲线的“阴影”“高光”）被错误绑定到影调参数。
2. 步进为小数（0.05）时整数刻度换算出错：刻度数应为 200、默认刻度 100，读数为 `0.00` 而不是 `0` 或 `-0.00`。
3. 绑定指向不存在的参数（拼写错误或内核删掉参数）时，应退化为未绑定，不能崩溃或显示 0..0 的假范围数字。
4. 原来写死范围的色阶（黑场、灰场、白场、输出黑白场）和创意色彩“程度”不再出现任何数字范围。
5. 精修页 smoke 检查必须能发现以后有人再把范围写回控件源码。

---

## 文件

- Create: `ui/panels/CMakeLists.txt`、`ui/panels/include/pixcake/ui/panels/slider_spec.hpp`、`ui/panels/src/slider_spec.cpp`
- Modify: 顶层 `CMakeLists.txt`（`add_subdirectory(ui/panels)`）、`apps/shell/CMakeLists.txt`（链接 `pixcake_ui_panels`）、`apps/shell/main.cpp`（精修色彩面板）
- Test: `tests/ui/test_slider_spec.cpp`、`tests/CMakeLists.txt`；`apps/shell/main.cpp` 的精修 `--smoke` 检查

### Task 1: `ui/panels` 滑杆规格

**Interfaces（Produces）:**
```cpp
namespace pixcake::ui::panels {
struct SliderBinding { std::string capability_id; std::string parameter_id; };
struct SliderSpec {
  std::string label;
  bool bound = false;          // 绑定解析到了内核参数模式
  double minimum = 0.0, maximum = 0.0, step = 0.0, default_value = 0.0;
  int decimals = 0;            // 由步进推出：0.05→2，1→0，0.01→2
  int tick_count = 0;          // round((maximum-minimum)/step)；未绑定为 0
  int default_tick = 0;        // round((default_value-minimum)/step)；未绑定为 0
  std::string default_text;    // 按 decimals 格式化的默认值；未绑定为 "—"
  std::string accessible_name; // "<label>，范围 <min> 到 <max>，步进 <step>，默认 <default>" 或 "<label>，范围待确认"
};
// 精修色彩面板顶层（白平衡、影调）显示名到参数的绑定；其他显示名返回 nullopt。
std::optional<SliderBinding> refine_color_binding(std::string_view label);
SliderSpec slider_spec(std::string label, const std::optional<SliderBinding>& binding);
}
```
绑定表（显示名 → `color.global/<parameter>`）：色温→`temperature`、色调→`tint`、曝光→`exposure`、对比度→`contrast`、亮度→`brightness`、高光→`highlights`、阴影→`shadows`、白色→`whites`、黑色→`blacks`、纹理→`texture`、清晰度→`clarity`、祛雾→`dehaze`、鲜艳度→`vibrance`、饱和度→`saturation`。数字格式化不得产生 `-0`。

- [x] **Step 1: 写失败测试** `tests/ui/test_slider_spec.cpp`：
  - `test_c38_exposure_spec_comes_from_parameter_mode`：`slider_spec("曝光", refine_color_binding("曝光"))` → `bound`、`minimum == -5`、`maximum == 5`、`step == 0.05`、`decimals == 2`、`tick_count == 200`、`default_tick == 100`、`default_text == "0.00"`、`accessible_name == "曝光，范围 -5.00 到 5.00，步进 0.05，默认 0.00"`。
  - `test_c36_temperature_spec`：色温 → `tick_count == 200`、`default_tick == 100`、`accessible_name == "色温，范围 -100 到 100，步进 1，默认 0"`。
  - `test_c39_all_top_level_labels_match_kernel_modes`：14 个显示名都 `bound`，且每项的 min/max/step/default 与 `find_parameter_mode("color.global", id)` 相同。
  - `test_c35_unbound_label_has_no_range`：`refine_color_binding("程度")`、`("黑场")`、`("色相-红色")` 都是 `nullopt`；`slider_spec("黑场", std::nullopt)` → `!bound`、`tick_count == 0`、`default_text == "—"`、`accessible_name == "黑场，范围待确认"`。
  - `test_c35_missing_kernel_parameter_is_unbound`：`slider_spec("X", SliderBinding{"color.global", "no_such"})` → `!bound`、`accessible_name == "X，范围待确认"`。
- [x] **Step 2:** 构建 `test_slider_spec`，失败（头文件不存在）。
- [x] **Step 3:** 建库 `pixcake_ui_panels`（链接 `pixcake_capability`、`pixcake_warnings`），实现两个函数。
- [x] **Step 4:** `ctest -R test_slider_spec` 通过；`arch_dependencies` 通过。

### Task 2: 壳层消费 `SliderSpec`

**Interfaces（Consumes）:** Task 1 的 `refine_color_binding`、`slider_spec`。

- [x] **Step 1: 写失败的 smoke 检查**（`main.cpp` 精修页 `--smoke` 分支，返回码 4）：
  - 在 `refine-color-labels` 下找无障碍名以 `曝光，` 开头的 `QSlider`：必须存在、禁用、`minimum() == 0`、`maximum() == 200`、`value() == 100`，无障碍名等于 `slider_spec("曝光", refine_color_binding("曝光")).accessible_name`。
  - 该面板下所有 `QSlider` 的无障碍名，要么等于对应 `slider_spec` 的结果，要么以 `，范围待确认` 结尾；任何无障碍名包含 `-100 到 100` 但对应显示名不在绑定表里，判失败。
  - 运行 `ctest -R test_shell_app_smoke_refine`，失败（当前曝光刻度为 -100..100）。
- [x] **Step 2:** 改 `add_disabled_slider` 为只接收 `const SliderSpec&` 与对象名；顶层白平衡/影调行用 `slider_spec(label, refine_color_binding(label))`；所有子分组行（色阶、曲线、HSL、色彩编辑器、颜色分级、可选颜色、细节、颗粒、镜头调整、透视矫正、校准、创意色彩）用 `slider_spec(label, std::nullopt)`。删除 `add_centered` 中的范围文字和所有数字范围参数。
- [x] **Step 3:** `grep -nE '范围 -?[0-9]|setRange\((-?[0-9]+), *(-?[0-9]+)\)' apps/shell/main.cpp` 只剩图库网格 `setRange(1, 7)`（需求第 6 节写明 1–7，不属于参数模式滑杆）。
- [x] **Step 4:** 全量构建与 CTest 通过。

### Task 3: 可见验证、证据与提交

- [x] **Step 1:** 用 Jarvis 打开本地壳层精修页，读无障碍树：曝光滑杆的无障碍名、色阶“黑场”为“范围待确认”。Jarvis 不可用（锁屏、权限）时记录原因，不写成通过。
- [x] **Step 2:** 写证据 `docs/superpowers/evidence/2026-11-05-refine-sliders-from-parameter-mode.md`：改动、测试名、RED 证据、Jarvis 结果、仍未关闭的项（原 Free 分组与色阶的参数模式需要实测后由内核注册；黑白档规则属于第 3 项）。
- [x] **Step 3:** 勾选本计划、状态改为已完成；`git diff --check`；提交 `feat(ui): derive refine slider ranges from kernel parameter modes`。
