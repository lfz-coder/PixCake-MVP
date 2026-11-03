#include "pixcake/layerdoc/layer_document.hpp"

#include <cstdio>
#include <string>

namespace {

int g_failures = 0;

void expect(bool cond, const char* text, int line) {
  if (!cond) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, text);
    ++g_failures;
  }
}

#define EXPECT(cond) expect(static_cast<bool>(cond), #cond, __LINE__)

pixcake::LayerDocument opened() {
  auto document = pixcake::LayerDocument::create("img-1", 4);
  EXPECT(document.has_value());
  return std::move(*document);
}

void test_entry_does_not_copy_sliders_and_keeps_background() {
  EXPECT(!pixcake::LayerDocument::create("", 1).has_value());
  auto document = opened();
  EXPECT(document.source_image_id() == "img-1");
  EXPECT(document.source_revision() == 4);
  EXPECT(!document.batch_design());
  EXPECT(document.layers().size() == 1);
  EXPECT(document.layers()[0].id == "background");
  EXPECT(document.layers()[0].name == "背景");
  EXPECT(document.layers()[0].background);
  EXPECT(document.layers()[0].blend == "正常");
  EXPECT(document.layers()[0].opacity == 100);
  EXPECT(!document.layers()[0].mask);
  EXPECT(document.generations().empty());
}

void test_blend_opacity_lock_and_index() {
  auto document = opened();
  EXPECT(document.set_blend("background", "滤色") == pixcake::ResultCode::Ok);
  EXPECT(document.set_blend("background", "叠加") == pixcake::ResultCode::StateConflict);
  pixcake::LayerView background;
  EXPECT(document.find_layer("background", background));
  EXPECT(background.blend == "滤色");
  EXPECT(document.set_opacity("background", 0) == pixcake::ResultCode::Ok);
  EXPECT(document.set_opacity("background", 101) == pixcake::ResultCode::StateConflict);
  EXPECT(document.set_opacity("background", -1) == pixcake::ResultCode::StateConflict);
  EXPECT(document.find_layer("background", background));
  EXPECT(background.opacity == 0);
  EXPECT(document.set_locked("background", true) == pixcake::ResultCode::Ok);
  EXPECT(document.rename("background", "底板") == pixcake::ResultCode::StateConflict);
  EXPECT(document.set_locked("background", false) == pixcake::ResultCode::Ok);
  EXPECT(document.rename("background", "底板") == pixcake::ResultCode::Ok);
  EXPECT(document.remove_child("", 1) == pixcake::ResultCode::StateConflict);
  EXPECT(document.layers().size() == 1);
  EXPECT(document.remove("background") == pixcake::ResultCode::StateConflict);
}

void test_group_mask_selection_and_batch_design() {
  auto document = opened();
  EXPECT(document.group_layer("background", "group-1", "组") == pixcake::ResultCode::StateConflict);
  EXPECT(document.add_layer("layer-1", "人物") == pixcake::ResultCode::Ok);
  EXPECT(document.group_layer("layer-1", "group-1", "组") == pixcake::ResultCode::Ok);
  pixcake::LayerView layer;
  EXPECT(document.find_layer("layer-1", layer));
  EXPECT(layer.parent_id == "group-1");
  pixcake::LayerView group;
  EXPECT(document.find_layer("group-1", group));
  EXPECT(group.group);
  EXPECT(document.add_mask("layer-1") == pixcake::ResultCode::Ok);
  EXPECT(document.set_mask_density("layer-1", 40) == pixcake::ResultCode::Ok);
  EXPECT(document.set_mask_density("layer-1", 101) == pixcake::ResultCode::Ok);
  EXPECT(document.set_mask_density("layer-1", -1) == pixcake::ResultCode::StateConflict);
  pixcake::LayerView masked;
  EXPECT(document.find_layer("layer-1", masked));
  EXPECT(masked.mask_density == 101);
  EXPECT(document.set_mask_feather("layer-1", 1001) == pixcake::ResultCode::Ok);
  EXPECT(document.set_mask_feather("layer-1", -1) == pixcake::ResultCode::StateConflict);
  EXPECT(document.find_layer("layer-1", masked));
  EXPECT(masked.mask_feather == 1001);
  EXPECT(document.set_selection(true, 12) == pixcake::ResultCode::Ok);
  EXPECT(document.selection().present);
  EXPECT(document.selection().feather == 12);
  EXPECT(document.set_selection(false, 1001) == pixcake::ResultCode::StateConflict);
  EXPECT(document.selection().feather == 12);

  EXPECT(document.set_batch_design(true) == pixcake::ResultCode::Ok);
  EXPECT(document.add_layer("layer-2", "后来的") == pixcake::ResultCode::StateConflict);
  EXPECT(document.place_result("result-1", "新图层", "tool-a") == pixcake::ResultCode::StateConflict);
  EXPECT(document.layers().size() == 3);
  EXPECT(document.set_batch_design(false) == pixcake::ResultCode::Ok);
  EXPECT(document.add_layer("layer-2", "后来的") == pixcake::ResultCode::Ok);
}

void test_place_result_shapes_once() {
  auto document = opened();
  EXPECT(document.place_result("result-1", "没有这种形状", "tool-a") == pixcake::ResultCode::StateConflict);
  EXPECT(document.generations().empty());
  EXPECT(document.place_result("result-1", "新图层", "tool-a") == pixcake::ResultCode::Ok);
  EXPECT(document.place_result("result-1", "新图层", "tool-a") == pixcake::ResultCode::Ok);
  EXPECT(document.generations().size() == 1);
  int copies = 0;
  for (const auto& layer : document.layers()) {
    if (layer.id == "result-1") {
      ++copies;
    }
  }
  EXPECT(copies == 1);

  EXPECT(document.place_result("result-2", "细节层加效果层", "绘影") == pixcake::ResultCode::Ok);
  pixcake::LayerView detail;
  pixcake::LayerView effect;
  EXPECT(document.find_layer("result-2#detail", detail));
  EXPECT(document.find_layer("result-2#effect", effect));
  EXPECT(detail.name == "细节");
  EXPECT(effect.name == "效果");
  EXPECT(document.place_result("result-2", "细节层加效果层", "绘影") == pixcake::ResultCode::Ok);
  EXPECT(document.generations().size() == 2);

  EXPECT(document.place_result("result-3", "可编辑蒙版的图层", "祛路人") == pixcake::ResultCode::Ok);
  pixcake::LayerView masked;
  EXPECT(document.find_layer("result-3", masked));
  EXPECT(masked.mask);
  EXPECT(document.place_result("result-4", "可改位置和大小的图层", "超清") == pixcake::ResultCode::Ok);
  pixcake::LayerView placed;
  EXPECT(document.find_layer("result-4", placed));
  EXPECT(placed.position_editable);
  EXPECT(document.place_result("result-5", "图层组", "换衣服") == pixcake::ResultCode::Ok);
  pixcake::LayerView folder;
  EXPECT(document.find_layer("result-5", folder));
  EXPECT(folder.group);

  EXPECT(document.place_result("result-6", "替换当前生成图层", "重绘") == pixcake::ResultCode::Ok);
  EXPECT(document.find_layer("result-5", folder));
  EXPECT(folder.name == "重绘");
  EXPECT(!document.find_layer("result-6", placed));
  EXPECT(document.generations().size() == 6);
}

void test_place_result_replace_locked_generation_layer() {
  auto document = opened();
  EXPECT(document.place_result("result-1", "新图层", "tool-a") == pixcake::ResultCode::Ok);
  EXPECT(document.set_locked("result-1", true) == pixcake::ResultCode::Ok);
  EXPECT(document.place_result("result-2", "替换当前生成图层", "重绘") ==
         pixcake::ResultCode::StateConflict);
  pixcake::LayerView layer;
  EXPECT(document.find_layer("result-1", layer));
  EXPECT(layer.name == "tool-a");
  EXPECT(layer.locked);
  pixcake::LayerView missing;
  EXPECT(!document.find_layer("result-2", missing));
  EXPECT(document.generations().size() == 1);
  EXPECT(document.generations()[0].result_id == "result-1");
}

void test_remove_clears_generation_cursor_for_deleted_children() {
  auto document = opened();
  EXPECT(document.place_result("result-1", "新图层", "tool-a") == pixcake::ResultCode::Ok);
  EXPECT(document.group_layer("result-1", "group-1", "組") == pixcake::ResultCode::Ok);
  EXPECT(document.remove("group-1") == pixcake::ResultCode::Ok);
  EXPECT(document.add_layer("result-1", "後來的") == pixcake::ResultCode::Ok);
  EXPECT(document.place_result("result-2", "替换当前生成图层", "重繪") == pixcake::ResultCode::Ok);
  pixcake::LayerView reused;
  EXPECT(document.find_layer("result-1", reused));
  EXPECT(reused.name == "後來的");
  pixcake::LayerView replacement;
  EXPECT(document.find_layer("result-2", replacement));
  EXPECT(replacement.name == "重繪");
}

}  // namespace

int main() {
  test_entry_does_not_copy_sliders_and_keeps_background();
  test_blend_opacity_lock_and_index();
  test_group_mask_selection_and_batch_design();
  test_place_result_shapes_once();
  test_place_result_replace_locked_generation_layer();
  test_remove_clears_generation_cursor_for_deleted_children();
  if (g_failures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  return 0;
}
