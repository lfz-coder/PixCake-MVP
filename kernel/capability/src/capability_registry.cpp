#include "pixcake/capability/capability_registry.hpp"

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
      {"grain", "色彩", "颗粒", true, true, true, ""},
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
  // 程度按官方（test 版实测 0..100）；需求原文 0..200 被用户裁定覆盖（2026-11-05）。
  add_unconfirmed_step("color.match", "amount", 0.0, 100.0, 100.0, "color", "pixcake.color.match", "amount", 10);
  add_unconfirmed_step("color.style", "amount", 0.0, 100.0, 100.0, "color", "pixcake.color.style", "amount", 11);
  add_color("color.global", "pixcake.color.global", 20);
  add_color("color.local", "pixcake.color.local", 21);
  add_color("color.post", "pixcake.color.post", 70);
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
  for (const char* item : {"hue", "saturation", "luminance"}) {
    add_global(std::string("color_editor_") + item + "_shift", -100.0, 100.0, 1.0, 0.0).step_confirmed = false;
  }
  // 颗粒：独立能力（用户 2026-11-05 裁定），各项 0..100 步长 1，默认值取自 test 版画面。
  const char* grain_ids[] = {"amount", "shadows", "midtones", "highlights", "size", "roughness", "color"};
  const double grain_defaults[] = {0, 30, 50, 15, 10, 15, 10};
  for (int i = 0; i < 7; ++i) {
    add("grain", grain_ids[i], 0.0, 100.0, 1.0, grain_defaults[i], "color", "pixcake.grain", grain_ids[i], 80 + i);
  }
  // 黑白档禁用影调里的鲜艳度和饱和度（需求第 10.3 节、第 24 节第 9 条）。
  for (auto& mode : modes) {
    if (mode.capability_id == "color.global" && (mode.parameter_id == "vibrance" || mode.parameter_id == "saturation")) {
      mode.disable_rules.push_back({"color.global", "profile", "monochrome", "黑白配置文件不调节鲜艳度和饱和度"});
    }
  }
  // mask 不是能力：这是 kernel/mask 蒙版属性“羽化”的显示刻度，不写入精修文档片段。
  add_unconfirmed_step("mask", "feather", 0.0, 100.0, 0.0, "mask", "pixcake.mask", "feather", 40);
  ParameterMode& angle = add_unconfirmed_step("crop", "angle", -45.0, 45.0, 0.0, "geometry", "pixcake.crop", "angle", 50);
  angle.default_confirmed = false;
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
      {"grain", "grain", true, {"color.post"}},
      {"manual.liquify", "manual.liquify", true, {"grain"}},
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

const ParameterMode* find_parameter_mode(std::string_view capability_id, std::string_view parameter_id) {
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
    if (!current) {
      const ParameterMode* condition = find_parameter_mode(rule.when_capability_id, rule.when_parameter_id);
      if (condition != nullptr && !condition->default_choice.empty()) current = condition->default_choice;
    }
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
