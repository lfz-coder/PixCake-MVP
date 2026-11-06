#include "pixcake/editdoc/edit_document.hpp"
#include "pixcake/layerdoc/layer_document.hpp"
#include "pixcake/support/checksum.hpp"
#include "support/fake_file_system.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void expect(bool cond, const char* text, int line) {
  if (!cond) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, text);
    ++g_failures;
  }
}

#define EXPECT(cond) expect(static_cast<bool>(cond), #cond, __LINE__)

class Latch final : public pixcake::FastExportLatch {
 public:
  bool on = false;
  bool enabled() const override { return on; }
};

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
  EXPECT(document.set_mask_feather("layer-1", 100) == pixcake::ResultCode::Ok);
  EXPECT(document.set_mask_feather("layer-1", 101) == pixcake::ResultCode::StateConflict);
  EXPECT(document.set_mask_feather("layer-1", -1) == pixcake::ResultCode::StateConflict);
  EXPECT(document.find_layer("layer-1", masked));
  EXPECT(masked.mask_feather == 100);
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

void test_round_trip_keeps_unknown_records_and_rejects_the_retouch_magic() {
  auto document = opened();
  EXPECT(document.add_layer("layer-1", "人物") == pixcake::ResultCode::Ok);
  EXPECT(document.append_unknown_record(10, {7, 8}) == pixcake::ResultCode::Ok);
  EXPECT(document.append_unknown_record(5, {1}) == pixcake::ResultCode::StateConflict);
  EXPECT(document.append_unknown_record(9, {1}) == pixcake::ResultCode::StateConflict);
  const std::vector<std::uint8_t> bytes = document.encode();
  EXPECT(bytes.size() > 4);
  EXPECT(bytes[0] == 'P' && bytes[1] == 'X' && bytes[2] == 'L' && bytes[3] == 'D');
  auto restored = pixcake::LayerDocument::decode(bytes);
  EXPECT(restored.has_value());
  EXPECT(restored->source_image_id() == "img-1");
  EXPECT(restored->unknown_records().size() == 1);
  EXPECT(restored->unknown_records()[0].second == std::vector<std::uint8_t>({7, 8}));

  auto edit = pixcake::EditDocument::create("img-1");
  EXPECT(edit.has_value());
  EXPECT(!pixcake::LayerDocument::decode(edit->encode()).has_value());

  pixcake::test::FakeFileSystem files;
  EXPECT(document.save(files, "/creative/img-1.pxlayer") == pixcake::ResultCode::Ok);
  pixcake::LayerDocument loaded;
  const pixcake::LoadResult opened_file = pixcake::LayerDocument::load(loaded, files, "/creative/missing.pxlayer");
  EXPECT(opened_file.absent);
  EXPECT(!opened_file.code.has_value());
  const pixcake::LoadResult present = pixcake::LayerDocument::load(loaded, files, "/creative/img-1.pxlayer");
  EXPECT(present.code == pixcake::ResultCode::Ok);
  pixcake::LayerView person;
  EXPECT(loaded.find_layer("layer-1", person));
  files.write_error = pixcake::ResultCode::DiskFull;
  EXPECT(document.save(files, "/creative/img-1.pxlayer") == pixcake::ResultCode::DiskFull);
  files.write_error = pixcake::ResultCode::Ok;
  pixcake::LayerDocument kept;
  const pixcake::LoadResult still = pixcake::LayerDocument::load(kept, files, "/creative/img-1.pxlayer");
  EXPECT(still.code == pixcake::ResultCode::Ok);
  EXPECT(kept.find_layer("layer-1", person));

  files.files["/creative/bad.pxlayer"] = {1, 2, 3, 4};
  const pixcake::LoadResult corrupt = pixcake::LayerDocument::load(loaded, files, "/creative/bad.pxlayer");
  EXPECT(corrupt.recovered_empty);
  EXPECT(corrupt.code == pixcake::ResultCode::DocumentCorrupt);
}

void test_apply_bakes_the_original_and_a_new_document_has_no_pixels() {
  auto layers = opened();
  auto edit = *pixcake::EditDocument::create("img-1");
  edit.store_fragment("color.global", 1, {9});
  edit.add_mask_id("mask-1");
  pixcake::test::FakeFileSystem files;
  EXPECT(edit.write_snapshot(files, "/docs/img.pxedit") == pixcake::ResultCode::Ok);
  const auto untouched = edit.revision();
  EXPECT(layers.source_image_id() == edit.image_id());
  EXPECT(edit.fragments().size() == 1);
  EXPECT(!edit.effects_baked());

  EXPECT(pixcake::apply_to_original(layers, edit, untouched, files, "/docs/img.pxedit") == pixcake::ResultCode::Ok);
  EXPECT(edit.effects_baked());
  EXPECT(edit.fragments().empty());
  EXPECT(edit.mask_ids().size() == 1);
  EXPECT(edit.revision() == untouched + 1);
  EXPECT(pixcake::apply_to_original(layers, edit, untouched, files, "/docs/img.pxedit") ==
         pixcake::ResultCode::StateConflict);
  EXPECT(edit.revision() == untouched + 1);
  EXPECT(pixcake::apply_to_original(layers, edit, edit.revision(), files, "/docs/img.pxedit") ==
         pixcake::ResultCode::Ok);
  EXPECT(edit.revision() == untouched + 1);

  auto other = *pixcake::EditDocument::create("img-2");
  EXPECT(pixcake::apply_to_original(layers, other, other.revision(), files, "/docs/img2.pxedit") ==
         pixcake::ResultCode::StateConflict);
  EXPECT(!other.effects_baked());

  auto copy = pixcake::applied_copy_document("img-new");
  EXPECT(copy.has_value());
  EXPECT(copy->image_id() == "img-new");
  EXPECT(copy->fragments().empty());
  EXPECT(!copy->effects_baked());
  std::string tsq;
  EXPECT(pixcake::tsq_sibling_path("/creative", "img-new", tsq) == pixcake::ResultCode::Ok);
  EXPECT(tsq == "/creative/img-new.tsq");
  EXPECT(files.files.count("/creative/img-new.tsq") == 0);
  EXPECT(pixcake::tsq_sibling_path("creative", "img-new", tsq) == pixcake::ResultCode::StateConflict);
}

void test_recovered_empty_save_round_trips() {
  pixcake::test::FakeFileSystem files;
  files.files["/creative/bad.pxlayer"] = {1, 2, 3, 4};
  pixcake::LayerDocument destination;
  const pixcake::LoadResult corrupt = pixcake::LayerDocument::load(destination, files, "/creative/bad.pxlayer");
  EXPECT(corrupt.recovered_empty);
  EXPECT(corrupt.code == pixcake::ResultCode::DocumentCorrupt);
  EXPECT(destination.recovered_empty());
  EXPECT(destination.source_image_id().empty());
  EXPECT(destination.save(files, "/creative/bad.pxlayer") == pixcake::ResultCode::Ok);
  pixcake::LayerDocument second;
  const pixcake::LoadResult ok = pixcake::LayerDocument::load(second, files, "/creative/bad.pxlayer");
  EXPECT(ok.code == pixcake::ResultCode::Ok);
  EXPECT(second.recovered_empty());
  EXPECT(second.source_image_id().empty());
  EXPECT(second.layers().empty());
}

void test_layer_save_observes_explicit_write_latch() {
  auto document = opened();
  pixcake::test::FakeFileSystem files;
  Latch latch;
  latch.on = true;
  EXPECT(document.save(files, "/creative/img-1.pxlayer", &latch) == pixcake::ResultCode::FastExportBusy);
  EXPECT(files.writes.empty());
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

std::uint64_t read_le(const std::vector<std::uint8_t>& bytes, std::size_t offset, int width) {
  std::uint64_t value = 0;
  for (int index = 0; index < width; ++index) value |= static_cast<std::uint64_t>(bytes[offset + index]) << (index * 8);
  return value;
}

void write_le(std::vector<std::uint8_t>& out, std::uint64_t value, int width) {
  for (int index = 0; index < width; ++index) out.push_back(static_cast<std::uint8_t>(value >> (index * 8)));
}

// 去掉某一类记录并重算校验和，用来构造旧版本写出的图层文件。
std::vector<std::uint8_t> without_record(const std::vector<std::uint8_t>& bytes, std::uint32_t removed) {
  const std::uint64_t body_size = read_le(bytes, 8, 8);
  std::vector<std::uint8_t> body;
  std::size_t offset = 16;
  while (offset < 16 + body_size) {
    const auto type = static_cast<std::uint32_t>(read_le(bytes, offset, 4));
    const auto size = static_cast<std::uint32_t>(read_le(bytes, offset + 4, 4));
    if (type != removed) body.insert(body.end(), bytes.begin() + offset, bytes.begin() + offset + 8 + size);
    offset += 8u + size;
  }
  std::vector<std::uint8_t> out(bytes.begin(), bytes.begin() + 8);
  write_le(out, body.size(), 8);
  out.insert(out.end(), body.begin(), body.end());
  write_le(out, pixcake::checksum_fnv1a64(body.data(), body.size()), 8);
  return out;
}

void test_c23_b73_stale_layer_save_is_rejected() {
  pixcake::test::FakeFileSystem files;
  auto seed = opened();
  EXPECT(seed.save(files, "/creative/race.pxlayer") == pixcake::ResultCode::Ok);
  EXPECT(seed.document_revision() == 1);
  pixcake::LayerDocument first;
  pixcake::LayerDocument second;
  EXPECT(pixcake::LayerDocument::load(first, files, "/creative/race.pxlayer").code == pixcake::ResultCode::Ok);
  EXPECT(pixcake::LayerDocument::load(second, files, "/creative/race.pxlayer").code == pixcake::ResultCode::Ok);
  EXPECT(first.set_opacity("background", 40) == pixcake::ResultCode::Ok);
  EXPECT(first.save(files, "/creative/race.pxlayer") == pixcake::ResultCode::Ok);
  EXPECT(first.document_revision() == 2);
  EXPECT(second.set_opacity("background", 90) == pixcake::ResultCode::Ok);
  EXPECT(second.save(files, "/creative/race.pxlayer") == pixcake::ResultCode::StateConflict);
  EXPECT(second.document_revision() == 1);
  pixcake::LayerDocument reloaded;
  EXPECT(pixcake::LayerDocument::load(reloaded, files, "/creative/race.pxlayer").code == pixcake::ResultCode::Ok);
  EXPECT(reloaded.document_revision() == 2);
  pixcake::LayerView background;
  EXPECT(reloaded.find_layer("background", background));
  EXPECT(background.opacity == 40);
}

void test_b116_layer_file_without_revision_record_loads_as_zero() {
  auto document = opened();
  const auto legacy = without_record(document.encode(), 9);
  const auto decoded = pixcake::LayerDocument::decode(legacy);
  EXPECT(decoded.has_value());
  EXPECT(decoded.has_value() && decoded->document_revision() == 0);
  EXPECT(decoded.has_value() && decoded->unknown_records().empty());
}

void test_b93_failed_layer_save_keeps_revision() {
  pixcake::test::FakeFileSystem files;
  auto document = opened();
  EXPECT(document.save(files, "/creative/full.pxlayer") == pixcake::ResultCode::Ok);
  files.write_error = pixcake::ResultCode::DiskFull;
  EXPECT(document.save(files, "/creative/full.pxlayer") == pixcake::ResultCode::DiskFull);
  EXPECT(document.document_revision() == 1);
  files.write_error = pixcake::ResultCode::Ok;
  EXPECT(document.save(files, "/creative/full.pxlayer") == pixcake::ResultCode::Ok);
  EXPECT(document.document_revision() == 2);
}

}  // namespace

int main() {
  test_c23_b73_stale_layer_save_is_rejected();
  test_b116_layer_file_without_revision_record_loads_as_zero();
  test_b93_failed_layer_save_keeps_revision();
  test_entry_does_not_copy_sliders_and_keeps_background();
  test_blend_opacity_lock_and_index();
  test_group_mask_selection_and_batch_design();
  test_place_result_shapes_once();
  test_place_result_replace_locked_generation_layer();
  test_remove_clears_generation_cursor_for_deleted_children();
  test_round_trip_keeps_unknown_records_and_rejects_the_retouch_magic();
  test_recovered_empty_save_round_trips();
  test_layer_save_observes_explicit_write_latch();
  test_apply_bakes_the_original_and_a_new_document_has_no_pixels();
  if (g_failures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  return 0;
}
