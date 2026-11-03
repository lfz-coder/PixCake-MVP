#include "pixcake/layerdoc/layer_document.hpp"

#include <utility>

namespace pixcake {
namespace {

bool has_nul(const std::string& text) { return text.find('\0') != std::string::npos; }

bool safe_id(const std::string& id) {
  return !id.empty() && id != "." && id != ".." && !has_nul(id) && id.find('/') == std::string::npos &&
         id.find('\\') == std::string::npos;
}

bool safe_name(const std::string& name) { return !name.empty() && !has_nul(name); }

bool known_blend(const std::string& blend) {
  return blend == "正常" || blend == "正片叠底" || blend == "滤色" || blend == "变亮" || blend == "变暗";
}

LayerView make_layer(std::string id, std::string name) {
  LayerView layer;
  layer.id = std::move(id);
  layer.name = std::move(name);
  return layer;
}

}  // namespace

std::optional<LayerDocument> LayerDocument::create(std::string source_image_id, std::uint64_t source_revision) {
  if (!safe_id(source_image_id)) {
    return std::nullopt;
  }
  LayerDocument document;
  document.source_image_id_ = std::move(source_image_id);
  document.source_revision_ = source_revision;
  LayerView background = make_layer("background", "背景");
  background.background = true;
  document.layers_.push_back(std::move(background));
  return document;
}

std::string LayerDocument::source_image_id() const { return source_image_id_; }
std::uint64_t LayerDocument::source_revision() const { return source_revision_; }
bool LayerDocument::batch_design() const { return batch_design_; }
bool LayerDocument::recovered_empty() const { return recovered_empty_; }
SelectionView LayerDocument::selection() const { return selection_; }
std::vector<LayerView> LayerDocument::layers() const { return layers_; }
std::vector<GenerationView> LayerDocument::generations() const { return generations_; }

bool LayerDocument::find_layer(const std::string& id, LayerView& out) const {
  const LayerView* found = find(id);
  if (found == nullptr) {
    return false;
  }
  out = *found;
  return true;
}

LayerView* LayerDocument::find(const std::string& id) {
  for (auto& layer : layers_) {
    if (layer.id == id) {
      return &layer;
    }
  }
  return nullptr;
}

const LayerView* LayerDocument::find(const std::string& id) const {
  for (const auto& layer : layers_) {
    if (layer.id == id) {
      return &layer;
    }
  }
  return nullptr;
}

bool LayerDocument::editable() const { return !batch_design_ && !recovered_empty_; }

bool LayerDocument::parent_ok(const std::string& parent_id) const {
  if (parent_id.empty()) {
    return true;
  }
  const LayerView* parent = find(parent_id);
  return parent != nullptr && parent->group;
}

bool LayerDocument::is_descendant(const std::string& id, const std::string& maybe_child) const {
  std::string walk = maybe_child;
  for (std::size_t hop = 0; hop < layers_.size() + 1 && !walk.empty(); ++hop) {
    if (walk == id) {
      return true;
    }
    const LayerView* node = find(walk);
    if (node == nullptr) {
      return false;
    }
    walk = node->parent_id;
  }
  return !walk.empty();
}

std::vector<std::string> LayerDocument::child_ids(const std::string& parent_id) const {
  std::vector<std::string> ids;
  for (const auto& layer : layers_) {
    if (layer.parent_id == parent_id) {
      ids.push_back(layer.id);
    }
  }
  return ids;
}

ResultCode LayerDocument::set_batch_design(bool on) {
  if (recovered_empty_) {
    return ResultCode::StateConflict;
  }
  batch_design_ = on;
  return ResultCode::Ok;
}

ResultCode LayerDocument::rename(const std::string& id, std::string name) {
  if (!editable() || !safe_name(name)) {
    return ResultCode::StateConflict;
  }
  LayerView* layer = find(id);
  if (layer == nullptr || layer->locked) {
    return ResultCode::StateConflict;
  }
  layer->name = std::move(name);
  return ResultCode::Ok;
}

ResultCode LayerDocument::set_blend(const std::string& id, const std::string& blend) {
  if (!editable() || !known_blend(blend)) {
    return ResultCode::StateConflict;
  }
  LayerView* layer = find(id);
  if (layer == nullptr || layer->locked) {
    return ResultCode::StateConflict;
  }
  layer->blend = blend;
  return ResultCode::Ok;
}

ResultCode LayerDocument::set_opacity(const std::string& id, int opacity) {
  if (!editable() || opacity < 0 || opacity > 100) {
    return ResultCode::StateConflict;
  }
  LayerView* layer = find(id);
  if (layer == nullptr || layer->locked) {
    return ResultCode::StateConflict;
  }
  layer->opacity = opacity;
  return ResultCode::Ok;
}

ResultCode LayerDocument::set_locked(const std::string& id, bool locked) {
  if (!editable()) {
    return ResultCode::StateConflict;
  }
  LayerView* layer = find(id);
  if (layer == nullptr) {
    return ResultCode::StateConflict;
  }
  layer->locked = locked;
  return ResultCode::Ok;
}

ResultCode LayerDocument::set_hidden(const std::string& id, bool hidden) {
  if (!editable()) {
    return ResultCode::StateConflict;
  }
  LayerView* layer = find(id);
  if (layer == nullptr || layer->locked) {
    return ResultCode::StateConflict;
  }
  layer->hidden = hidden;
  return ResultCode::Ok;
}

ResultCode LayerDocument::add_layer(const std::string& id, std::string name) {
  if (!editable() || !safe_id(id) || id == "background" || !safe_name(name) || find(id) != nullptr) {
    return ResultCode::StateConflict;
  }
  layers_.push_back(make_layer(id, std::move(name)));
  return ResultCode::Ok;
}

ResultCode LayerDocument::add_group(const std::string& id, std::string name, const std::string& parent_id) {
  if (!editable() || !safe_id(id) || !safe_name(name) || find(id) != nullptr || !parent_ok(parent_id)) {
    return ResultCode::StateConflict;
  }
  LayerView group = make_layer(id, std::move(name));
  group.group = true;
  group.parent_id = parent_id;
  layers_.push_back(std::move(group));
  return ResultCode::Ok;
}

ResultCode LayerDocument::group_layer(const std::string& layer_id, const std::string& group_id,
                                      std::string group_name) {
  if (!editable() || !safe_id(group_id) || !safe_name(group_name) || find(group_id) != nullptr) {
    return ResultCode::StateConflict;
  }
  LayerView* layer = find(layer_id);
  if (layer == nullptr || layer->background || layer->locked) {
    return ResultCode::StateConflict;
  }
  const std::string parent = layer->parent_id;
  LayerView group = make_layer(group_id, std::move(group_name));
  group.group = true;
  group.parent_id = parent;
  layers_.push_back(std::move(group));
  find(layer_id)->parent_id = group_id;
  return ResultCode::Ok;
}

ResultCode LayerDocument::remove(const std::string& id) {
  if (!editable()) {
    return ResultCode::StateConflict;
  }
  const LayerView* root = find(id);
  if (root == nullptr || root->background || root->locked) {
    return ResultCode::StateConflict;
  }
  std::vector<std::string> doomed;
  doomed.push_back(id);
  for (std::size_t index = 0; index < doomed.size(); ++index) {
    for (const auto& layer : layers_) {
      if (layer.parent_id != doomed[index]) {
        continue;
      }
      bool seen = false;
      for (const auto& gone : doomed) {
        if (gone == layer.id) {
          seen = true;
        }
      }
      if (seen) {
        continue;
      }
      if (layer.background || layer.locked) {
        return ResultCode::StateConflict;
      }
      doomed.push_back(layer.id);
    }
  }
  std::vector<LayerView> kept;
  for (const auto& layer : layers_) {
    bool drop = false;
    for (const auto& gone : doomed) {
      if (layer.id == gone) {
        drop = true;
      }
    }
    if (!drop) {
      kept.push_back(layer);
    }
  }
  layers_ = std::move(kept);
  if (current_generation_layer_id_ == id) {
    current_generation_layer_id_.clear();
  }
  return ResultCode::Ok;
}

ResultCode LayerDocument::remove_child(const std::string& parent_id, int index) {
  if (!parent_ok(parent_id)) {
    return ResultCode::StateConflict;
  }
  const std::vector<std::string> children = child_ids(parent_id);
  if (index < 0 || static_cast<std::size_t>(index) >= children.size()) {
    return ResultCode::StateConflict;
  }
  return remove(children[static_cast<std::size_t>(index)]);
}

ResultCode LayerDocument::move_child(const std::string& id, const std::string& parent_id, int index) {
  if (!editable() || !parent_ok(parent_id)) {
    return ResultCode::StateConflict;
  }
  LayerView* node = find(id);
  if (node == nullptr || node->background || node->locked || is_descendant(id, parent_id)) {
    return ResultCode::StateConflict;
  }
  std::vector<std::string> siblings;
  for (const auto& layer : layers_) {
    if (layer.parent_id == parent_id && layer.id != id) {
      siblings.push_back(layer.id);
    }
  }
  if (index < 0 || static_cast<std::size_t>(index) > siblings.size()) {
    return ResultCode::StateConflict;
  }
  LayerView moved = *node;
  moved.parent_id = parent_id;
  layers_.erase(layers_.begin() + static_cast<std::ptrdiff_t>(node - layers_.data()));
  std::size_t at = layers_.size();
  if (static_cast<std::size_t>(index) < siblings.size()) {
    for (std::size_t position = 0; position < layers_.size(); ++position) {
      if (layers_[position].id == siblings[static_cast<std::size_t>(index)]) {
        at = position;
        break;
      }
    }
  }
  layers_.insert(layers_.begin() + static_cast<std::ptrdiff_t>(at), std::move(moved));
  return ResultCode::Ok;
}

ResultCode LayerDocument::add_mask(const std::string& id) {
  if (!editable()) {
    return ResultCode::StateConflict;
  }
  LayerView* layer = find(id);
  if (layer == nullptr || layer->group || layer->locked) {
    return ResultCode::StateConflict;
  }
  layer->mask = true;
  return ResultCode::Ok;
}

ResultCode LayerDocument::remove_mask(const std::string& id) {
  if (!editable()) {
    return ResultCode::StateConflict;
  }
  LayerView* layer = find(id);
  if (layer == nullptr || !layer->mask || layer->locked) {
    return ResultCode::StateConflict;
  }
  layer->mask = false;
  layer->mask_density = 100;
  layer->mask_feather = 0;
  return ResultCode::Ok;
}

ResultCode LayerDocument::set_mask_density(const std::string& id, int density) {
  if (!editable() || density < 0) {
    return ResultCode::StateConflict;
  }
  LayerView* layer = find(id);
  if (layer == nullptr || !layer->mask || layer->locked) {
    return ResultCode::StateConflict;
  }
  layer->mask_density = density;
  return ResultCode::Ok;
}

ResultCode LayerDocument::set_mask_feather(const std::string& id, int feather) {
  if (!editable() || feather < 0) {
    return ResultCode::StateConflict;
  }
  LayerView* layer = find(id);
  if (layer == nullptr || !layer->mask || layer->locked) {
    return ResultCode::StateConflict;
  }
  layer->mask_feather = feather;
  return ResultCode::Ok;
}

ResultCode LayerDocument::set_selection(bool inverted, int feather) {
  if (!editable() || feather < 0 || feather > 1000) {
    return ResultCode::StateConflict;
  }
  selection_.present = true;
  selection_.inverted = inverted;
  selection_.feather = feather;
  return ResultCode::Ok;
}

ResultCode LayerDocument::clear_selection() {
  if (!editable()) {
    return ResultCode::StateConflict;
  }
  selection_ = SelectionView{};
  return ResultCode::Ok;
}

ResultCode LayerDocument::place_result(const std::string& result_id, const std::string& shape,
                                       const std::string& tool_id) {
  if (!editable() || !safe_id(result_id) || !safe_name(tool_id)) {
    return ResultCode::StateConflict;
  }
  for (const auto& generation : generations_) {
    if (generation.result_id == result_id) {
      return ResultCode::Ok;
    }
  }
  const bool known = shape == "新图层" || shape == "替换当前生成图层" || shape == "细节层加效果层" ||
                     shape == "图层组" || shape == "可编辑蒙版的图层" || shape == "可改位置和大小的图层";
  if (!known) {
    return ResultCode::StateConflict;
  }
  if (shape == "替换当前生成图层") {
    LayerView* current = find(current_generation_layer_id_);
    if (current != nullptr && !current->background && !current->locked) {
      current->name = tool_id;
      generations_.push_back(GenerationView{result_id, shape, tool_id});
      return ResultCode::Ok;
    }
  }
  auto adopt = [&](LayerView layer) {
    current_generation_layer_id_ = layer.id;
    layers_.push_back(std::move(layer));
  };
  if (shape == "细节层加效果层") {
    if (find(result_id + "#detail") != nullptr || find(result_id + "#effect") != nullptr) {
      return ResultCode::StateConflict;
    }
    layers_.push_back(make_layer(result_id + "#detail", "细节"));
    LayerView effect = make_layer(result_id + "#effect", "效果");
    adopt(std::move(effect));
  } else if (shape == "图层组") {
    if (find(result_id) != nullptr) {
      return ResultCode::StateConflict;
    }
    LayerView group = make_layer(result_id, tool_id);
    group.group = true;
    adopt(std::move(group));
  } else {
    if (find(result_id) != nullptr) {
      return ResultCode::StateConflict;
    }
    LayerView layer = make_layer(result_id, tool_id);
    if (shape == "可编辑蒙版的图层") {
      layer.mask = true;
    }
    if (shape == "可改位置和大小的图层") {
      layer.position_editable = true;
    }
    adopt(std::move(layer));
  }
  generations_.push_back(GenerationView{result_id, shape, tool_id});
  return ResultCode::Ok;
}

}  // namespace pixcake
