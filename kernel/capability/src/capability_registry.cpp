#include "pixcake/capability/capability_registry.hpp"
#include "pixcake/capability/curve_points.hpp"
#include "pixcake/mask/brush_mask.hpp"
#include "pixcake/mask/semantic_parts.hpp"

#include <tuple>
#include <unordered_set>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <string>
#include <queue>
#include <unordered_map>
#include <utility>

namespace pixcake {
namespace {

constexpr char kRecomputeReason[] = "同步时按每张图重新计算，不是把数值原样抄过去";

std::vector<CapabilityDescriptor> phase_one_descriptors() {
  return {
      {"color.match", "AI 色彩", "AI 追色", true, true, false, kRecomputeReason},
      {"color.style", "AI 色彩", "AI 色彩风格", true, true, false, kRecomputeReason},
      {"color.global", "色彩", "全图色彩工具", true, true, true, ""},
      {"color.local", "色彩", "局部调色", true, true, true, ""},
      {"color.post", "色彩", "后期调色", true, true, true, ""},
      // 契约 §4：降噪、锐化、噪点、杂色、颗粒、镜头、透视、校准。管线里按阶段标记拆成多步，见 phase_one_pipeline()。
      {"detail", "色彩", "细节", true, true, true, ""},
      {"portrait.blemish", "人像", "祛除瑕疵", true, true, true, ""},
      {"portrait.skin", "人像", "皮肤调整", true, true, true, ""},
      {"portrait.reshape", "人像", "面部重塑", true, true, true, ""},
      {"portrait.feature", "人像", "表情、牙齿、眼睛、妆容、头发", true, true, true, ""},
      {"portrait.body", "人像", "手、脚、全身美型", true, true, true, ""},
      {"image.background", "图像", "背景调整", true, true, true, ""},
      {"image.idphoto", "图像", "证件照换背景", true, true, true, ""},
      {"image.sky", "图像", "换天空", true, true, true, ""},
      {"clothes", "衣物", "衣物美化", true, true, true, ""},
      {"pet", "宠物", "宠物美化", true, false, true, ""},
      {"crop", "裁剪", "裁剪旋转", true, true, true, ""},
      {"manual.edge", "手动", "祛边", true, true, true, ""},
      {"manual.liquify", "手动", "液化", true, false, true, ""},
      {"manual.heal", "手动", "污点修复、修补、仿制", true, false, true, ""},
  };
}

std::vector<ParameterMode> make_parameter_modes() {
  std::vector<ParameterMode> modes;
  const auto add = [&modes](std::string capability, std::string parameter, double minimum,
                            double maximum, double step, double default_value, std::string stage,
                            std::string program, std::string channel, std::uint32_t order) -> ParameterMode& {
    ParameterMode mode;
    mode.capability_id = std::move(capability);
    mode.parameter_id = std::move(parameter);
    mode.minimum = minimum;
    mode.maximum = maximum;
    mode.step = step;
    mode.default_value = default_value;
    mode.render_stage = std::move(stage);
    mode.render_program = std::move(program);
    mode.render_channel = std::move(channel);
    mode.order = order;
    modes.push_back(std::move(mode));
    return modes.back();
  };
  // 契约只给了范围（和默认值）、没给步进的参数：校验只查范围。
  const auto add_unconfirmed_step = [&add](std::string capability, std::string parameter, double minimum,
                                           double maximum, double default_value, std::string stage,
                                           std::string program, std::string channel,
                                           std::uint32_t order) -> ParameterMode& {
    ParameterMode& mode = add(std::move(capability), std::move(parameter), minimum, maximum, 1.0, default_value,
                              std::move(stage), std::move(program), std::move(channel), order);
    mode.step_confirmed = false;
    return mode;
  };
  const auto add_color = [&add](const std::string& capability, const std::string& program,
                                std::uint32_t base_order) {
    add(capability, "temperature", -100.0, 100.0, 1.0, 0.0, "color", program, "temperature", base_order);
    add(capability, "tint", -100.0, 100.0, 1.0, 0.0, "color", program, "tint", base_order);
    add(capability, "exposure", -5.0, 5.0, 0.05, 0.0, "tone", program, "exposure", base_order);
    for (const char* parameter : {"contrast", "brightness", "highlights", "shadows", "whites",
                                  "blacks", "texture", "clarity", "dehaze", "vibrance", "saturation"}) {
      add(capability, parameter, -100.0, 100.0, 1.0, 0.0, "tone", program, parameter, base_order);
    }
  };
  // AI 追色程度按需求原文 0..200（2026-11-05 用户裁定恢复，追色没有实测）。
  add_unconfirmed_step("color.match", "amount", 0.0, 200.0, 100.0, "color", "pixcake.color.match", "amount", 10);
  // 精准控制开关（需求第 395–397 行，架构“写回的存放与切换”），默认关。
  ParameterMode& precise = add("color.match", "precise", 0.0, 0.0, 1.0, 0.0, "color", "pixcake.color.match", "precise", 10);
  precise.choices = {"off", "on"};
  precise.default_choice = "off";
  // AI 色彩风格程度按官方 0..100（2026-11-05 用户裁定，test 版实测上限 100）；默认值按需求 100。
  add_unconfirmed_step("color.style", "amount", 0.0, 100.0, 100.0, "color", "pixcake.color.style", "amount", 11);
  add_color("color.global", "pixcake.color.global", 20);
  add_color("color.local", "pixcake.color.local", 21);
  add_color("color.post", "pixcake.color.post", 70);
  // 局部调色作用在哪个语义部位（需求 §10.4）：整个画面，或语义部位表里的一个。编译时变成绘制步骤的蒙版
  // "semantic:<部位>"，不是绘制通道。一次只能选一个部位；多块蒙版的列表、手绘选区、渐变、颜色范围还没有。
  ParameterMode& region = add("color.local", "region", 0.0, 0.0, 1.0, 0.0, "color", "pixcake.color.local",
                              "region", 21);
  region.choices.push_back("all");
  region.choices.push_back("linear");  // 线性渐变、径向渐变：蒙版由下面的位置参数在画幅上算出，不需要分割模型
  region.choices.push_back("radial");
  region.choices.push_back("color_range");      // 颜色范围、亮度范围：蒙版由画面本身和下面的取样值算出
  region.choices.push_back("luminance_range");
  region.choices.push_back("brush");  // 画笔：笔触存在同一子项的文本参数 strokes 里
  for (const auto& part : mask::semantic_parts()) region.choices.push_back(part.id);
  region.default_choice = "all";  // 必须在后面的 add() 之前：add 可能让 modes 重新分配，region 引用会失效
  // 渐变的位置和形状（百分比，和分辨率无关）。官方没有逐项实测，范围按画面百分比取，默认值未确认。
  for (const auto& spec : {std::make_tuple("gradient_x", 0.0, 100.0, 50.0), std::make_tuple("gradient_y", 0.0, 100.0, 50.0),
                           std::make_tuple("gradient_angle", -180.0, 180.0, 0.0),
                           std::make_tuple("gradient_size", 0.0, 100.0, 50.0),
                           std::make_tuple("gradient_feather", 0.0, 100.0, 50.0),
                           std::make_tuple("gradient_ratio", 5.0, 400.0, 100.0)}) {
    ParameterMode& gradient = add("color.local", std::get<0>(spec), std::get<1>(spec), std::get<2>(spec), 1.0,
                                  std::get<3>(spec), "color", "pixcake.color.local", std::get<0>(spec), 21);
    gradient.default_confirmed = false;
  }
  // 颜色范围的取样色（RGB）和调整、亮度范围的梯形四个值（官方 2026-11-08 实测，见 evidence/2026-11-08-range-mask-observation.md）。
  for (const auto& spec : {std::make_tuple("range_r", 0.0, 255.0, 128.0), std::make_tuple("range_g", 0.0, 255.0, 128.0),
                           std::make_tuple("range_b", 0.0, 255.0, 128.0), std::make_tuple("range_adjust", 1.0, 100.0, 50.0),
                           std::make_tuple("lum_a", 0.0, 100.0, 20.0), std::make_tuple("lum_b", 0.0, 100.0, 45.0),
                           std::make_tuple("lum_c", 0.0, 100.0, 65.0), std::make_tuple("lum_d", 0.0, 100.0, 90.0)}) {
    ParameterMode& range = add("color.local", std::get<0>(spec), std::get<1>(spec), std::get<2>(spec), 1.0,
                               std::get<3>(spec), "color", "pixcake.color.local", std::get<0>(spec), 21);
    range.default_confirmed = false;
  }
  // 子项和上一步的叠加方式：添加（并集）或减去。只有同一蒙版里有多个子项时才起作用；官方的合成像素没有核对。
  {
    ParameterMode& combine = add("color.local", "mode", 0.0, 0.0, 1.0, 0.0, "color", "pixcake.color.local", "mode", 21);
    combine.choices = {"add", "subtract"};
    combine.default_choice = "add";
  }
  // 子项反相（官方“显示蒙版反相”，2026-11-08 实测是真的反转这个子项的选区）：每个子项自己有一个，默认关。
  {
    ParameterMode& invert = add("color.local", "invert", 0.0, 0.0, 1.0, 0.0, "color", "pixcake.color.local", "invert", 21);
    invert.choices = {"off", "on"};
    invert.default_choice = "off";
  }
  // 画笔笔触（子项的文本参数，见 kernel/mask/brush_mask.hpp 的格式）：整串合法才收；不能同步、不能存预设。
  {
    ParameterMode& strokes = add("color.local", "strokes", 0.0, 0.0, 1.0, 0.0, "color", "pixcake.color.local", "strokes", 21);
    strokes.text = true;
    strokes.max_length = static_cast<std::uint32_t>(mask::kMaxBrushTextChars);
  }
  // 蒙版名称（只有蒙版本身有，子项没有）：1 到 20 个字符的文本，默认没有（界面显示“自定义-N”）。
  {
    ParameterMode& name = add("color.local", "name", 0.0, 0.0, 1.0, 0.0, "color", "pixcake.color.local", "name", 21);
    name.text = true;
    name.max_length = 20;
  }
  // 局部调色允许多个蒙版（参数编号带 m<N>: 前缀），旧版本程序读到这个片段要整体跳过，所以版本升到 2。
  for (auto& mode : modes) {
    if (mode.capability_id == "color.local") mode.schema_version = 2;
  }
  // 配置文件（需求第 10.3 节）：标准、质感、黑白、基础，默认标准。
  ParameterMode& profile = add("color.global", "profile", 0.0, 0.0, 1.0, 0.0, "color", "pixcake.color.global",
                               "profile", 20);
  profile.choices = {"standard", "texture", "monochrome", "basic"};
  profile.default_choice = "standard";
  // 色阶（RGB 复合通道）。需求 §10.3：输入色阶的黑、灰、白，输出色阶的黑、白。
  // 来源：架构附录 B「色阶」读数和 2026-11-05 在 test 版上的数值框实测（越界被拒）。
  // 上排两个输出框互相约束（最小间隔未测定），这里只校验各自范围；灰场两位小数，步进待确认。
  // 红、绿、蓝通道没有实测，不注册。
  const auto add_global = [&add](const std::string& parameter, double minimum, double maximum, double step,
                                 double default_value) -> ParameterMode& {
    return add("color.global", parameter, minimum, maximum, step, default_value, "color", "pixcake.color.global",
               parameter, 20);
  };
  add_global("levels_rgb_input_black", 0.0, 255.0, 1.0, 0.0);
  add_global("levels_rgb_input_gray", -1.0, 1.0, 0.01, 0.0).step_confirmed = false;
  add_global("levels_rgb_input_white", 0.0, 255.0, 1.0, 255.0);
  add_global("levels_rgb_output_black", 0.0, 255.0, 1.0, 0.0);
  add_global("levels_rgb_output_white", 0.0, 255.0, 1.0, 255.0);
  // HSL：八个颜色各自的色相、饱和度、明亮度（需求 §10.3），-100 到 100，步长 1，默认 0（2026-11-05 用户给出，
  // 色相行另有读回确认）。黑白配置文件把 HSL 换成黑白组，所以黑白档下这 24 项不可编辑。
  for (const char* channel : {"hue", "saturation", "luminance"}) {
    for (const char* color : {"red", "orange", "yellow", "green", "aqua", "blue", "purple", "magenta"}) {
      ParameterMode& hsl = add_global(std::string("hsl_") + channel + "_" + color, -100.0, 100.0, 1.0, 0.0);
      hsl.disable_rules.push_back({"color.global", "profile", "monochrome", "黑白配置文件下 HSL 换成黑白组"});
    }
  }
  // 校准：PhotoCraft Camera Raw 阶段 1b（camera_raw.rs calibrate）。官方面板是红、绿、蓝原色各一对色相、
  // 饱和度滑杆（2026-11-05 页面图名称；范围按滑杆惯例填写，未实测），PhotoCraft 也把它们夹在 -100..100，
  // 默认 0，步进待确认。官方面板没有“阴影-色调”，不登记。
  for (const char* primary : {"red", "green", "blue"}) {
    for (const char* item : {"hue", "saturation"}) {
      add_global(std::string("calibration_") + primary + "_" + item, -100.0, 100.0, 1.0, 0.0).step_confirmed = false;
    }
  }
  // 曲线：PhotoCraft Camera Raw 第 5 阶段（camera_raw.rs parametric_lut、curve_lut）。官方面板有参数曲线
  // （高光、亮调、暗调、阴影）和 RGB、红、绿、蓝通道曲线（2026-11-06 读到入口和控件，范围未实测），
  // 这里按 PhotoCraft 的范围登记：参数曲线 -100 到 100、默认 0，步进待确认；分割点不登记，固定为
  // PhotoCraft 默认 25/50/75（官方面板没有分割点控件）。点曲线是文本 "x,y;..."，按 validate_curve 校验
  // （curve_points.hpp）。官方的“亮度通道曲线”在 Camera Raw 里没有对应，不登记。
  for (const char* region : {"highlights", "lights", "darks", "shadows"}) {
    add_global(std::string("curve_") + region, -100.0, 100.0, 1.0, 0.0).step_confirmed = false;
  }
  for (const char* channel : {"rgb", "red", "green", "blue"}) {
    ParameterMode& points = add_global(std::string("curve_points_") + channel, 0.0, 0.0, 1.0, 0.0);
    points.text = true;
    points.max_length = 512;
  }
  // 可选颜色：九个目标色，每个目标四个滑杆，-100 到 100，步长 1，默认 0（2026-11-05 用户给出）。
  // 目标色和滑杆名取自 test 版画面；调整方式默认「相对」。
  for (const char* target : {"red", "yellow", "green", "cyan", "blue", "magenta", "white", "neutral", "black"}) {
    for (const char* slider : {"cyan", "magenta", "yellow", "black"}) {
      add_global(std::string("selective_") + target + "_" + slider, -100.0, 100.0, 1.0, 0.0);
    }
  }
  ParameterMode& selective_mode = add_global("selective_mode", 0.0, 0.0, 1.0, 0.0);
  selective_mode.choices = {"relative", "absolute"};
  selective_mode.default_choice = "relative";
  // 颜色分级：阴影、中间调、高光、全局四个模式，各有明度、饱和度、色相；另有混合。
  // 来源：2026-11-05 test 版数值框实测（四个模式一致，越界被拒）。只测了整数输入，步进待确认。
  // 平衡只用滑杆读到过 -100 到 100，没有用数值框核对，不注册。
  for (const char* zone : {"shadows", "midtones", "highlights", "global"}) {
    add_global(std::string("grading_") + zone + "_luminance", -100.0, 100.0, 1.0, 0.0).step_confirmed = false;
    add_global(std::string("grading_") + zone + "_saturation", 0.0, 100.0, 1.0, 0.0).step_confirmed = false;
    add_global(std::string("grading_") + zone + "_hue", 0.0, 359.0, 1.0, 0.0).step_confirmed = false;
  }
  add_global("grading_blending", 0.0, 100.0, 1.0, 50.0).step_confirmed = false;
  // 色彩编辑器：色相、饱和度、明亮度偏移 -100 到 100，默认 0（官方《基础调色功能说明》）。
  // 手册没写步进，标待确认。色彩平滑、亮度平滑、色彩均匀度没有官方数字也没有实测，不注册。
  // 黑白档下色彩编辑器从面板消失（2026-11-06 test 版实测），所以三项偏移在黑白档不可编辑。
  for (const char* item : {"hue", "saturation", "luminance"}) {
    ParameterMode& shift = add_global(std::string("color_editor_") + item + "_shift", -100.0, 100.0, 1.0, 0.0);
    shift.step_confirmed = false;
    shift.disable_rules.push_back({"color.global", "profile", "monochrome", "黑白配置文件下没有色彩编辑器"});
  }
  // 黑白组（需求第 419 行“黑白档把 HSL 换成黑白组”）：黑白混合 8 色，-100..100、整数、默认 0
  // （2026-11-06 test 版数值框实测）。只在黑白档可编辑；禁用规则只支持“等于某值时禁用”，所以对其余三档各写一条。
  for (const char* color : {"red", "orange", "yellow", "green", "aqua", "blue", "purple", "magenta"}) {
    ParameterMode& mix = add_global(std::string("bw_mix_") + color, -100.0, 100.0, 1.0, 0.0);
    for (const char* other : {"standard", "texture", "basic"}) {
      mix.disable_rules.push_back({"color.global", "profile", other, "黑白混合只在黑白配置文件下可用"});
    }
  }
  // 颗粒：`detail` 的一组参数（契约 §4）。自定义工具栏里的“颗粒”是对这组参数的引用。
  // 各项 0..100 步长 1（2026-11-05 用户给出），默认值取自 test 版画面。
  // 架构只规定了镜头、透视、降噪的管线位置；颗粒没有对应管线步骤，按 order 排在全部管线步骤之后。
  const char* grain_ids[] = {"grain_amount", "grain_shadows", "grain_midtones", "grain_highlights",
                             "grain_size", "grain_roughness", "grain_color"};
  const double grain_defaults[] = {0, 30, 50, 15, 10, 15, 10};
  for (int i = 0; i < 7; ++i) {
    ParameterMode& grain = add("detail", grain_ids[i], 0.0, 100.0, 1.0, grain_defaults[i], "detail.grain",
                               "pixcake.detail", grain_ids[i], 80 + i);
    // 只有颗粒效果不为 0 时，下面各项才可以调节（2026-11-05 用户给出）。
    if (i > 0) grain.disable_rules.push_back({"detail", "grain_amount", "", "颗粒效果为 0 时不可调节", true});
  }
  // 锐化与降噪：PhotoCraft Camera Raw 阶段 8（camera_raw.rs detail），在颗粒之前执行。官方“细节”分组有
  // 锐化（半径、细节）、噪点消除（对比度、细节）、减少杂色（平滑度、细节）（2026-11-05 页面图名称，范围未实测）。
  // 对应：噪点消除 = Camera Raw 明亮度降噪（只有它带“对比”），减少杂色 = 颜色降噪（只有它带“平滑度”）。
  // 范围、默认值取 Camera Raw（PhotoCraft 默认）：锐化 0..150、半径 0.5..3.0（步进 0.1）默认 1.0、细节默认 25；
  // 降噪 0..100、细节默认 50。PhotoCraft 没有的“对比度”“平滑度”和官方没有的“蒙版”不登记。数量为 0 时子项不可调。
  const struct {
    const char* id;
    double minimum, maximum, step, default_value;
    const char* amount;  // 子项所属的数量；数量本身为空
    const char* reason;
  } detail_modes[] = {
      {"sharpen_amount", 0.0, 150.0, 1.0, 0.0, "", ""},
      {"sharpen_radius", 0.5, 3.0, 0.1, 1.0, "sharpen_amount", "锐化为 0 时不可调节"},
      {"sharpen_detail", 0.0, 100.0, 1.0, 25.0, "sharpen_amount", "锐化为 0 时不可调节"},
      {"noise_luminance", 0.0, 100.0, 1.0, 0.0, "", ""},
      {"noise_luminance_detail", 0.0, 100.0, 1.0, 50.0, "noise_luminance", "噪点消除为 0 时不可调节"},
      {"noise_color", 0.0, 100.0, 1.0, 0.0, "", ""},
      {"noise_color_detail", 0.0, 100.0, 1.0, 50.0, "noise_color", "减少杂色为 0 时不可调节"},
  };
  std::uint32_t detail_order = 87;
  for (const auto& m : detail_modes) {
    ParameterMode& mode = add("detail", m.id, m.minimum, m.maximum, m.step, m.default_value, "detail.sharpen",
                              "pixcake.detail", m.id, detail_order++);
    mode.step_confirmed = false;
    mode.default_confirmed = m.default_value == 0.0;
    if (*m.amount != '\0') mode.disable_rules.push_back({"detail", m.amount, "", m.reason, true});
  }
  // 黑白档禁用影调里的鲜艳度和饱和度（需求第 10.3 节、第 24 节第 9 条）。
  for (auto& mode : modes) {
    if (mode.capability_id == "color.global" && (mode.parameter_id == "vibrance" || mode.parameter_id == "saturation")) {
      mode.disable_rules.push_back({"color.global", "profile", "monochrome", "黑白配置文件不调节鲜艳度和饱和度"});
    }
  }
  // mask 不是能力：这是 kernel/mask 蒙版属性“羽化”的显示刻度，不写入精修文档片段。
  add_unconfirmed_step("mask", "feather", 0.0, 100.0, 0.0, "mask", "pixcake.mask", "feather", 40);
  // 祛路人选区羽化 0 到 1000（边界 B-33）。选区归 mask（契约 §4 第 76 行），由图层文档保存。
  ParameterMode& selection_feather =
      add("mask", "selection_feather", 0.0, 1000.0, 1.0, 0.0, "mask", "pixcake.mask", "selection_feather", 41);
  selection_feather.default_confirmed = false;
  // 创意图层蒙版的密度 0 到 100、默认 100，羽化 0 到 1000、默认 0，步进都是 1（2026-11-06 test 版实测，
  // 架构附录 B“创意图层蒙版”）。由图层文档保存，不写入精修文档片段；与上面精修蒙版的 feather 是两个控件。
  add("mask", "density", 0.0, 100.0, 1.0, 100.0, "mask", "pixcake.mask", "density", 43);
  add("mask", "layer_feather", 0.0, 1000.0, 1.0, 0.0, "mask", "pixcake.mask", "layer_feather", 44);
  // layer 不是能力：图层不透明度 0 到 100（边界 B-33、契约 C-28），由图层文档保存，不写入精修文档片段。
  // 需求没有写默认值；新图层按 100 处理，标待确认。
  ParameterMode& opacity = add("layer", "opacity", 0.0, 100.0, 1.0, 100.0, "layer", "", "opacity", 42);
  opacity.default_confirmed = false;
  ParameterMode& angle = add_unconfirmed_step("crop", "angle", -45.0, 45.0, 0.0, "geometry", "pixcake.crop", "angle", 50);
  angle.default_confirmed = false;
  // 裁剪旋转的其余几何参数（需求 §12.6）：顺时针转几个 90°、水平和垂直翻转、居中的固定比例裁剪。官方没有逐项实测，
  // 范围和选项按需求写的功能列出；交互式拖框裁剪、证件照尺寸、长宽分辨率等没有登记。
  ParameterMode& turns = add("crop", "rotate", 0.0, 3.0, 1.0, 0.0, "geometry", "pixcake.crop", "rotate", 50);
  turns.default_confirmed = false;
  for (const char* flip : {"flip_horizontal", "flip_vertical"}) {
    ParameterMode& mode = add("crop", flip, 0.0, 0.0, 1.0, 0.0, "geometry", "pixcake.crop", flip, 50);
    mode.choices = {"off", "on"};
    mode.default_choice = "off";
  }
  ParameterMode& ratio = add("crop", "ratio", 0.0, 0.0, 1.0, 0.0, "geometry", "pixcake.crop", "ratio", 50);
  ratio.choices = {"original", "1:1", "4:3", "3:4", "3:2", "2:3", "16:9", "9:16", "5:4", "4:5"};
  ratio.default_choice = "original";
  for (const char* axis : {"vertical", "horizontal", "rotation"}) {
    const std::string parameter = std::string("eye_glint_") + axis;
    add_unconfirmed_step("portrait.feature", parameter, -100.0, 100.0, 0.0, "portrait",
                         "pixcake.portrait.feature", parameter, 60);
    add_unconfirmed_step("pet", parameter, -100.0, 100.0, 0.0, "portrait", "pixcake.pet", parameter, 63);
  }
  add_unconfirmed_step("image.background", "light_uniform_brightness", -100.0, 100.0, 0.0, "image",
                       "pixcake.image.background", "light_uniform_brightness", 70);
  // 导出品质：0 到 14 的整数（需求第 19 节）；默认值需求未写。export 不是能力，不写入精修文档。
  ParameterMode& quality = add("export", "quality", 0.0, 14.0, 1.0, 0.0, "export", "", "quality", 0);
  quality.default_confirmed = false;
  return modes;
}

}  // namespace

CapabilityRegistry::CapabilityRegistry(std::vector<CapabilityDescriptor> descriptors)
    : descriptors_(std::move(descriptors)) {}

CapabilityRegistry CapabilityRegistry::phase_one() {
  return CapabilityRegistry(phase_one_descriptors());
}

const CapabilityDescriptor* CapabilityRegistry::find(std::string_view id) const {
  for (const auto& descriptor : descriptors_) {
    if (descriptor.id == id) {
      return &descriptor;
    }
  }
  return nullptr;
}

const std::vector<CapabilityDescriptor>& CapabilityRegistry::descriptors() const { return descriptors_; }

std::vector<PipelineStep> phase_one_pipeline() {
  return {
      {"detail.geometry", "detail", false, {}},
      {"detail.denoise", "detail", false, {"detail.geometry"}},
      {"color.match", "color.match", true, {"detail.denoise"}},
      {"color.style", "color.style", true, {"color.match"}},
      {"color.global", "color.global", true, {"color.style"}},
      {"color.local", "color.local", true, {"color.global"}},
      {"portrait.blemish", "portrait.blemish", true, {"color.local"}},
      {"portrait.skin", "portrait.skin", true, {"portrait.blemish"}},
      {"portrait.reshape", "portrait.reshape", true, {"portrait.skin"}},
      {"portrait.feature", "portrait.feature", true, {"portrait.reshape"}},
      {"portrait.body", "portrait.body", true, {"portrait.feature"}},
      {"image.background", "image.background", true, {"portrait.body"}},
      {"image.idphoto", "image.idphoto", true, {"image.background"}},
      {"image.sky", "image.sky", true, {"image.idphoto"}},
      {"clothes", "clothes", true, {"image.sky"}},
      {"pet", "pet", true, {"clothes"}},
      {"color.post", "color.post", true, {"pet"}},
      {"manual.liquify", "manual.liquify", true, {"color.post"}},
      {"manual.heal", "manual.heal", true, {"manual.liquify"}},
      {"manual.edge", "manual.edge", true, {"manual.heal"}},
      {"crop", "crop", true, {"manual.edge"}},
  };
}

bool validate_phase_one_pipeline(const std::vector<PipelineStep>& pipeline, std::string* error) {
  if (error != nullptr) {
    error->clear();
  }
  std::unordered_set<std::string> seen_capabilities;
  std::unordered_set<std::string> seen_steps;
  std::unordered_map<std::string, std::size_t> positions;
  for (const auto& step : pipeline) {
    if (step.step_id.empty()) {
      if (error != nullptr) *error = "管线步骤标识为空";
      return false;
    }
    if (!seen_steps.insert(step.step_id).second) {
      if (error != nullptr) *error = "管线步骤重复: " + step.step_id;
      return false;
    }
    positions.emplace(step.step_id, positions.size());
    if (step.capability_id == "detail") {
    } else if (!seen_capabilities.insert(step.capability_id).second) {
      if (error != nullptr) {
        *error = "能力在管线中重复: " + step.capability_id;
      }
      return false;
    }
  }
  std::vector<std::size_t> indegree(pipeline.size(), 0);
  std::vector<std::vector<std::size_t>> outgoing(pipeline.size());
  for (std::size_t index = 0; index < pipeline.size(); ++index) {
    for (const auto& dependency : pipeline[index].dependencies) {
      const auto found = positions.find(dependency);
      if (found == positions.end()) {
        if (error != nullptr) *error = "管线依赖不存在: " + dependency;
        return false;
      }
      ++indegree[index];
      outgoing[found->second].push_back(index);
    }
  }
  std::queue<std::size_t> ready;
  for (std::size_t index = 0; index < indegree.size(); ++index) {
    if (indegree[index] == 0) ready.push(index);
  }
  std::size_t visited = 0;
  while (!ready.empty()) {
    const std::size_t current = ready.front();
    ready.pop();
    ++visited;
    for (const std::size_t next : outgoing[current]) {
      if (--indegree[next] == 0) ready.push(next);
    }
  }
  if (visited != pipeline.size()) {
    if (error != nullptr) *error = "管线存在依赖环路";
    return false;
  }
  return true;
}

std::vector<std::string> phase_one_pipeline_order(std::string* error) {
  const auto pipeline = phase_one_pipeline();
  if (!validate_phase_one_pipeline(pipeline, error)) return {};
  std::unordered_map<std::string, std::size_t> positions;
  for (std::size_t index = 0; index < pipeline.size(); ++index) {
    positions.emplace(pipeline[index].step_id, index);
  }
  std::vector<std::size_t> indegree(pipeline.size(), 0);
  std::vector<std::vector<std::size_t>> outgoing(pipeline.size());
  for (std::size_t index = 0; index < pipeline.size(); ++index) {
    for (const auto& dependency : pipeline[index].dependencies) {
      const std::size_t dependency_index = positions.at(dependency);
      ++indegree[index];
      outgoing[dependency_index].push_back(index);
    }
  }
  std::queue<std::size_t> ready;
  for (std::size_t index = 0; index < indegree.size(); ++index) {
    if (indegree[index] == 0) ready.push(index);
  }
  std::vector<std::string> order;
  order.reserve(pipeline.size());
  while (!ready.empty()) {
    const std::size_t current = ready.front();
    ready.pop();
    order.push_back(pipeline[current].step_id);
    for (const std::size_t next : outgoing[current]) {
      if (--indegree[next] == 0) ready.push(next);
    }
  }
  return order;
}

std::optional<std::uint32_t> phase_one_capability_order(std::string_view capability_id) {
  std::string error;
  const auto pipeline = phase_one_pipeline();
  const auto order = phase_one_pipeline_order(&error);
  if (!error.empty()) return std::nullopt;
  std::uint32_t rank = 0;
  for (const auto& step_id : order) {
    for (const auto& step : pipeline) {
      if (step.step_id == step_id && step.capability_id == capability_id && step.capability_id != "detail") {
        return rank;
      }
    }
    ++rank;
  }
  return std::nullopt;
}

const std::vector<ParameterMode>& phase_one_parameter_modes() {
  static const std::vector<ParameterMode> modes = make_parameter_modes();
  return modes;
}

const MatchWritebackSwitch& match_writeback_switch() {
  static const MatchWritebackSwitch kSwitch{"color.match", "precise", "on", {"color.global", "color.local"}};
  return kSwitch;
}

bool split_local_parameter(std::string_view parameter_id, int& instance, std::string_view& base) {
  instance = 0;
  base = parameter_id;
  if (parameter_id.size() < 3 || parameter_id[0] != 'm') return true;  // 基础参数没有 m 开头的以外的冲突：下面再查表
  const auto colon = parameter_id.find(':');
  if (colon == std::string_view::npos) return true;
  const std::string_view digits = parameter_id.substr(1, colon - 1);
  if (digits.empty() || digits.size() > 2 || digits[0] == '0') return false;
  int value = 0;
  for (const char c : digits) {
    if (c < '0' || c > '9') return false;
    value = value * 10 + (c - '0');
  }
  if (value < 1 || value > kMaxLocalInstances) return false;
  instance = value;
  base = parameter_id.substr(colon + 1);
  return !base.empty();
}

bool split_local_item(std::string_view base, int& item, std::string_view& leaf) {
  item = 0;
  leaf = base;
  if (base.size() < 3 || base[0] != 's') return true;
  const auto colon = base.find(':');
  if (colon == std::string_view::npos) return true;
  const std::string_view digits = base.substr(1, colon - 1);
  if (digits.empty() || digits.size() > 2 || digits[0] == '0') return false;
  int value = 0;
  for (const char c : digits) {
    if (c < '0' || c > '9') return false;
    value = value * 10 + (c - '0');
  }
  if (value < 1 || value > kMaxLocalItems) return false;
  item = value;
  leaf = base.substr(colon + 1);
  return !leaf.empty();
}

std::string local_item_prefix(int item) { return item <= 0 ? std::string() : "s" + std::to_string(item) + ":"; }

std::string local_instance_prefix(int instance) { return instance <= 0 ? std::string() : "m" + std::to_string(instance) + ":"; }

const ParameterMode* find_parameter_mode(std::string_view capability_id, std::string_view parameter_id) {
  if (capability_id == "color.local") {
    int instance = 0;
    std::string_view base;
    if (!split_local_parameter(parameter_id, instance, base)) return nullptr;
    int item = 0;
    std::string_view leaf;
    if (!split_local_item(base, item, leaf)) return nullptr;
    if (item > 0 && leaf != "region" && leaf != "mode" && leaf != "invert" && leaf != "strokes" && leaf.rfind("gradient_", 0) != 0 && leaf.rfind("range_", 0) != 0 &&
        leaf.rfind("lum_", 0) != 0) {
      return nullptr;
    }  // 子项只有自己的参数
    parameter_id = leaf;
  }
  const auto& modes = phase_one_parameter_modes();
  for (const auto& mode : modes) {
    if (mode.capability_id == capability_id && mode.parameter_id == parameter_id) {
      return &mode;
    }
  }
  return nullptr;
}

std::optional<std::string> parameter_disabled_reason(
    std::string_view capability_id, std::string_view parameter_id,
    const std::function<std::optional<std::string>(std::string_view, std::string_view)>& committed_value) {
  const ParameterMode* mode = find_parameter_mode(capability_id, parameter_id);
  if (mode == nullptr) return std::nullopt;
  for (const auto& rule : mode->disable_rules) {
    std::optional<std::string> current = committed_value ? committed_value(rule.when_capability_id, rule.when_parameter_id)
                                                         : std::nullopt;
    const ParameterMode* condition = find_parameter_mode(rule.when_capability_id, rule.when_parameter_id);
    if (rule.when_zero) {
      double number = condition != nullptr ? condition->default_value : 0.0;
      if (current) {
        char* end = nullptr;
        const double parsed = std::strtod(current->c_str(), &end);
        if (end != current->c_str() && *end == '\0') number = parsed;
      }
      if (number == 0.0) return rule.reason;
      continue;
    }
    if (!current && condition != nullptr && !condition->default_choice.empty()) current = condition->default_choice;
    if (current && *current == rule.when_choice) return rule.reason;
  }
  return std::nullopt;
}

bool parse_number(std::string_view text, double& value) {
  if (text.empty()) return false;
  std::string copy(text);
  char* end = nullptr;
  errno = 0;
  value = std::strtod(copy.c_str(), &end);
  return errno == 0 && end == copy.c_str() + copy.size() && std::isfinite(value);
}

bool step_aligned(double value, double step) {
  if (step <= 0.0) return false;
  const double quotient = value / step;
  return std::abs(quotient - std::round(quotient)) <= 1e-9;
}

bool validate_parameter_display_value(std::string_view capability_id, std::string_view parameter_id,
                                      std::string_view display_value) {
  const ParameterMode* mode = find_parameter_mode(capability_id, parameter_id);
  if (mode == nullptr) return false;
  if (mode->text) {
    // 按字符数（UTF-8 起始字节）计，不含控制字符。
    std::uint32_t characters = 0;
    for (const char c : display_value) {
      const auto byte = static_cast<unsigned char>(c);
      if (byte < 0x20 || byte == 0x7f) return false;
      if ((byte & 0xC0) != 0x80) ++characters;
    }
    if (characters < 1 || characters > mode->max_length) return false;
    if (is_curve_points_parameter(mode->parameter_id)) {
      std::vector<std::array<float, 2>> points;
      return decode_curve_points(display_value, points);
    }
    if (mode->parameter_id == "strokes") {
      std::vector<mask::BrushStroke> parsed;
      return mask::decode_brush_strokes(display_value, parsed) && !parsed.empty();
    }
    return true;
  }
  if (!mode->choices.empty()) {
    for (const auto& choice : mode->choices) {
      if (choice == display_value) return true;
    }
    return false;
  }
  double value = 0.0;
  if (!parse_number(display_value, value)) return false;
  if (value < mode->minimum || value > mode->maximum) return false;
  return !mode->step_confirmed || step_aligned(value - mode->minimum, mode->step);
}

}  // namespace pixcake
