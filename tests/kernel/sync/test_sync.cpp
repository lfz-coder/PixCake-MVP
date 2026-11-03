#include "pixcake/editdoc/edit_document.hpp"
#include "pixcake/capability/capability_registry.hpp"
#include "pixcake/sync/sync_batch.hpp"
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

const pixcake::SyncItem* find_item(const std::vector<pixcake::SyncItem>& tree, const char* id) {
  for (const auto& item : tree) {
    if (item.id == id) {
      return &item;
    }
  }
  return nullptr;
}

void test_tree_reads_attributes_from_owners() {
  const auto tree = pixcake::sync_tree();
  EXPECT(tree.size() == 22);

  const auto* match = find_item(tree, "color.match");
  EXPECT(match != nullptr);
  EXPECT(match->module_label == "AI 色彩");
  EXPECT(match->label == "AI 追色");
  EXPECT(match->capability_id == "color.match");
  EXPECT(!match->enabled);
  EXPECT(match->blocked_reason == "同步时按每张图重新计算，不是把数值原样抄过去");

  const auto* local = find_item(tree, "color.match.local");
  EXPECT(local != nullptr);
  EXPECT(local->module_label == "AI 色彩");
  EXPECT(local->label == "局部追色");
  EXPECT(local->capability_id.empty());
  EXPECT(!local->enabled);
  EXPECT(local->blocked_reason == "局部追色不能批量同步");

  const auto* style = find_item(tree, "color.style");
  EXPECT(style != nullptr);
  EXPECT(!style->enabled);
  EXPECT(style->blocked_reason == match->blocked_reason);

  const auto* detail = find_item(tree, "detail");
  EXPECT(detail != nullptr);
  EXPECT(detail->module_label == "色彩");
  EXPECT(detail->label == "细节");
  EXPECT(detail->enabled);
  EXPECT(detail->capability_id == "detail");

  const auto* brush = find_item(tree, "mask.brush");
  EXPECT(brush != nullptr);
  EXPECT(brush->module_label == "色彩");
  EXPECT(brush->label == "自定义画笔蒙版");
  EXPECT(brush->capability_id.empty());
  EXPECT(!brush->enabled);
  EXPECT(brush->blocked_reason == "自定义里的画笔不能同步");

  const char* enabled[] = {"color.global",    "color.local",     "color.post",       "portrait.blemish",
                           "portrait.skin",    "portrait.reshape", "portrait.feature", "portrait.body",
                           "image.background", "image.idphoto",   "image.sky",        "clothes",
                           "pet",              "crop",            "manual.edge",      "manual.liquify",
                           "manual.heal"};
  for (const char* id : enabled) {
    const auto* item = find_item(tree, id);
    EXPECT(item != nullptr);
    EXPECT(item->enabled);
    EXPECT(item->capability_id == id);
    EXPECT(item->blocked_reason.empty());
  }
  EXPECT(find_item(tree, "manual.edge")->module_label == "手动");
  EXPECT(find_item(tree, "pet")->module_label == "宠物");

  const auto registry = pixcake::CapabilityRegistry::phase_one();
  for (const auto& descriptor : registry.descriptors()) {
    const auto* row = find_item(tree, descriptor.id.c_str());
    EXPECT(row != nullptr);
    if (row == nullptr) {
      continue;
    }
    EXPECT(row->module_label == descriptor.module_label);
    EXPECT(row->label == descriptor.label);
    EXPECT(row->capability_id == descriptor.id);
    EXPECT(row->enabled == descriptor.can_batch_sync);
    EXPECT(row->blocked_reason == descriptor.batch_sync_blocked_reason);
  }
}

pixcake::SyncTarget target_of(pixcake::EditDocument& document, pixcake::test::FakeFileSystem& files,
                              const char* path) {
  pixcake::SyncTarget target;
  target.document = &document;
  target.expected_revision = document.revision();
  target.files = &files;
  target.path = path;
  return target;
}

void test_checked_sync_copies_only_selected_fragments() {
  auto source = *pixcake::EditDocument::create("source");
  source.store_fragment("portrait.blemish", 1, {1});
  source.store_fragment("color.local", 1, {2});
  source.store_fragment("detail", 1, {3});
  pixcake::test::FakeFileSystem source_files;
  EXPECT(source.write_snapshot(source_files, "docs/source.pxedit") == pixcake::ResultCode::Ok);

  auto first = *pixcake::EditDocument::create("first");
  first.store_fragment("color.local", 1, {9});
  first.store_fragment("portrait.blemish", 1, {8});
  first.append_raw_record(40, {4});
  first.add_preset_ref("preset-1");
  pixcake::test::FakeFileSystem first_files;
  EXPECT(first.write_snapshot(first_files, "docs/first.pxedit") == pixcake::ResultCode::Ok);
  auto second = *pixcake::EditDocument::create("second");
  second.store_fragment("portrait.blemish", 1, {7});
  pixcake::test::FakeFileSystem second_files;
  EXPECT(second.write_snapshot(second_files, "docs/second.pxedit") == pixcake::ResultCode::Ok);
  std::vector<pixcake::SyncTarget> targets = {target_of(first, first_files, "docs/first.pxedit"),
                                              target_of(second, second_files, "docs/second.pxedit")};

  const auto empty = pixcake::apply_checked(source, "docs/source.pxedit", {}, targets);
  const auto duplicate =
      pixcake::apply_checked(source, "docs/source.pxedit", {"portrait.blemish", "portrait.blemish"}, targets);
  const auto ai = pixcake::apply_checked(source, "docs/source.pxedit", {"color.match"}, targets);
  const auto mismatch = pixcake::retry_failed(source, "docs/source.pxedit", {"portrait.blemish"}, targets, {});
  EXPECT(empty[0].code == pixcake::ResultCode::StateConflict);
  EXPECT(duplicate[1].code == pixcake::ResultCode::StateConflict);
  EXPECT(ai[0].code == pixcake::ResultCode::StateConflict);
  EXPECT(mismatch[0].code == pixcake::ResultCode::StateConflict);
  EXPECT(first.revision() == 0);

  const std::vector<std::string> checked = {"portrait.blemish", "detail"};
  const auto synced = pixcake::apply_checked(source, "docs/source.pxedit", checked, targets);
  EXPECT(synced.size() == 2);
  EXPECT(synced[0].code == pixcake::ResultCode::Ok);
  EXPECT(synced[1].code == pixcake::ResultCode::Ok);
  EXPECT(first.revision() == 1);
  EXPECT(first.fragment("portrait.blemish")->inner == std::vector<std::uint8_t>({1}));
  EXPECT(first.fragment("detail")->inner == std::vector<std::uint8_t>({3}));
  EXPECT(first.fragment("color.local")->inner == std::vector<std::uint8_t>({9}));
  EXPECT(first.payloads_of_type(40).size() == 1);
  EXPECT(first.preset_refs().size() == 1);
  EXPECT(second.fragment("portrait.blemish")->inner == std::vector<std::uint8_t>({1}));
  EXPECT(second.fragment("detail")->inner == std::vector<std::uint8_t>({3}));
  EXPECT(source.revision() == 0);
  const auto second_saved = second_files.files["docs/second.pxedit"];

  auto blocked = pixcake::apply_checked(source, "docs/source.pxedit", {"mask.brush"}, targets);
  EXPECT(blocked[0].code == pixcake::ResultCode::StateConflict);
  EXPECT(blocked[1].code == pixcake::ResultCode::StateConflict);
  EXPECT(first.revision() == 1);
  EXPECT(second_files.files["docs/second.pxedit"] == second_saved);

  second_files.write_error = pixcake::ResultCode::DiskFull;
  targets[0].expected_revision = first.revision();
  targets[1].expected_revision = second.revision();
  const auto failed = pixcake::apply_checked(source, "docs/source.pxedit", {"color.global"}, targets);
  EXPECT(failed[0].code == pixcake::ResultCode::Ok);
  EXPECT(failed[1].code == pixcake::ResultCode::DiskFull);
  EXPECT(second.revision() == 1);
  EXPECT(second_files.files["docs/second.pxedit"] == second_saved);

  second_files.write_error = pixcake::ResultCode::Ok;
  const auto retried = pixcake::retry_failed(source, "docs/source.pxedit", {"color.global"}, targets, failed);
  EXPECT(retried[0].code == pixcake::ResultCode::Ok);
  EXPECT(retried[1].code == pixcake::ResultCode::Ok);
  EXPECT(first.revision() == 2);
  EXPECT(second.revision() == 2);
  EXPECT(!second.fragment("color.global").has_value());
}

void test_same_path_skips_only_that_target() {
  auto source = *pixcake::EditDocument::create("source");
  source.store_fragment("portrait.blemish", 1, {1});
  pixcake::test::FakeFileSystem source_files;
  EXPECT(source.write_snapshot(source_files, "docs/source.pxedit") == pixcake::ResultCode::Ok);
  auto other = *pixcake::EditDocument::create("other");
  pixcake::test::FakeFileSystem other_files;
  EXPECT(other.write_snapshot(other_files, "docs/other.pxedit") == pixcake::ResultCode::Ok);
  std::vector<pixcake::SyncTarget> targets = {target_of(source, source_files, "docs/source.pxedit"),
                                              target_of(other, other_files, "docs/other.pxedit")};
  const auto synced =
      pixcake::apply_checked(source, "docs/source.pxedit", {"portrait.blemish"}, targets);
  EXPECT(synced[0].code == pixcake::ResultCode::StateConflict);
  EXPECT(synced[1].code == pixcake::ResultCode::Ok);
  EXPECT(source.revision() == 0);
  EXPECT(other.revision() == 1);
  EXPECT(other.fragment("portrait.blemish")->inner == std::vector<std::uint8_t>({1}));
}

void test_copy_paste_moves_committed_fragments_without_a_tree() {
  auto source = *pixcake::EditDocument::create("source");
  source.store_fragment("color.match", 1, {5});
  source.store_fragment("portrait.blemish", 1, {6});
  source.append_raw_record(40, {1});
  EXPECT(source.set_preview("portrait.blemish", "degree", "10") == pixcake::ResultCode::Ok);
  const auto copied = pixcake::copy_committed(source);
  EXPECT(copied.fragments.size() == 2);
  EXPECT(copied.fragments[0].capability_id == "color.match");

  auto target = *pixcake::EditDocument::create("target");
  target.store_fragment("color.local", 1, {9});
  pixcake::test::FakeFileSystem files;
  EXPECT(target.write_snapshot(files, "docs/target.pxedit") == pixcake::ResultCode::Ok);
  EXPECT(pixcake::paste_committed(target, copied, 0, files, "docs/target.pxedit") == pixcake::ResultCode::Ok);
  EXPECT(target.revision() == 1);
  EXPECT(target.fragment("color.match")->inner == std::vector<std::uint8_t>({5}));
  EXPECT(target.fragment("color.local")->inner == std::vector<std::uint8_t>({9}));
  EXPECT(target.payloads_of_type(40).empty());

  const auto saved = files.files["docs/target.pxedit"];
  EXPECT(pixcake::paste_committed(target, copied, 0, files, "docs/target.pxedit") ==
         pixcake::ResultCode::StateConflict);
  EXPECT(target.revision() == 1);
  EXPECT(files.files["docs/target.pxedit"] == saved);

  pixcake::EffectClipboard empty;
  EXPECT(pixcake::paste_committed(target, empty, 1, files, "docs/target.pxedit") == pixcake::ResultCode::Ok);
  EXPECT(target.revision() == 1);
  EXPECT(files.files["docs/target.pxedit"] == saved);

  files.write_error = pixcake::ResultCode::DiskFull;
  EXPECT(pixcake::paste_committed(target, copied, 1, files, "docs/target.pxedit") ==
         pixcake::ResultCode::DiskFull);
  EXPECT(target.revision() == 1);
  EXPECT(target.fragment("portrait.blemish")->inner == std::vector<std::uint8_t>({6}));
  EXPECT(files.files["docs/target.pxedit"] == saved);
}

}  // namespace

int main() {
  test_tree_reads_attributes_from_owners();
  test_checked_sync_copies_only_selected_fragments();
  test_same_path_skips_only_that_target();
  test_copy_paste_moves_committed_fragments_without_a_tree();
  if (g_failures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  return 0;
}
