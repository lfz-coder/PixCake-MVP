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

// 精修色彩面板的行（显示顺序、显示名、绑定都来自同一张表，代码质量清单 Q-11）。Section 开始一个可折叠分组，
// 其后的行属于它，直到下一个 Section；HeaderAction 画在分组标题栏右侧；Picker 是白平衡的“预设 + 吸管”一行。
//
// 人像美化等面板还用到这些（PixCake 9.3.0 截图里的控件种类）：Subheading 组内小标题，Separator 组内分隔线，
// Toggle 开关，Checkbox 复选框，PairSlider 左右两支联动滑杆，Cards 图片卡片单选（options 是各卡片名），
// Swatches 色块单选（options 是 #rrggbb，第一个空串表示“无”），AiButton AI 大按钮，Tabs 页签（options），
// Group 一个带标题的框，里面是后面的子项（child），Tool 名字加右侧的小图标按钮（options 是各按钮的说明，
// 至少一个），Dropdown 名字在上、下拉框在下（options 是可选值，第一个是当前值），Segments 占满一行的两段切换
// （推荐/我的），Notice 一块提示（如“未检测到天空”），IconRow 一行图标按钮（options 是各按钮的说明）。
enum class PanelRowKind {
  Title, Section, HeaderAction, Picker, Slider, Choice,
  Subheading, Separator, Toggle, Checkbox, PairSlider, Cards, Swatches, AiButton, Tabs, Group, Tool,
  Dropdown, Segments, Notice, IconRow
};
struct PanelRow {
  PanelRowKind kind = PanelRowKind::Title;
  std::string label;
  std::optional<SliderBinding> binding;  // 滑杆和选择有；没有参数模式时为空
  std::string object_name;               // 冒烟检查要找的几支滑杆
  // 官方画面上的修饰：角标（New、Beta）、名字后的问号、Section 标题栏的“还原本组”。
  std::string badge;
  bool help = false;
  bool reset = false;
  // 滑杆：数值右边有 ▸（带子项，子项是后面 child 为真的行）；子项；双向（默认滑块在中间）；档位数（> 0 是档位滑杆）。
  bool expandable = false;
  bool child = false;
  bool centered = false;
  bool at_maximum = false;  // 截图里滑块在最右端（默认是最大值，如不透明度 100）
  int ticks = 0;
  std::string track;  // 色带：temperature、tint
  std::vector<std::string> options;
};
std::vector<PanelRow> refine_color_rows();
// 枚举参数（配置文件、可选颜色调整方式）的可选值：稳定标识、显示名、是否默认。顺序与参数模式一致。
struct ChoiceOption {
  std::string id;
  std::string label;
  bool is_default = false;
};
std::vector<ChoiceOption> choice_options(const SliderBinding& binding);
// 后期调色“全图调整”下的行：白平衡、影调两组标题和十四支滑杆（绑定 color.post）。滑杆来自色彩面板的同一张表，
// 只取 color.post 已登记参数模式的那十四支；其余分组（色阶等）后期调色没有登记，不出现。
std::vector<PanelRow> refine_post_color_rows();
// 局部调色面板的行：作用部位下拉（color.local/region），再是白平衡、影调两组标题和十四支滑杆（绑定 color.local）。
std::vector<PanelRow> refine_local_rows();
// 色彩面板的分组复用到别的能力（局部调色、后期调色和色彩调节是同一套分组，只是绑定不同）：从色彩面板的表里取
// sections 列出的分组（按表里的顺序），滑杆和选择改绑到 capability 的同名参数；该能力没有登记的参数变成未绑定
// （范围待确认，选择行不带）。分组标题栏的按钮、白平衡预设行、黑白混合（只属于全图的黑白配置文件）不带。
// 绑定了的滑杆对象名是 <object_prefix><参数>。
std::vector<PanelRow> color_section_rows(std::string_view capability, const std::vector<std::string>& sections,
                                         std::string_view object_prefix);
// 局部调色的分组（PixCake 9.3.0 局部调色页，2026-11-08 截图）：白平衡、影调、色阶、曲线、HSL、色彩编辑器、颜色分级、
// 可选颜色、细节、创意色彩。
std::vector<std::string> refine_local_sections();
// 后期调色的分组：color.post 登记了参数的白平衡、影调。
std::vector<std::string> refine_post_sections();
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
