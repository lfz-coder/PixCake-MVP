#include "pixcake/gpu/render_executor.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_set>
namespace pixcake::gpu {

namespace {

bool known_blend_mode(const std::string& blend_mode) {
  return blend_mode == "normal" || blend_mode == "multiply" || blend_mode == "screen" ||
         blend_mode == "lighten" || blend_mode == "darken";
}

}  // namespace

ResultCode RenderExecutor::validate(const render::RenderDescription& description,
                                    std::string& error) {
  if (description.schema_version != render::kRenderDescriptionVersion) {
    error = "render description version is unsupported";
    return ResultCode::CapabilityTooNew;
  }
  std::uint32_t previous_order = 0;
  bool first_operation = true;
  for (const auto& operation : description.operations) {
    if (operation.capability_id.empty() || operation.stage.empty() || operation.program.empty()) {
      error = "render operation identity is empty";
      return ResultCode::StateConflict;
    }
    if (!first_operation && operation.order < previous_order) {
      error = "render operations are out of pipeline order";
      return ResultCode::StateConflict;
    }
    previous_order = operation.order;
    first_operation = false;
    if (!operation.enabled) {
      if (operation.disabled_reason.empty()) {
        error = "disabled render operation has no reason";
        return ResultCode::StateConflict;
      }
      continue;
    }
    for (const auto& value : operation.values) {
      if (value.parameter_id.empty() || value.channel.empty() || value.display_value.empty() ||
          !std::isfinite(value.numeric_value)) {
        error = "render value is malformed";
        return ResultCode::StateConflict;
      }
    }
  }

  std::unordered_set<std::string> layer_ids;
  for (const auto& layer : description.layers) {
    if (layer.id.empty() || !layer_ids.insert(layer.id).second) {
      error = "render layer identity is empty or duplicated";
      return ResultCode::StateConflict;
    }
    if (layer.opacity > 100 || layer.mask_density > 100 || layer.mask_feather > 100) {
      error = "render layer range is invalid";
      return ResultCode::StateConflict;
    }
    if (!known_blend_mode(layer.blend_mode)) {
      error = "render layer blend mode is unknown";
      return ResultCode::StateConflict;
    }
    if (!layer.parent_id.empty() && layer.parent_id == layer.id) {
      error = "render layer cannot parent itself";
      return ResultCode::StateConflict;
    }
  }
  for (const auto& layer : description.layers) {
    if (!layer.parent_id.empty() && layer_ids.find(layer.parent_id) == layer_ids.end()) {
      error = "render layer parent is missing";
      return ResultCode::StateConflict;
    }
  }
  for (const auto& layer : description.layers) {
    std::unordered_set<std::string> chain;
    std::string current = layer.id;
    while (!current.empty()) {
      if (!chain.insert(current).second) {
        error = "render layer parent relation contains a cycle";
        return ResultCode::StateConflict;
      }
      const auto found = std::find_if(description.layers.begin(), description.layers.end(),
                                      [&](const render::RenderLayer& candidate) {
                                        return candidate.id == current;
                                      });
      if (found == description.layers.end()) break;
      current = found->parent_id;
    }
  }
  if (description.selection.feather > 1000) {
    error = "render selection feather is invalid";
    return ResultCode::StateConflict;
  }
  for (const auto& diagnostic : description.diagnostics) {
    if (diagnostic.blocking) {
      error = diagnostic.reason.empty() ? "render description has a blocking diagnostic"
                                        : diagnostic.reason;
      return ResultCode::StateConflict;
    }
  }
  error.clear();
  return ResultCode::Ok;
}

bool RenderExecutor::execute(gl::Context& context, const render::RenderDescription& description,
                             const std::vector<Pass>& passes, std::string& error) const {
  const ResultCode valid = validate(description, error);
  if (valid != ResultCode::Ok) return false;
  if (passes.size() != description.operations.size()) {
    error = "render pass count does not match render description";
    return false;
  }
  return execute(context, passes, error);
}

bool RenderExecutor::execute(gl::Context& context, const std::vector<Pass>& passes,
                             std::string& error) const {
  if (!context.valid()) { error = "render context is empty"; return false; }
  for (const auto& pass : passes) {
    if (!pass) { error = "render pass is empty"; return false; }
    if (!pass(context, error)) return false;
  }
  return true;
}
}  // namespace pixcake::gpu
