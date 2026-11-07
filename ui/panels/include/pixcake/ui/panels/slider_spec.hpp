#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
// 色阶、颜色分级、HSL、黑白混合、可选颜色、颗粒的子分组滑杆绑定；顶层影调和同名的子分组滑杆互不串。
std::optional<SliderBinding> refine_subgroup_binding(std::string_view label);
std::optional<SliderBinding> refine_local_binding(std::string_view label);

// 裁剪面板角度滑杆的参数（crop/angle）。
SliderBinding refine_crop_angle_binding();

// 精修色彩面板的行（显示顺序、显示名、绑定都来自同一张表，代码质量清单 Q-11）。
enum class PanelRowKind { Title, Slider, Choice };
struct PanelRow {
  PanelRowKind kind = PanelRowKind::Title;
  std::string label;
  std::optional<SliderBinding> binding;  // 滑杆和选择有；没有参数模式时为空
  std::string object_name;               // 冒烟检查要找的几支滑杆
};
std::vector<PanelRow> refine_color_rows();
// 枚举参数（配置文件、可选颜色调整方式）的可选值：稳定标识、显示名、是否默认。顺序与参数模式一致。
struct ChoiceOption {
  std::string id;
  std::string label;
  bool is_default = false;
};
std::vector<ChoiceOption> choice_options(const SliderBinding& binding);
// 后期调色面板：两项全图/局部，加色彩面板的分组标题。
std::vector<std::string> refine_post_color_titles();
const char* refine_color_panel_name();
const char* refine_local_feather_label();
// 无障碍名“<显示名>，……”里的显示名；范围待确认的滑杆。
std::string slider_label_of(const std::string& accessible_name);
bool slider_range_pending(const std::string& accessible_name);

// 按绑定从内核参数模式解析滑杆规格。绑定为空或参数模式里找不到时返回未绑定的规格。
SliderSpec slider_spec(std::string label, const std::optional<SliderBinding>& binding);

}  // namespace pixcake::ui::panels
