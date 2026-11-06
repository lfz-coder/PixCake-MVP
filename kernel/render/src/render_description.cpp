#include "pixcake/render/render_description.hpp"

#include "pixcake/capabilities/module_set.hpp"
#include "pixcake/capability/capability_registry.hpp"
#include "pixcake/layerdoc/layer_document.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace pixcake::render {
namespace {

// 能力模块按读取接口取值：拖动中的预览值优先，其次已提交值。
class DocumentReader final : public ParameterReader {
 public:
  explicit DocumentReader(const EditDocument& document) : document_(document) {}

  std::optional<std::string> value(const std::string& capability_id, const std::string& parameter_id) const override {
    auto found = document_.preview(capability_id, parameter_id);
    if (!found) found = document_.committed_parameter(capability_id, parameter_id);
    return found;
  }

  std::uint32_t fragment_version(const std::string& capability_id) const override {
    const auto fragment = document_.fragment(capability_id);
    return fragment ? fragment->version : 1u;
  }

 private:
  const EditDocument& document_;
};

RenderOperation operation_of(DrawStep step) {
  RenderOperation operation;
  operation.capability_id = std::move(step.capability_id);
  operation.stage = std::move(step.stage);
  operation.program = std::move(step.program);
  operation.order = step.order;
  for (auto& value : step.values) {
    operation.values.push_back(
        RenderValue{std::move(value.parameter_id), std::move(value.channel), std::move(value.display_value),
                    value.numeric_value});
  }
  operation.mask_ids = std::move(step.mask_ids);
  operation.blend_mode = std::move(step.blend_mode);
  operation.enabled = step.enabled;
  operation.disabled_reason = std::move(step.disabled_reason);
  return operation;
}

std::optional<std::string> stable_blend_mode(const std::string& blend) {
  if (blend == "正常") return "normal";
  if (blend == "正片叠底") return "multiply";
  if (blend == "滤色") return "screen";
  if (blend == "变亮") return "lighten";
  if (blend == "变暗") return "darken";
  return std::nullopt;
}

}  // namespace

CompileResult compile(const EditDocument& document) {
  const CapabilityRegistry registry = CapabilityRegistry::phase_one();
  return compile(document, registry);
}

CompileResult compile(const EditDocument& document, const CapabilityRegistry& registry) {
  CompileResult result;
  result.description.image_id = document.image_id();
  result.description.editing_instance_id = document.editing_instance_id();
  result.description.document_revision = document.revision();

  if (document.recovered_empty()) {
    result.code = ResultCode::DocumentCorrupt;
    result.description.diagnostics.push_back({"", "文档已恢复为空，不能编译绘制描述", true});
    return result;
  }
  if (document.image_id().empty()) {
    result.code = ResultCode::StateConflict;
    result.description.diagnostics.push_back({"", "文档没有原图编号", true});
    return result;
  }

  const auto& modes = phase_one_parameter_modes();
  std::vector<std::string> capability_ids;
  std::set<std::string> seen_capabilities;
  for (const auto& fragment : document.fragments()) {
    if (seen_capabilities.insert(fragment.capability_id).second) {
      capability_ids.push_back(fragment.capability_id);
    }
  }
  // A drag can update only the in-memory preview copy. Include those
  // capabilities even when no committed fragment exists yet.
  for (const auto& mode : modes) {
    if (document.preview(mode.capability_id, mode.parameter_id).has_value() &&
        seen_capabilities.insert(mode.capability_id).second) {
      capability_ids.push_back(mode.capability_id);
    }
  }

  const DocumentReader reader(document);
  for (const auto& capability_id : capability_ids) {
    const auto* descriptor = registry.find(capability_id);
    if (descriptor == nullptr) {
      result.description.diagnostics.push_back(
          {capability_id, "能力未注册，保留片段但跳过绘制", false});
      continue;
    }
    if (!descriptor->registered_for_drawing) {
      result.description.diagnostics.push_back(
          {capability_id, "能力未注册绘制，保留片段但跳过绘制", false});
      continue;
    }

    // 编译交给能力自己的模块（架构 7.2、契约 C-31）；渲染只把绘制描述换成渲染描述并排序。
    const CapabilityModule* module = find_capability_module(capability_id);
    if (module == nullptr) {
      result.description.diagnostics.push_back({capability_id, "能力没有模块，保留片段但跳过绘制", false});
      continue;
    }
    ModuleCompile compiled = module->compile(reader);
    for (auto& diagnostic : compiled.diagnostics) {
      result.description.diagnostics.push_back(
          {std::move(diagnostic.capability_id), std::move(diagnostic.message), diagnostic.blocking});
    }
    for (auto& step : compiled.steps) result.description.operations.push_back(operation_of(std::move(step)));
  }

  std::sort(result.description.operations.begin(), result.description.operations.end(),
            [](const RenderOperation& left, const RenderOperation& right) {
              if (left.order != right.order) {
                return left.order < right.order;
              }
              return left.capability_id < right.capability_id;
            });
  return result;
}

CompileResult compile(const EditDocument& document, const LayerDocument& layers,
                      const CapabilityRegistry& registry) {
  CompileResult result = compile(document, registry);
  if (layers.recovered_empty()) {
    result.code = ResultCode::DocumentCorrupt;
    result.description.diagnostics.push_back({"layers", "图层文档已恢复为空，不能编译合成输入", true});
    return result;
  }
  if (layers.source_image_id() != document.image_id()) {
    result.code = ResultCode::StateConflict;
    result.description.diagnostics.push_back({"layers", "图层文档和精修文档的原图编号不一致", true});
    return result;
  }

  const auto views = layers.layers();
  std::unordered_set<std::string> ids;
  ids.reserve(views.size());
  for (const auto& layer : views) {
    if (layer.id.empty() || !ids.insert(layer.id).second) {
      result.description.diagnostics.push_back({"layers", "图层标识为空或重复", true});
    }
  }
  std::unordered_map<std::string, std::string> parents;
  for (const auto& layer : views) parents[layer.id] = layer.parent_id;
  for (const auto& layer : views) {
    std::unordered_set<std::string> chain;
    std::string current = layer.id;
    while (!current.empty()) {
      if (!chain.insert(current).second) {
        result.description.diagnostics.push_back({"layers", "图层父子关系存在环路", true});
        break;
      }
      const auto parent = parents.find(current);
      if (parent == parents.end()) break;
      current = parent->second;
    }
  }
  for (std::size_t index = 0; index < views.size(); ++index) {
    const auto& layer = views[index];
    RenderLayer output;
    output.id = layer.id;
    output.parent_id = layer.parent_id;
    output.order = static_cast<std::uint32_t>(index);
    output.opacity = layer.opacity < 0 ? 0u : static_cast<std::uint32_t>(layer.opacity);
    output.hidden = layer.hidden;
    output.group = layer.group;
    output.mask_enabled = layer.mask;
    output.mask_density = layer.mask_density < 0 ? 0u : static_cast<std::uint32_t>(layer.mask_density);
    output.mask_feather = layer.mask_feather < 0 ? 0u : static_cast<std::uint32_t>(layer.mask_feather);
    const auto blend = stable_blend_mode(layer.blend);
    if (!blend) {
      output.enabled = false;
      output.disabled_reason = "图层混合模式未注册";
      result.description.diagnostics.push_back({"layers", output.disabled_reason, true});
    } else {
      output.blend_mode = *blend;
    }
    if (layer.parent_id.empty() ? false : ids.find(layer.parent_id) == ids.end()) {
      output.enabled = false;
      output.disabled_reason = "图层父节点不存在";
      result.description.diagnostics.push_back({"layers", output.disabled_reason, true});
    }
    // 范围来自参数模式（Q-06）：layer/opacity、mask/density、mask/layer_feather。
    if (!validate_parameter_display_value("layer", "opacity", std::to_string(layer.opacity)) ||
        !validate_parameter_display_value("mask", "density", std::to_string(layer.mask_density)) ||
        !validate_parameter_display_value("mask", "layer_feather", std::to_string(layer.mask_feather))) {
      output.enabled = false;
      output.disabled_reason = "图层透明度或蒙版参数越界";
      result.description.diagnostics.push_back({"layers", output.disabled_reason, true});
    }
    result.description.layers.push_back(std::move(output));
  }
  const auto selection = layers.selection();
  result.description.selection.present = selection.present;
  result.description.selection.inverted = selection.inverted;
  result.description.selection.feather = selection.feather < 0 ? 0u : static_cast<std::uint32_t>(selection.feather);
  if (selection.feather < 0 || selection.feather > 1000) {
    result.description.diagnostics.push_back({"layers", "选择区羽化参数越界", true});
  }
  return result;
}

}  // namespace pixcake::render
