#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace pixcake::ui::panels {

// 面板里一支滑杆对应的能力参数。
struct SliderBinding {
  std::string capability_id;
  std::string parameter_id;
};

// 画一支滑杆需要的全部数字。范围、步进和默认值只来自内核参数模式；
// 没有参数模式时 bound 为假，不带任何范围数字。
struct SliderSpec {
  std::string label;
  bool bound = false;
  double minimum = 0.0;
  double maximum = 0.0;
  double step = 0.0;
  double default_value = 0.0;
  int decimals = 0;             // 由步进推出：0.05 → 2，1 → 0
  int tick_count = 0;           // (maximum - minimum) / step；未绑定为 0
  int default_tick = 0;         // (default_value - minimum) / step；未绑定为 0
  std::string default_text;     // 按 decimals 格式化的默认值；未绑定为 "—"
  std::string accessible_name;  // "<显示名>，范围 <最小> 到 <最大>，步进 <步进>，默认 <默认>" 或 "<显示名>，范围待确认"
};

// 精修色彩面板顶层（白平衡、影调）的显示名到参数的绑定。子分组里的同名滑杆不在此表。
std::optional<SliderBinding> refine_color_binding(std::string_view label);

// 精修局部调色面板的显示名到参数的绑定（羽化 → mask/feather）；其他显示名返回 nullopt。
std::optional<SliderBinding> refine_local_binding(std::string_view label);

// 裁剪面板角度滑杆的参数（crop/angle）。
SliderBinding refine_crop_angle_binding();

// 按绑定从内核参数模式解析滑杆规格。绑定为空或参数模式里找不到时返回未绑定的规格。
SliderSpec slider_spec(std::string label, const std::optional<SliderBinding>& binding);

}  // namespace pixcake::ui::panels
