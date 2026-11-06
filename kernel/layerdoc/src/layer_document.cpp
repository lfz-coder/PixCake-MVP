#include "pixcake/layerdoc/layer_document.hpp"

#include "pixcake/persist/atomic_file.hpp"
#include "pixcake/persist/write_lock.hpp"
#include "pixcake/support/checksum.hpp"

#include <algorithm>
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

constexpr std::uint32_t kLayerSource = 1;
constexpr std::uint32_t kLayerRevision = 2;
constexpr std::uint32_t kLayerBatch = 3;
constexpr std::uint32_t kLayerSelection = 4;
constexpr std::uint32_t kLayerNode = 5;
constexpr std::uint32_t kLayerGeneration = 6;
constexpr std::uint32_t kLayerCurrent = 7;
constexpr std::uint32_t kLayerRecovered = 8;
constexpr std::uint32_t kLayerDocumentRevision = 9;
constexpr std::uint32_t kLayerFirstUnknown = 10;
constexpr std::uint32_t kLayerFormat = 1;

void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xffu));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
  out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffu));
  out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffu));
}

void append_u64(std::vector<std::uint8_t>& out, std::uint64_t value) {
  for (int shift = 0; shift < 64; shift += 8) {
    out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
  }
}

bool read_u32(const std::vector<std::uint8_t>& in, std::size_t offset, std::uint32_t& out) {
  if (offset > in.size() || in.size() - offset < 4) {
    return false;
  }
  out = static_cast<std::uint32_t>(in[offset]) | (static_cast<std::uint32_t>(in[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(in[offset + 2]) << 16) | (static_cast<std::uint32_t>(in[offset + 3]) << 24);
  return true;
}

bool read_u64(const std::vector<std::uint8_t>& in, std::size_t offset, std::uint64_t& out) {
  if (offset > in.size() || in.size() - offset < 8) {
    return false;
  }
  out = 0;
  for (int shift = 0; shift < 64; shift += 8) {
    out |= static_cast<std::uint64_t>(in[offset + static_cast<std::size_t>(shift / 8)]) << shift;
  }
  return true;
}

void append_string(std::vector<std::uint8_t>& out, const std::string& text) {
  append_u32(out, static_cast<std::uint32_t>(text.size()));
  out.insert(out.end(), text.begin(), text.end());
}

bool read_string(const std::vector<std::uint8_t>& in, std::size_t& offset, std::string& out) {
  std::uint32_t size = 0;
  if (!read_u32(in, offset, size) || size > in.size() - (offset + 4)) {
    return false;
  }
  out.assign(in.begin() + static_cast<std::ptrdiff_t>(offset + 4),
             in.begin() + static_cast<std::ptrdiff_t>(offset + 4 + size));
  offset += 4u + size;
  return true;
}

void append_record(std::vector<std::uint8_t>& body, std::uint32_t type, const std::vector<std::uint8_t>& payload) {
  append_u32(body, type);
  append_u32(body, static_cast<std::uint32_t>(payload.size()));
  body.insert(body.end(), payload.begin(), payload.end());
}

std::uint32_t layer_flags(const LayerView& layer) {
  std::uint32_t flags = 0;
  if (layer.group) {
    flags |= 1u;
  }
  if (layer.locked) {
    flags |= 2u;
  }
  if (layer.hidden) {
    flags |= 4u;
  }
  if (layer.background) {
    flags |= 8u;
  }
  if (layer.mask) {
    flags |= 16u;
  }
  if (layer.position_editable) {
    flags |= 32u;
  }
  return flags;
}

bool known_shape(const std::string& shape) {
  return shape == "新图层" || shape == "替换当前生成图层" || shape == "细节层加效果层" || shape == "图层组" ||
         shape == "可编辑蒙版的图层" || shape == "可改位置和大小的图层";
}

bool decode_layer(const std::vector<std::uint8_t>& payload, LayerView& layer) {
  std::size_t offset = 0;
  if (!read_string(payload, offset, layer.id) || !read_string(payload, offset, layer.name) ||
      !read_string(payload, offset, layer.parent_id) || !read_string(payload, offset, layer.blend)) {
    return false;
  }
  std::uint32_t opacity = 0;
  std::uint32_t flags = 0;
  std::uint32_t density = 0;
  std::uint32_t feather = 0;
  if (!read_u32(payload, offset, opacity) || !read_u32(payload, offset + 4, flags) ||
      !read_u32(payload, offset + 8, density) || !read_u32(payload, offset + 12, feather)) {
    return false;
  }
  offset += 16;
  if (offset != payload.size() || !safe_id(layer.id) || !safe_name(layer.name) || !known_blend(layer.blend)) {
    return false;
  }
  if (!layer.parent_id.empty() && !safe_id(layer.parent_id)) {
    return false;
  }
  if (opacity > 100) {
    return false;
  }
  layer.opacity = static_cast<int>(opacity);
  layer.mask_density = static_cast<int>(density);
  // PXLD v1 wrote feather values above the current editor range.  The field
  // is understood, so migrate it to the nearest supported value while
  // keeping the rest of the document readable.
  layer.mask_feather = static_cast<int>(std::min<std::uint32_t>(feather, 100u));
  layer.group = (flags & 1u) != 0;
  layer.locked = (flags & 2u) != 0;
  layer.hidden = (flags & 4u) != 0;
  layer.background = (flags & 8u) != 0;
  layer.mask = (flags & 16u) != 0;
  layer.position_editable = (flags & 32u) != 0;
  return true;
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
  for (const std::string& gone : doomed) {
    if (current_generation_layer_id_ == gone) {
      current_generation_layer_id_.clear();
      break;
    }
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
  if (!editable() || feather < 0 || feather > 100) {
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
    if (current != nullptr) {
      if (current->background || current->locked) {
        return ResultCode::StateConflict;
      }
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

std::vector<std::uint8_t> LayerDocument::encode() const {
  std::vector<std::uint8_t> body;
  if (!recovered_empty_ || !source_image_id_.empty()) {
    std::vector<std::uint8_t> source;
    append_string(source, source_image_id_);
    append_record(body, kLayerSource, source);
  }
  std::vector<std::uint8_t> revision;
  append_u64(revision, source_revision_);
  append_record(body, kLayerRevision, revision);
  std::vector<std::uint8_t> batch;
  append_u32(batch, batch_design_ ? 1u : 0u);
  append_record(body, kLayerBatch, batch);
  std::vector<std::uint8_t> selection;
  append_u32(selection, selection_.present ? 1u : 0u);
  append_u32(selection, selection_.inverted ? 1u : 0u);
  append_u32(selection, static_cast<std::uint32_t>(selection_.feather));
  append_record(body, kLayerSelection, selection);
  for (const auto& layer : layers_) {
    std::vector<std::uint8_t> payload;
    append_string(payload, layer.id);
    append_string(payload, layer.name);
    append_string(payload, layer.parent_id);
    append_string(payload, layer.blend);
    append_u32(payload, static_cast<std::uint32_t>(layer.opacity));
    append_u32(payload, layer_flags(layer));
    append_u32(payload, static_cast<std::uint32_t>(layer.mask_density));
    append_u32(payload, static_cast<std::uint32_t>(layer.mask_feather));
    append_record(body, kLayerNode, payload);
  }
  for (const auto& generation : generations_) {
    std::vector<std::uint8_t> payload;
    append_string(payload, generation.result_id);
    append_string(payload, generation.shape);
    append_string(payload, generation.tool_id);
    append_record(body, kLayerGeneration, payload);
  }
  std::vector<std::uint8_t> current;
  append_string(current, current_generation_layer_id_);
  append_record(body, kLayerCurrent, current);
  if (recovered_empty_) {
    append_record(body, kLayerRecovered, {1});
  }
  std::vector<std::uint8_t> document_revision;
  append_u64(document_revision, document_revision_);
  append_record(body, kLayerDocumentRevision, document_revision);
  for (const auto& unknown : unknown_) {
    append_record(body, unknown.first, unknown.second);
  }

  std::vector<std::uint8_t> out;
  out.push_back('P');
  out.push_back('X');
  out.push_back('L');
  out.push_back('D');
  append_u32(out, kLayerFormat);
  append_u64(out, body.size());
  out.insert(out.end(), body.begin(), body.end());
  append_u64(out, checksum_fnv1a64(body.data(), body.size()));
  return out;
}

std::optional<LayerDocument> LayerDocument::decode(const std::vector<std::uint8_t>& bytes) {
  if (bytes.size() < 24 || bytes[0] != 'P' || bytes[1] != 'X' || bytes[2] != 'L' || bytes[3] != 'D') {
    return std::nullopt;
  }
  std::uint32_t format = 0;
  std::uint64_t body_size = 0;
  if (!read_u32(bytes, 4, format) || !read_u64(bytes, 8, body_size) || format != kLayerFormat) {
    return std::nullopt;
  }
  if (body_size > bytes.size() - 24 || 16 + body_size + 8 != bytes.size()) {
    return std::nullopt;
  }
  const std::vector<std::uint8_t> body(bytes.begin() + 16, bytes.begin() + 16 + static_cast<std::ptrdiff_t>(body_size));
  std::uint64_t stored = 0;
  if (!read_u64(bytes, 16 + static_cast<std::size_t>(body_size), stored) ||
      stored != checksum_fnv1a64(body.data(), body.size())) {
    return std::nullopt;
  }

  LayerDocument document;
  bool saw_source = false;
  bool saw_revision = false;
  std::size_t offset = 0;
  while (offset < body.size()) {
    std::uint32_t type = 0;
    std::uint32_t size = 0;
    if (!read_u32(body, offset, type) || !read_u32(body, offset + 4, size) || size > body.size() - (offset + 8)) {
      return std::nullopt;
    }
    const std::vector<std::uint8_t> payload(body.begin() + static_cast<std::ptrdiff_t>(offset + 8),
                                            body.begin() + static_cast<std::ptrdiff_t>(offset + 8 + size));
    offset += 8u + size;
    if (type == kLayerSource) {
      std::size_t cursor = 0;
      if (!read_string(payload, cursor, document.source_image_id_) || cursor != payload.size() ||
          !safe_id(document.source_image_id_)) {
        return std::nullopt;
      }
      saw_source = true;
    } else if (type == kLayerRevision) {
      if (size != 8 || !read_u64(payload, 0, document.source_revision_)) {
        return std::nullopt;
      }
      saw_revision = true;
    } else if (type == kLayerBatch) {
      std::uint32_t on = 0;
      if (size != 4 || !read_u32(payload, 0, on) || on > 1) {
        return std::nullopt;
      }
      document.batch_design_ = on == 1;
    } else if (type == kLayerSelection) {
      std::uint32_t present = 0;
      std::uint32_t inverted = 0;
      std::uint32_t feather = 0;
      if (size != 12 || !read_u32(payload, 0, present) || !read_u32(payload, 4, inverted) ||
          !read_u32(payload, 8, feather) || present > 1 || inverted > 1 || feather > 1000) {
        return std::nullopt;
      }
      document.selection_.present = present == 1;
      document.selection_.inverted = inverted == 1;
      document.selection_.feather = static_cast<int>(feather);
    } else if (type == kLayerNode) {
      LayerView layer;
      if (!decode_layer(payload, layer)) {
        return std::nullopt;
      }
      for (const auto& existing : document.layers_) {
        if (existing.id == layer.id) {
          return std::nullopt;
        }
      }
      document.layers_.push_back(std::move(layer));
    } else if (type == kLayerGeneration) {
      std::size_t cursor = 0;
      GenerationView generation;
      if (!read_string(payload, cursor, generation.result_id) || !read_string(payload, cursor, generation.shape) ||
          !read_string(payload, cursor, generation.tool_id) || cursor != payload.size() ||
          !safe_id(generation.result_id) || !known_shape(generation.shape) || !safe_name(generation.tool_id)) {
        return std::nullopt;
      }
      for (const auto& existing : document.generations_) {
        if (existing.result_id == generation.result_id) {
          return std::nullopt;
        }
      }
      document.generations_.push_back(std::move(generation));
    } else if (type == kLayerCurrent) {
      std::size_t cursor = 0;
      if (!read_string(payload, cursor, document.current_generation_layer_id_) || cursor != payload.size()) {
        return std::nullopt;
      }
      if (!document.current_generation_layer_id_.empty() && !safe_id(document.current_generation_layer_id_)) {
        return std::nullopt;
      }
    } else if (type == kLayerRecovered) {
      if (size != 1 || payload[0] != 1) {
        return std::nullopt;
      }
      document.recovered_empty_ = true;
    } else if (type == kLayerDocumentRevision) {
      if (size != 8 || !read_u64(payload, 0, document.document_revision_)) {
        return std::nullopt;
      }
    } else if (type >= kLayerFirstUnknown) {
      document.unknown_.push_back({type, payload});
    } else {
      return std::nullopt;
    }
  }
  if (!document.recovered_empty_ && (!saw_source || !saw_revision)) {
    return std::nullopt;
  }
  for (const auto& layer : document.layers_) {
    if (layer.parent_id.empty()) {
      continue;
    }
    const LayerView* parent = document.find(layer.parent_id);
    if (parent == nullptr || !parent->group || document.is_descendant(layer.id, layer.parent_id)) {
      return std::nullopt;
    }
  }
  return document;
}

LoadResult LayerDocument::load(LayerDocument& destination, FileSystem& files, const std::string& path) {
  LoadResult result;
  const DurableLoad opened = open_durable_file(files, path, [](const std::vector<std::uint8_t>& bytes) {
    return decode(bytes).has_value();
  });
  if (opened.absent) {
    result.absent = true;
    return result;
  }
  if (opened.recovered_empty || opened.code != ResultCode::Ok) {
    if (opened.recovered_empty || opened.code == ResultCode::DocumentCorrupt) {
      destination = LayerDocument{};
      destination.recovered_empty_ = true;
      result.code = ResultCode::DocumentCorrupt;
      result.recovered_empty = true;
      return result;
    }
    result.code = opened.code;
    return result;
  }
  auto decoded = decode(opened.bytes);
  if (!decoded) {
    destination = LayerDocument{};
    destination.recovered_empty_ = true;
    result.code = ResultCode::DocumentCorrupt;
    result.recovered_empty = true;
    return result;
  }
  destination = std::move(*decoded);
  result.code = ResultCode::Ok;
  result.used_backup = opened.used_backup;
  return result;
}

std::uint64_t LayerDocument::document_revision() const { return document_revision_; }

ResultCode LayerDocument::save(FileSystem& files, const std::string& path, const FastExportLatch* latch) {
  WriteScope scope(files, path, latch);
  if (!scope) {
    return scope.code();
  }
  const ResultCode durable = check_durable_revision(
      files, path, document_revision_, [](const std::vector<std::uint8_t>& bytes) -> std::optional<std::uint64_t> {
        const auto decoded = decode(bytes);
        if (!decoded) return std::nullopt;
        return decoded->document_revision();
      });
  if (durable != ResultCode::Ok) {
    return durable;
  }
  LayerDocument next = *this;
  next.document_revision_ = document_revision_ + 1;
  const ResultCode saved = atomic_replace(files, path, next.encode());
  if (saved != ResultCode::Ok) {
    return saved;
  }
  document_revision_ = next.document_revision_;
  return ResultCode::Ok;
}

ResultCode LayerDocument::append_unknown_record(std::uint32_t type, std::vector<std::uint8_t> payload) {
  if (type < kLayerFirstUnknown) {
    return ResultCode::StateConflict;
  }
  unknown_.push_back({type, std::move(payload)});
  return ResultCode::Ok;
}

std::vector<std::pair<std::uint32_t, std::vector<std::uint8_t>>> LayerDocument::unknown_records() const {
  return unknown_;
}

ResultCode apply_to_original(const LayerDocument& layers, EditDocument& document, std::uint64_t expected_revision,
                             FileSystem& files, const std::string& path) {
  if (layers.recovered_empty() || layers.source_image_id() != document.image_id()) {
    return ResultCode::StateConflict;
  }
  return document.commit_baked_reset(expected_revision, files, path);
}

std::optional<EditDocument> applied_copy_document(const std::string& new_image_id) {
  return EditDocument::create(new_image_id);
}

ResultCode tsq_sibling_path(const std::string& creative_directory, const std::string& new_image_id, std::string& out) {
  if (creative_directory.size() < 2 || creative_directory.front() != '/' || creative_directory.back() == '/' ||
      creative_directory.find('\0') != std::string::npos || !safe_id(new_image_id)) {
    return ResultCode::StateConflict;
  }
  out = creative_directory + "/" + new_image_id + ".tsq";
  return ResultCode::Ok;
}

}  // namespace pixcake
