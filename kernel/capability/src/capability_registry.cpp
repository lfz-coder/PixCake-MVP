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
                            std::string program, std::string channel, std::uint32_t order) {
    modes.push_back(ParameterMode{std::move(capability), std::move(parameter), minimum, maximum, step,
                                  default_value, std::move(stage), std::move(program), std::move(channel), 1,
                                  order});
  };
  const auto add_color = [&add](const std::string& capability, const std::string& program,
                                std::uint32_t base_order) {
    add(capability, "temperature", -100.0, 100.0, 1.0, 0.0, "color", program, "temperature", base_order);
    add(capability, "color_temperature", -100.0, 100.0, 1.0, 0.0, "color", program, "temperature", base_order);
    add(capability, "tint", -100.0, 100.0, 1.0, 0.0, "color", program, "tint", base_order);
    add(capability, "color_tint", -100.0, 100.0, 1.0, 0.0, "color", program, "tint", base_order);
    add(capability, "exposure", -5.0, 5.0, 0.05, 0.0, "tone", program, "exposure", base_order);
    for (const char* parameter : {"contrast", "brightness", "highlights", "shadows", "whites",
                                  "blacks", "texture", "clarity", "dehaze", "vibrance", "saturation"}) {
      add(capability, parameter, -100.0, 100.0, 1.0, 0.0, "tone", program, parameter, base_order);
    }
  };
  add("color.match", "amount", 0.0, 200.0, 1.0, 100.0, "color", "pixcake.color.match", "amount", 10);
  add("color.style", "amount", 0.0, 200.0, 1.0, 100.0, "color", "pixcake.color.style", "amount", 11);
  add_color("color.global", "pixcake.color.global", 20);
  add_color("color.local", "pixcake.color.local", 21);
  add_color("color.post", "pixcake.color.post", 70);
  add("mask", "feather", 0.0, 100.0, 1.0, 0.0, "mask", "pixcake.mask", "feather", 40);
  add("crop", "angle", -45.0, 45.0, 1.0, 0.0, "geometry", "pixcake.crop", "angle", 50);
  add("portrait.eye_glint", "vertical", -100.0, 100.0, 1.0, 0.0, "portrait", "pixcake.portrait.eye_glint", "vertical", 60);
  add("portrait.eye_glint", "horizontal", -100.0, 100.0, 1.0, 0.0, "portrait", "pixcake.portrait.eye_glint", "horizontal", 60);
  add("portrait.eye_glint", "rotation", -100.0, 100.0, 1.0, 0.0, "portrait", "pixcake.portrait.eye_glint", "rotation", 60);
  add("pet.eye_glint", "vertical", -100.0, 100.0, 1.0, 0.0, "portrait", "pixcake.pet.eye_glint", "vertical", 63);
  add("pet.eye_glint", "horizontal", -100.0, 100.0, 1.0, 0.0, "portrait", "pixcake.pet.eye_glint", "horizontal", 63);
  add("pet.eye_glint", "rotation", -100.0, 100.0, 1.0, 0.0, "portrait", "pixcake.pet.eye_glint", "rotation", 63);
  add("light.uniform", "brightness", -100.0, 100.0, 1.0, 0.0, "image", "pixcake.light.uniform", "brightness", 70);
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

const ParameterMode* find_parameter_mode(std::string_view capability_id, std::string_view parameter_id) {
  const auto& modes = phase_one_parameter_modes();
  for (const auto& mode : modes) {
    if (mode.capability_id == capability_id && mode.parameter_id == parameter_id) {
      return &mode;
    }
  }
  return nullptr;
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
  double value = 0.0;
  if (!parse_number(display_value, value)) return false;
  const ParameterMode* mode = find_parameter_mode(capability_id, parameter_id);
  return mode != nullptr && value >= mode->minimum && value <= mode->maximum &&
         step_aligned(value - mode->minimum, mode->step);
}

}  // namespace pixcake
