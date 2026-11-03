#pragma once

#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pixcake {

struct LayerView {
  std::string id;
  std::string name;
  std::string parent_id;
  bool group = false;
  bool locked = false;
  bool hidden = false;
  bool background = false;
  bool mask = false;
  bool position_editable = false;
  std::string blend = "正常";
  int opacity = 100;
  int mask_density = 100;
  int mask_feather = 0;
};

struct SelectionView {
  bool present = false;
  bool inverted = false;
  int feather = 0;
};

struct GenerationView {
  std::string result_id;
  std::string shape;
  std::string tool_id;
};

// 图层文档不保存精修滑杆，也不保存像素。
class LayerDocument {
 public:
  static std::optional<LayerDocument> create(std::string source_image_id, std::uint64_t source_revision);

  std::string source_image_id() const;
  std::uint64_t source_revision() const;
  bool batch_design() const;
  bool recovered_empty() const;
  SelectionView selection() const;
  std::vector<LayerView> layers() const;
  std::vector<GenerationView> generations() const;
  bool find_layer(const std::string& id, LayerView& out) const;

  ResultCode set_batch_design(bool on);
  ResultCode rename(const std::string& id, std::string name);
  ResultCode set_blend(const std::string& id, const std::string& blend);
  ResultCode set_opacity(const std::string& id, int opacity);
  ResultCode set_locked(const std::string& id, bool locked);
  ResultCode set_hidden(const std::string& id, bool hidden);
  ResultCode add_layer(const std::string& id, std::string name);
  ResultCode add_group(const std::string& id, std::string name, const std::string& parent_id);
  ResultCode group_layer(const std::string& layer_id, const std::string& group_id, std::string group_name);
  ResultCode remove(const std::string& id);
  ResultCode remove_child(const std::string& parent_id, int index);
  ResultCode move_child(const std::string& id, const std::string& parent_id, int index);
  ResultCode add_mask(const std::string& id);
  ResultCode remove_mask(const std::string& id);
  ResultCode set_mask_density(const std::string& id, int density);
  ResultCode set_mask_feather(const std::string& id, int feather);
  ResultCode set_selection(bool inverted, int feather);
  ResultCode clear_selection();
  ResultCode place_result(const std::string& result_id, const std::string& shape, const std::string& tool_id);

 private:
  LayerView* find(const std::string& id);
  const LayerView* find(const std::string& id) const;
  bool editable() const;
  bool parent_ok(const std::string& parent_id) const;
  bool is_descendant(const std::string& id, const std::string& maybe_child) const;
  std::vector<std::string> child_ids(const std::string& parent_id) const;

  std::string source_image_id_;
  std::uint64_t source_revision_ = 0;
  bool batch_design_ = false;
  bool recovered_empty_ = false;
  SelectionView selection_;
  std::vector<LayerView> layers_;
  std::vector<GenerationView> generations_;
  std::string current_generation_layer_id_;
};

}  // namespace pixcake
