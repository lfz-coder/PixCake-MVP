#include "pixcake/ui/panels/slider_spec.hpp"

#include "pixcake/capability/capability_registry.hpp"
#include "pixcake/mask/semantic_parts.hpp"
#include "pixcake/ui/shell/shared_words.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

namespace pixcake::ui::panels {
namespace {

constexpr char kGlobalColor[] = "color.global";
constexpr char kColorAdjust[] = "色彩调节";
constexpr char kLocalFeather[] = "羽化";
constexpr char kLocalRegion[] = "作用部位";
constexpr char kRegionAll[] = "整个画面";
constexpr char kPendingSuffix[] = "，范围待确认";
constexpr char kNameSeparator[] = "，";

int decimals_for(double step) {
  for (int places = 0; places <= 4; ++places) {
    const double scaled = step * std::pow(10.0, places);
    if (std::abs(scaled - std::round(scaled)) < 1e-9) return places;
  }
  return 4;
}

std::string format_value(double value, int decimals) {
  const double scale = std::pow(10.0, decimals);
  double rounded = std::round(value * scale) / scale;
  if (rounded == 0.0) rounded = 0.0;  // 不输出 -0
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, rounded);
  return buffer;
}

enum class Def { Title, Section, TopSlider, SubSlider, HslGrid, BwGrid, SelectiveGrid, Choice };

struct RowDef {
  Def def;
  std::string_view label;
  std::string_view parameter = {};
  std::string_view object_name = {};
  std::string_view capability = kGlobalColor;
};

struct LabelBinding {
  std::string_view label;
  std::string_view parameter_id;
};

// 精修色彩面板的唯一来源（代码质量清单 Q-11）：显示顺序、显示名和参数绑定都在这张表里。
// 顺序来自 PixCake 9.3.0 的 Jarvis 页面图。Section 是分组标题，后期调色面板复用它们。
// 没有参数的滑杆还没有参数模式，只显示“范围待确认”。颜色分级的“平衡”只用滑杆读到过范围，没有登记。
constexpr RowDef kColorPanel[] = {
    {Def::Title, "开启 AI 色彩风格"}, {Def::Choice, "配置文件", "profile"}, {Def::Title, kColorAdjust}, {Def::Title, ui::shell::kWordPreset},
    {Def::Section, "滤镜"}, {Def::Title, "推荐"}, {Def::Title, "我的"}, {Def::Title, "AI 校准"},
    {Def::TopSlider, "程度"},
    {Def::Section, "白平衡"}, {Def::Title, "AI校准白平衡"}, {Def::Title, "白平衡选择器"},
    {Def::TopSlider, "色温", "temperature"}, {Def::TopSlider, "色调", "tint"},
    {Def::Section, "影调"}, {Def::Title, "AI校准曝光"},
    {Def::TopSlider, "曝光", "exposure"}, {Def::TopSlider, "对比度", "contrast"},
    {Def::TopSlider, "亮度", "brightness"}, {Def::TopSlider, "高光", "highlights"},
    {Def::TopSlider, "阴影", "shadows"}, {Def::TopSlider, "白色", "whites"}, {Def::TopSlider, "黑色", "blacks"},
    {Def::TopSlider, "纹理", "texture"}, {Def::TopSlider, "清晰度", "clarity"}, {Def::TopSlider, "祛雾", "dehaze"},
    {Def::TopSlider, "鲜艳度", "vibrance"}, {Def::TopSlider, "饱和度", "saturation"},
    {Def::Section, "色阶"},
    {Def::SubSlider, "黑场", "levels_rgb_input_black"},
    {Def::SubSlider, "灰场", "levels_rgb_input_gray", "refine-levels-gray"},
    {Def::SubSlider, "白场", "levels_rgb_input_white"},
    {Def::SubSlider, "输出黑场", "levels_rgb_output_black"},
    {Def::SubSlider, "输出白场", "levels_rgb_output_white"},
    {Def::Section, "曲线"},
    {Def::SubSlider, "中间调域值"}, {Def::SubSlider, "亮调"}, {Def::SubSlider, "亮调域值"}, {Def::SubSlider, "暗调"},
    {Def::SubSlider, "暗调域值"}, {Def::SubSlider, "阴影"}, {Def::SubSlider, "高光"},
    {Def::Title, "RGB通道曲线"}, {Def::Title, "亮度通道曲线"}, {Def::Title, "红色通道曲线"},
    {Def::Title, "绿色通道曲线"}, {Def::Title, "蓝色通道曲线"},
    {Def::Section, "HSL"}, {Def::HslGrid, ""},
    // 黑白配置文件下 HSL 换成“黑白混合”八色（2026-11-06 实测）；两组都画，可用与否由参数模式的禁用规则决定。
    {Def::BwGrid, ""},
    {Def::Section, "色彩编辑器"},
    {Def::SubSlider, "色相偏移", "color_editor_hue_shift"},
    {Def::SubSlider, "饱和度偏移", "color_editor_saturation_shift"},
    {Def::SubSlider, "明亮度偏移", "color_editor_luminance_shift"},
    {Def::Title, "色彩平滑"}, {Def::Title, "亮度平滑"}, {Def::Title, "显示所选色彩范围"}, {Def::Title, "反转色彩范围"},
    {Def::Section, "颜色分级"},
    {Def::SubSlider, "中间调亮度", "grading_midtones_luminance"},
    {Def::SubSlider, "中间调色相", "grading_midtones_hue"},
    {Def::SubSlider, "中间调饱和度", "grading_midtones_saturation"},
    {Def::SubSlider, "全局亮度", "grading_global_luminance"},
    {Def::SubSlider, "全局调色相", "grading_global_hue"},
    {Def::SubSlider, "全局调饱和度", "grading_global_saturation"},
    {Def::SubSlider, "平衡"},
    {Def::SubSlider, "混合", "grading_blending"},
    {Def::SubSlider, "阴影色相", "grading_shadows_hue"},
    {Def::SubSlider, "阴影饱和度", "grading_shadows_saturation"},
    {Def::SubSlider, "高光亮度", "grading_highlights_luminance"},
    {Def::SubSlider, "高光色相", "grading_highlights_hue"},
    {Def::SubSlider, "高光饱和度", "grading_highlights_saturation"},
    {Def::SubSlider, "阴影亮度", "grading_shadows_luminance"},
    {Def::Section, "可选颜色"}, {Def::SelectiveGrid, ""}, {Def::Choice, "调整方式", "selective_mode"},
    {Def::Section, "细节"},
    {Def::SubSlider, "减少杂色"}, {Def::SubSlider, "减少杂色-平滑度"}, {Def::SubSlider, "减少杂色-细节"},
    {Def::SubSlider, "噪点消除"}, {Def::SubSlider, "噪点消除-对比度"}, {Def::SubSlider, "噪点消除-细节"},
    {Def::SubSlider, "锐化"}, {Def::SubSlider, "锐化-半径"}, {Def::SubSlider, "锐化-细节"},
    {Def::SubSlider, "AI降噪-程度"},
    // 颗粒属于 detail（契约 §4）。页面图的显示名与 2026-11-05 画面读数一一按顺序对应：颗粒效果、阴影、
    // 中间调、高光、颗粒大小、粗糙度、颗粒色彩。
    {Def::Section, "颗粒"},
    {Def::SubSlider, "总滑杆", "grain_amount", {}, "detail"},
    {Def::SubSlider, "阴影颗粒度透明度", "grain_shadows", {}, "detail"},
    {Def::SubSlider, "中间调颗粒度透明度", "grain_midtones", {}, "detail"},
    {Def::SubSlider, "高光颗粒度透明度", "grain_highlights", {}, "detail"},
    {Def::SubSlider, "单个颗粒度大小", "grain_size", {}, "detail"},
    {Def::SubSlider, "颗粒边缘锐度", "grain_roughness", {}, "detail"},
    {Def::SubSlider, "颗粒颜色", "grain_color", {}, "detail"},
    {Def::Title, "颗粒风格"},
    {Def::Section, "镜头调整"},
    {Def::SubSlider, "手动-中点"}, {Def::SubSlider, "手动-扭曲度"}, {Def::SubSlider, "手动-晕影"},
    {Def::SubSlider, "配置文件-扭曲度"}, {Def::SubSlider, "配置文件-晕影"},
    {Def::Title, "自动约束裁剪开关"}, {Def::Title, "配置文件-开关"}, {Def::Title, "配置文件-厂商"},
    {Def::Title, "配置文件-镜头机型名称"}, {Def::Title, "配置状态"},
    {Def::Section, "透视矫正"},
    {Def::SubSlider, "垂直"}, {Def::SubSlider, "垂直偏移"}, {Def::SubSlider, "水平"}, {Def::SubSlider, "水平偏移"},
    {Def::SubSlider, ui::shell::kWordRotate}, {Def::SubSlider, "缩放"}, {Def::SubSlider, "长宽比"},
    {Def::Title, "AI 透视矫正-开关"}, {Def::Title, "引导线"}, {Def::Title, "引导线模式开关"},
    {Def::Title, "辅助线开关"}, {Def::Title, "锁定裁剪"},
    {Def::Section, "校准"},
    {Def::SubSlider, "红原色-色相"}, {Def::SubSlider, "红原色-饱和度"}, {Def::SubSlider, "绿原色-色相"},
    {Def::SubSlider, "绿原色-饱和度"}, {Def::SubSlider, "蓝原色-色相"}, {Def::SubSlider, "蓝原色-饱和度"},
    {Def::Section, "创意色彩"}, {Def::SubSlider, "程度", {}, "refine-creative-color-amount"},
};

// 后期调色面板在分组标题前多两项。
constexpr std::string_view kPostColorLead[] = {"全图调整", "局部调整"};

constexpr std::array<LabelBinding, 3> kHslChannels = {{{"色相", "hue"}, {"饱和度", "saturation"}, {"明亮度", "luminance"}}};
constexpr std::array<LabelBinding, 8> kHslColors = {{{"红色", "red"},   {"橙色", "orange"}, {"黄色", "yellow"},
                                                     {"绿色", "green"}, {"浅绿色", "aqua"},  {"蓝色", "blue"},
                                                     {"紫色", "purple"}, {"洋红色", "magenta"}}};
constexpr std::array<LabelBinding, 9> kSelectiveTargets = {{{"红色", "red"},     {"黄色", "yellow"},
                                                            {"绿色", "green"},   {"青色", "cyan"},
                                                            {"蓝色", "blue"},    {"洋红色", "magenta"},
                                                            {"白色", "white"},   {"中间色（灰色）", "neutral"},
                                                            {"黑色", "black"}}};
constexpr std::array<LabelBinding, 1> kBwGroup = {{{"黑白混合", "mix"}}};
// 可选值的显示名（配置文件按需求 10.3，调整方式按画面）。
constexpr std::array<LabelBinding, 6> kChoiceLabels = {{{"标准", "standard"}, {"质感", "texture"},
                                                        {"黑白", "monochrome"}, {"基础", "basic"},
                                                        {"相对", "relative"}, {"绝对", "absolute"}}};
constexpr std::array<LabelBinding, 4> kSelectiveSliders = {{{"青色", "cyan"}, {"洋红", "magenta"}, {"黄色", "yellow"}, {"黑色", "black"}}};

// "<前缀显示名>-<后缀显示名>"，两段都要在表里。
template <std::size_t A, std::size_t B>
std::optional<std::string> pair_parameter(std::string_view label, const std::array<LabelBinding, A>& first,
                                          const std::array<LabelBinding, B>& second, std::string_view prefix) {
  const auto dash = label.find('-');
  if (dash == std::string_view::npos) return std::nullopt;
  const std::string_view left = label.substr(0, dash);
  const std::string_view right = label.substr(dash + 1);
  const auto find_in = [](const auto& table, std::string_view name) -> const LabelBinding* {
    for (const auto& entry : table) {
      if (entry.label == name) return &entry;
    }
    return nullptr;
  };
  const LabelBinding* a = find_in(first, left);
  const LabelBinding* b = find_in(second, right);
  if (a == nullptr || b == nullptr) return std::nullopt;
  return std::string(prefix) + std::string(a->parameter_id) + "_" + std::string(b->parameter_id);
}

template <std::size_t A, std::size_t B>
void add_grid(std::vector<PanelRow>& rows, const std::array<LabelBinding, A>& first,
              const std::array<LabelBinding, B>& second) {
  for (const auto& a : first) {
    for (const auto& b : second) {
      PanelRow row;
      row.kind = PanelRowKind::Slider;
      row.label = std::string(a.label) + "-" + std::string(b.label);
      row.binding = refine_subgroup_binding(row.label);
      rows.push_back(std::move(row));
    }
  }
}

std::optional<SliderBinding> find_row(Def def, std::string_view label) {
  for (const auto& row : kColorPanel) {
    if (row.def == def && row.label == label && !row.parameter.empty()) {
      return SliderBinding{std::string(row.capability), std::string(row.parameter)};
    }
  }
  return std::nullopt;
}

}  // namespace

std::optional<SliderBinding> refine_color_binding(std::string_view label) { return find_row(Def::TopSlider, label); }

// 只给子分组滑杆用（色阶、色彩编辑器、颜色分级、HSL、可选颜色）。曲线等子分组里同名的“阴影”“高光”
// 没有参数，不会被绑到顶层影调参数。
std::optional<SliderBinding> refine_subgroup_binding(std::string_view label) {
  if (auto row = find_row(Def::SubSlider, label)) return row;
  // HSL：“色相-红色”等，显示名带模式（色相、饱和度、明亮度）和颜色。
  if (const auto hsl = pair_parameter(label, kHslChannels, kHslColors, "hsl_")) {
    return SliderBinding{kGlobalColor, *hsl};
  }
  // 黑白混合：“黑白混合-红色”等。
  if (const auto mix = pair_parameter(label, kBwGroup, kHslColors, "bw_")) {
    return SliderBinding{kGlobalColor, *mix};
  }
  // 可选颜色：“红色-青色”等，显示名带目标色和滑杆名。
  if (const auto selective = pair_parameter(label, kSelectiveTargets, kSelectiveSliders, "selective_")) {
    return SliderBinding{kGlobalColor, *selective};
  }
  return std::nullopt;
}

std::vector<PanelRow> refine_color_rows() {
  std::vector<PanelRow> rows;
  for (const auto& def : kColorPanel) {
    if (def.def == Def::HslGrid) {
      add_grid(rows, kHslChannels, kHslColors);
      continue;
    }
    if (def.def == Def::BwGrid) {
      add_grid(rows, kBwGroup, kHslColors);
      continue;
    }
    if (def.def == Def::SelectiveGrid) {
      add_grid(rows, kSelectiveTargets, kSelectiveSliders);
      continue;
    }
    PanelRow row;
    row.label = std::string(def.label);
    row.object_name = std::string(def.object_name);
    if (def.def == Def::Choice) {
      row.kind = PanelRowKind::Choice;
      row.binding = SliderBinding{std::string(def.capability), std::string(def.parameter)};
      rows.push_back(std::move(row));
      continue;
    }
    if (def.def == Def::TopSlider || def.def == Def::SubSlider) {
      row.kind = PanelRowKind::Slider;
      row.binding = def.def == Def::TopSlider ? refine_color_binding(def.label) : refine_subgroup_binding(def.label);
    }
    rows.push_back(std::move(row));
  }
  return rows;
}

std::vector<PanelRow> refine_post_color_rows() {
  std::vector<PanelRow> rows;
  bool top = false;  // 只取白平衡、影调（滤镜和色阶之后的分组后期调色没有登记）
  for (const auto& def : kColorPanel) {
    if (def.def == Def::Section) top = def.label == "白平衡" || def.label == "影调";
    if (!top) continue;
    PanelRow row;
    row.label = std::string(def.label);
    if (def.def == Def::Section) {
      rows.push_back(std::move(row));
    } else if (def.def == Def::TopSlider && !def.parameter.empty()) {
      row.kind = PanelRowKind::Slider;
      row.binding = SliderBinding{"color.post", std::string(def.parameter)};
      row.object_name = "refine-post-" + std::string(def.parameter);
      rows.push_back(std::move(row));
    }
  }
  return rows;
}

std::vector<PanelRow> refine_local_rows() {
  std::vector<PanelRow> rows;
  PanelRow region;
  region.kind = PanelRowKind::Choice;
  region.label = kLocalRegion;
  region.binding = SliderBinding{"color.local", "region"};
  region.object_name = "refine-local-region";
  rows.push_back(std::move(region));
  for (const auto& [parameter, label] : {std::pair<const char*, const char*>{"gradient_x", "中心横向"},
                                         {"gradient_y", "中心纵向"},
                                         {"gradient_angle", "渐变角度"},
                                         {"gradient_size", "渐变范围"},
                                         {"gradient_feather", "渐变柔和"},
                                         {"gradient_ratio", "椭圆高宽比"},
                                         {"range_r", "取样色 红"},
                                         {"range_g", "取样色 绿"},
                                         {"range_b", "取样色 蓝"},
                                         {"range_adjust", "调整"},
                                         {"lum_a", "亮度 下限（渐变起点）"},
                                         {"lum_b", "亮度 下限（全选起点）"},
                                         {"lum_c", "亮度 上限（全选终点）"},
                                         {"lum_d", "亮度 上限（渐变终点）"}}) {
    PanelRow row;
    row.kind = PanelRowKind::Slider;
    row.label = label;
    row.binding = SliderBinding{"color.local", parameter};
    row.object_name = std::string("refine-local-") + parameter;
    rows.push_back(std::move(row));
  }
  for (auto row : refine_post_color_rows()) {
    if (row.binding) {
      row.binding->capability_id = "color.local";
      row.object_name = "refine-local-" + row.binding->parameter_id;
    }
    rows.push_back(std::move(row));
  }
  return rows;
}

std::vector<std::string> refine_post_color_titles() {
  std::vector<std::string> titles(std::begin(kPostColorLead), std::end(kPostColorLead));
  for (const auto& def : kColorPanel) {
    if (def.def == Def::Section) titles.emplace_back(def.label);
  }
  return titles;
}

std::vector<ChoiceOption> choice_options(const SliderBinding& binding) {
  std::vector<ChoiceOption> options;
  const ParameterMode* mode = find_parameter_mode(binding.capability_id, binding.parameter_id);
  if (mode == nullptr) return options;
  for (const auto& id : mode->choices) {
    std::string label = id;
    for (const auto& entry : kChoiceLabels) {
      if (entry.parameter_id == id) label = std::string(entry.label);
    }
    if (binding.capability_id == "color.local" && binding.parameter_id == "region") {
      if (id == "all") label = kRegionAll;
      if (id == "linear") label = ui::shell::kWordLinearGradient;
      if (id == "radial") label = ui::shell::kWordRadialGradient;
      if (id == "brush") label = ui::shell::kWordBrush;
      if (id == "color_range") label = ui::shell::kWordColorRange;
      if (id == "luminance_range") label = ui::shell::kWordLuminanceRange;
      for (const auto& part : mask::semantic_parts()) {
        if (id == part.id) label = part.label;
      }
    }
    options.push_back({id, label, id == mode->default_choice});
  }
  return options;
}

const char* refine_color_panel_name() { return kColorAdjust; }
const char* refine_local_feather_label() { return kLocalFeather; }

std::string slider_label_of(const std::string& accessible_name) {
  return accessible_name.substr(0, accessible_name.find(kNameSeparator));
}

bool slider_range_pending(const std::string& accessible_name) {
  const std::string suffix = kPendingSuffix;
  return accessible_name.size() >= suffix.size() &&
         accessible_name.compare(accessible_name.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::optional<SliderBinding> refine_local_binding(std::string_view label) {
  if (label == kLocalFeather) return SliderBinding{"mask", "feather"};
  return std::nullopt;
}

SliderBinding refine_crop_angle_binding() { return SliderBinding{"crop", "angle"}; }

SliderSpec slider_spec(std::string label, const std::optional<SliderBinding>& binding) {
  SliderSpec spec;
  spec.label = std::move(label);
  const ParameterMode* mode =
      binding ? find_parameter_mode(binding->capability_id, binding->parameter_id) : nullptr;
  if (mode == nullptr || mode->step <= 0.0 || mode->maximum < mode->minimum) {
    spec.default_text = "—";
    spec.accessible_name = spec.label + kPendingSuffix;
    return spec;
  }
  spec.bound = true;
  spec.minimum = mode->minimum;
  spec.maximum = mode->maximum;
  spec.step = mode->step;
  spec.default_value = mode->default_value;
  spec.decimals = decimals_for(mode->step);
  spec.tick_count = static_cast<int>(std::lround((mode->maximum - mode->minimum) / mode->step));
  spec.default_tick = static_cast<int>(std::lround((mode->default_value - mode->minimum) / mode->step));
  spec.default_text = format_value(mode->default_value, spec.decimals);
  // 参数模式标明步进或默认值没有来源时，只写“待确认”，不把内部取值当成事实展示。
  const std::string step_text =
      mode->step_confirmed ? "步进 " + format_value(mode->step, spec.decimals) : std::string("步进待确认");
  const std::string default_text =
      mode->default_confirmed ? "默认 " + spec.default_text : std::string("默认待确认");
  spec.accessible_name = spec.label + kNameSeparator + "范围 " + format_value(mode->minimum, spec.decimals) + " 到 " +
                         format_value(mode->maximum, spec.decimals) + kNameSeparator + step_text + kNameSeparator +
                         default_text;
  return spec;
}

}  // namespace pixcake::ui::panels
