#include "pixcake/editdoc/edit_document.hpp"
#include "pixcake/capabilities/module_set.hpp"
#include "pixcake/capability/capability_module.hpp"
#include "pixcake/capability/capability_registry.hpp"
#include "pixcake/sync/sync_batch.hpp"
#include "pixcake/capability/fragment_codec.hpp"
#include "support/fake_file_system.hpp"

#include <cstdio>
#include <optional>
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

// 能力片段的合法编码，里面只有一条测试用的未知记录（类型 77）带着标记字节。
// 预设、同步、粘贴经能力模块校验片段，裸字节不再被接受。
std::vector<std::uint8_t> marked(std::uint8_t n) {
  return pixcake::encode_fragment_records({{77, {n}}});
}

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

void test_b37_checked_sync_copies_only_selected_fragments() {
  auto source = *pixcake::EditDocument::create("source");
  source.store_fragment("portrait.blemish", 1, marked(1));
  source.store_fragment("color.local", 1, marked(2));
  source.store_fragment("detail", 1, marked(3));
  pixcake::test::FakeFileSystem source_files;
  EXPECT(source.write_snapshot(source_files, "docs/source.pxedit") == pixcake::ResultCode::Ok);

  auto first = *pixcake::EditDocument::create("first");
  first.store_fragment("color.local", 1, marked(9));
  first.store_fragment("portrait.blemish", 1, marked(8));
  first.append_raw_record(40, {4});
  first.add_preset_ref("preset-1");
  pixcake::test::FakeFileSystem first_files;
  EXPECT(first.write_snapshot(first_files, "docs/first.pxedit") == pixcake::ResultCode::Ok);
  auto second = *pixcake::EditDocument::create("second");
  second.store_fragment("portrait.blemish", 1, marked(7));
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
  EXPECT(first.fragment("portrait.blemish")->inner == marked(1));
  EXPECT(first.fragment("detail")->inner == marked(3));
  EXPECT(first.fragment("color.local")->inner == marked(9));
  EXPECT(first.payloads_of_type(40).size() == 1);
  EXPECT(first.preset_refs().size() == 1);
  EXPECT(second.fragment("portrait.blemish")->inner == marked(1));
  EXPECT(second.fragment("detail")->inner == marked(3));
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

void test_b68_same_path_skips_only_that_target() {
  auto source = *pixcake::EditDocument::create("source");
  source.store_fragment("portrait.blemish", 1, marked(1));
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
  EXPECT(other.fragment("portrait.blemish")->inner == marked(1));
}

void test_retry_matches_stable_target_paths() {
  auto source = *pixcake::EditDocument::create("source");
  source.store_fragment("portrait.blemish", 1, marked(1));
  pixcake::test::FakeFileSystem source_files;
  EXPECT(source.write_snapshot(source_files, "docs/source.pxedit") == pixcake::ResultCode::Ok);
  auto first = *pixcake::EditDocument::create("first");
  auto second = *pixcake::EditDocument::create("second");
  pixcake::test::FakeFileSystem first_files;
  pixcake::test::FakeFileSystem second_files;
  EXPECT(first.write_snapshot(first_files, "docs/first.pxedit") == pixcake::ResultCode::Ok);
  EXPECT(second.write_snapshot(second_files, "docs/second.pxedit") == pixcake::ResultCode::Ok);
  std::vector<pixcake::SyncTarget> original = {target_of(first, first_files, "docs/first.pxedit"),
                                               target_of(second, second_files, "docs/second.pxedit")};
  const auto previous = pixcake::apply_checked(source, "docs/source.pxedit", {"portrait.blemish"}, original);
  EXPECT(previous[0].code == pixcake::ResultCode::Ok);
  EXPECT(previous[1].code == pixcake::ResultCode::Ok);

  std::vector<pixcake::SyncTarget> reordered = {original[1], original[0]};
  std::vector<pixcake::SyncItemResult> mixed = previous;
  mixed[0].path = "docs/second.pxedit";
  mixed[1].path = "docs/first.pxedit";
  mixed[0].code = pixcake::ResultCode::DiskFull;
  mixed[1].code = pixcake::ResultCode::Ok;
  const auto retried = pixcake::retry_failed(source, "docs/source.pxedit", {"portrait.blemish"}, reordered, mixed);
  EXPECT(retried[0].path == "docs/second.pxedit");
  EXPECT(retried[0].code == pixcake::ResultCode::StateConflict);
  EXPECT(retried[1].path == "docs/first.pxedit");
  EXPECT(retried[1].code == pixcake::ResultCode::Ok);
}

void test_copy_paste_moves_committed_fragments_without_a_tree() {
  auto source = *pixcake::EditDocument::create("source");
  source.store_fragment("color.match", 1, marked(5));
  source.store_fragment("portrait.blemish", 1, marked(6));
  source.append_raw_record(40, {1});
  EXPECT(source.set_preview("portrait.blemish", "degree", "10") == pixcake::ResultCode::Ok);
  const auto copied = pixcake::copy_committed(source);
  EXPECT(copied.fragments.size() == 2);
  EXPECT(copied.fragments[0].capability_id == "color.match");

  auto target = *pixcake::EditDocument::create("target");
  target.store_fragment("color.local", 1, marked(9));
  pixcake::test::FakeFileSystem files;
  EXPECT(target.write_snapshot(files, "docs/target.pxedit") == pixcake::ResultCode::Ok);
  EXPECT(pixcake::paste_committed(target, copied, 0, files, "docs/target.pxedit") == pixcake::ResultCode::Ok);
  EXPECT(target.revision() == 1);
  EXPECT(target.fragment("color.match")->inner == marked(5));
  EXPECT(target.fragment("color.local")->inner == marked(9));
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
  EXPECT(target.fragment("portrait.blemish")->inner == marked(6));
  EXPECT(files.files["docs/target.pxedit"] == saved);
}

}  // namespace

// C-31：同步与粘贴经能力模块写入。来源片段损坏或取值越界时拒绝，目标不变。
void test_c31_sync_and_paste_validate_through_capability_modules() {
  auto source = *pixcake::EditDocument::create("source");
  source.store_fragment("color.local", 1, {1, 0, 0});
  pixcake::test::FakeFileSystem source_files;
  EXPECT(source.write_snapshot(source_files, "docs/source.pxedit") == pixcake::ResultCode::Ok);
  auto target = *pixcake::EditDocument::create("target");
  target.store_fragment("color.local", 1, marked(9));
  pixcake::test::FakeFileSystem files;
  EXPECT(target.write_snapshot(files, "docs/target.pxedit") == pixcake::ResultCode::Ok);
  std::vector<pixcake::SyncTarget> targets = {target_of(target, files, "docs/target.pxedit")};
  const auto synced = pixcake::apply_checked(source, "docs/source.pxedit", {"color.local"}, targets);
  EXPECT(synced.size() == 1 && synced[0].code == pixcake::ResultCode::DocumentCorrupt);
  EXPECT(target.revision() == 0 && target.fragment("color.local")->inner == marked(9));

  pixcake::EffectClipboard clipboard;
  clipboard.fragments.push_back(pixcake::FragmentView{
      "color.global", 1,
      pixcake::encode_fragment_records({{pixcake::kFragmentParameterRecord,
                                         pixcake::encode_parameter_record("vibrance", "500")}})});
  const auto saved = files.files["docs/target.pxedit"];
  EXPECT(pixcake::paste_committed(target, clipboard, 0, files, "docs/target.pxedit") ==
         pixcake::ResultCode::StateConflict);
  EXPECT(target.revision() == 0 && files.files["docs/target.pxedit"] == saved);
}

// 局部调色的多个蒙版：第 2 个蒙版的参数提交后版本是 2，重新读盘仍在；旧的版本 1 片段照常可读，写入时升到 2；
// 粘贴到另一张照片带着全部蒙版。
void test_local_instances_persist_and_paste() {
  const auto limits = pixcake::phase_one_version_limits();
  auto document = *pixcake::EditDocument::create("multi");
  pixcake::test::FakeFileSystem files;
  EXPECT(document.write_snapshot(files, "docs/multi.pxedit") == pixcake::ResultCode::Ok);
  // A legacy fragment: version 1, unprefixed values only.
  document.store_fragment("color.local", 1,
                          pixcake::encode_fragment_records({{pixcake::kFragmentParameterRecord,
                                                             pixcake::encode_parameter_record("exposure", "1")}}));
  EXPECT(document.write_snapshot(files, "docs/multi.pxedit") == pixcake::ResultCode::Ok);
  EXPECT(document.set_preview("color.local", "m2:exposure", "0.5") == pixcake::ResultCode::Ok);
  EXPECT(document.commit_preview(document.revision(), "color.local", "m2:exposure", limits, files,
                                 "docs/multi.pxedit") == pixcake::ResultCode::Ok);
  EXPECT(document.fragment("color.local")->version == 2);
  EXPECT(document.committed_parameter("color.local", "exposure") == std::optional<std::string>("1"));
  EXPECT(document.committed_parameter("color.local", "m2:exposure") == std::optional<std::string>("0.5"));
  EXPECT(document.set_preview("color.local", "m3:region", "person.hair") == pixcake::ResultCode::Ok);
  EXPECT(document.commit_preview(document.revision(), "color.local", "m3:region", limits, files,
                                 "docs/multi.pxedit") == pixcake::ResultCode::Ok);
  pixcake::EditDocument reloaded = *pixcake::EditDocument::create("multi");
  EXPECT(pixcake::EditDocument::load(reloaded, files, "docs/multi.pxedit", limits).code == pixcake::ResultCode::Ok);
  EXPECT(reloaded.committed_parameter("color.local", "m3:region") == std::optional<std::string>("person.hair"));
  EXPECT(reloaded.committed_parameter("color.local", "exposure") == std::optional<std::string>("1"));
  // A bad prefix or value is refused at commit.
  EXPECT(document.set_preview("color.local", "m40:exposure", "1") == pixcake::ResultCode::Ok);  // preview is free text
  EXPECT(document.commit_preview(document.revision(), "color.local", "m40:exposure", limits, files,
                                 "docs/multi.pxedit") != pixcake::ResultCode::Ok);
  // Paste carries every mask.
  pixcake::EffectClipboard clipboard;
  clipboard.fragments.push_back(*document.fragment("color.local"));
  auto target = *pixcake::EditDocument::create("target-multi");
  pixcake::test::FakeFileSystem target_files;
  EXPECT(target.write_snapshot(target_files, "docs/t.pxedit") == pixcake::ResultCode::Ok);
  EXPECT(pixcake::paste_committed(target, clipboard, 0, target_files, "docs/t.pxedit") == pixcake::ResultCode::Ok);
  EXPECT(target.committed_parameter("color.local", "m2:exposure") == std::optional<std::string>("0.5"));
  EXPECT(target.committed_parameter("color.local", "m3:region") == std::optional<std::string>("person.hair"));
}

void test_remove_local_mask() {
  const auto limits = pixcake::phase_one_version_limits();
  auto document = *pixcake::EditDocument::create("remove");
  pixcake::test::FakeFileSystem files;
  EXPECT(document.write_snapshot(files, "docs/r.pxedit") == pixcake::ResultCode::Ok);
  for (const char* id : {"exposure", "m1:exposure", "m1:region", "m2:contrast"}) {
    const std::string value = std::string(id) == "m1:region" ? "person.hair" : (std::string(id).find("exposure") != std::string::npos ? "1" : "10");
    EXPECT(document.set_preview("color.local", id, value) == pixcake::ResultCode::Ok);
    EXPECT(document.commit_preview(document.revision(), "color.local", id, limits, files, "docs/r.pxedit") ==
           pixcake::ResultCode::Ok);
  }
  const auto before = document.revision();
  EXPECT(document.commit_parameter_prefix_removal(before, "color.local", "m1:", files, "docs/r.pxedit") ==
         pixcake::ResultCode::Ok);
  EXPECT(document.revision() == before + 1);
  EXPECT(!document.committed_parameter("color.local", "m1:exposure") && !document.committed_parameter("color.local", "m1:region"));
  EXPECT(document.committed_parameter("color.local", "exposure") && document.committed_parameter("color.local", "m2:contrast"));
  // A mask with nothing in the document, or a stale revision, is refused and changes nothing.
  EXPECT(document.commit_parameter_prefix_removal(document.revision(), "color.local", "m1:", files, "docs/r.pxedit") ==
         pixcake::ResultCode::StateConflict);
  EXPECT(document.commit_parameter_prefix_removal(before, "color.local", "m2:", files, "docs/r.pxedit") ==
         pixcake::ResultCode::StateConflict);
  // The unprefixed group (instance 0) goes without touching the others.
  EXPECT(document.commit_parameter_prefix_removal(document.revision(), "color.local", "", files, "docs/r.pxedit") ==
         pixcake::ResultCode::Ok);
  EXPECT(!document.committed_parameter("color.local", "exposure") && document.committed_parameter("color.local", "m2:contrast"));
  pixcake::EditDocument reloaded = *pixcake::EditDocument::create("remove");
  EXPECT(pixcake::EditDocument::load(reloaded, files, "docs/r.pxedit", limits).code == pixcake::ResultCode::Ok);
  EXPECT(!reloaded.committed_parameter("color.local", "exposure") && reloaded.committed_parameter("color.local", "m2:contrast"));
}

// 多个参数一次提交：一个修订号；任何一个不合格整次拒绝、文档和文件都不变。
void test_commit_parameters_is_one_revision() {
  const auto limits = pixcake::phase_one_version_limits();
  auto document = *pixcake::EditDocument::create("batch");
  pixcake::test::FakeFileSystem files;
  EXPECT(document.write_snapshot(files, "docs/b.pxedit") == pixcake::ResultCode::Ok);
  EXPECT(document.set_preview("color.local", "m1:region", "linear") == pixcake::ResultCode::Ok);
  EXPECT(document.set_preview("color.local", "m1:gradient_x", "30") == pixcake::ResultCode::Ok);
  EXPECT(document.set_preview("color.local", "m1:gradient_angle", "90") == pixcake::ResultCode::Ok);
  const auto before = document.revision();
  EXPECT(document.commit_parameters(before, "color.local", {"m1:region", "m1:gradient_x", "m1:gradient_angle"}, limits,
                                    files, "docs/b.pxedit") == pixcake::ResultCode::Ok);
  EXPECT(document.revision() == before + 1);
  EXPECT(document.committed_parameter("color.local", "m1:gradient_x") == std::optional<std::string>("30") &&
         document.committed_parameter("color.local", "m1:gradient_angle") == std::optional<std::string>("90") &&
         document.fragment("color.local")->version == 2);
  const auto saved = files.files["docs/b.pxedit"];
  EXPECT(document.set_preview("color.local", "m1:gradient_x", "60") == pixcake::ResultCode::Ok);
  EXPECT(document.set_preview("color.local", "m1:gradient_size", "500") == pixcake::ResultCode::Ok);  // out of range
  EXPECT(document.commit_parameters(document.revision(), "color.local", {"m1:gradient_x", "m1:gradient_size"}, limits, files,
                                    "docs/b.pxedit") == pixcake::ResultCode::StateConflict);
  EXPECT(document.revision() == before + 1 && files.files["docs/b.pxedit"] == saved &&
         document.committed_parameter("color.local", "m1:gradient_x") == std::optional<std::string>("30"));
  // A parameter with no preview, a stale revision and an empty list are refused too.
  EXPECT(document.commit_parameters(document.revision(), "color.local", {"m1:gradient_y"}, limits, files, "docs/b.pxedit") ==
         pixcake::ResultCode::StateConflict);
  EXPECT(document.commit_parameters(before, "color.local", {"m1:gradient_x"}, limits, files, "docs/b.pxedit") ==
         pixcake::ResultCode::StateConflict);
  EXPECT(document.commit_parameters(document.revision(), "color.local", {}, limits, files, "docs/b.pxedit") ==
         pixcake::ResultCode::StateConflict);
}

// 自定义画笔蒙版不能同步、不能存预设（需求 §10.8）：粘贴、同步、预设搬运都不带走画笔子项的区域、叠加方式、反相和笔触，
// 其余的（别的子项、整个蒙版的调整）照常。
void test_brush_items_do_not_travel() {
  const std::string strokes = "a1000,0,100,100:1000,5000,8000,0";
  std::vector<pixcake::FragmentRecord> records;
  const auto add = [&records](const char* id, const char* value) {
    records.push_back({pixcake::kFragmentParameterRecord, pixcake::encode_parameter_record(id, value)});
  };
  add("m1:exposure", "1");
  add("m1:region", "brush");
  add("m1:strokes", strokes.c_str());
  add("m1:invert", "on");
  add("m2:exposure", "0.5");
  add("m2:region", "person.hair");
  add("m2:s1:region", "brush");
  add("m2:s1:mode", "subtract");
  add("m2:s1:strokes", strokes.c_str());
  add("m2:s2:region", "linear");
  pixcake::FragmentBytes source;
  source.version = 2;
  source.inner = pixcake::encode_fragment_records(records);
  const pixcake::FragmentTransfer moved = pixcake::transfer_fragment("color.local", source, std::nullopt);
  EXPECT(moved.code == pixcake::ResultCode::Ok && moved.fragment.has_value());
  std::vector<pixcake::FragmentRecord> out;
  EXPECT(moved.fragment && pixcake::decode_fragment_records(moved.fragment->inner, out));
  std::map<std::string, std::string> got;
  for (const auto& record : out) {
    std::string id, value;
    if (pixcake::decode_parameter_record(record.payload, id, value)) got[id] = value;
  }
  EXPECT(got.count("m1:exposure") == 1 && got.count("m2:exposure") == 1 && got["m2:region"] == "person.hair" &&
         got["m2:s2:region"] == "linear");  // everything else travels
  EXPECT(got.count("m1:region") == 0 && got.count("m1:strokes") == 0 && got.count("m1:invert") == 0 &&
         got.count("m2:s1:region") == 0 && got.count("m2:s1:mode") == 0 && got.count("m2:s1:strokes") == 0);  // the brush items stay
  EXPECT(got.size() == 4);
  // Paste puts the same into the target document.
  pixcake::EffectClipboard clipboard;
  clipboard.fragments.push_back({"color.local", 2, source.inner});
  auto target = *pixcake::EditDocument::create("brush-target");
  pixcake::test::FakeFileSystem files;
  EXPECT(target.write_snapshot(files, "docs/b.pxedit") == pixcake::ResultCode::Ok);
  EXPECT(pixcake::paste_committed(target, clipboard, 0, files, "docs/b.pxedit") == pixcake::ResultCode::Ok);
  EXPECT(target.committed_parameter("color.local", "m2:exposure") == std::optional<std::string>("0.5") &&
         !target.committed_parameter("color.local", "m1:strokes") && !target.committed_parameter("color.local", "m1:region"));
}

int main() {
  test_brush_items_do_not_travel();
  test_commit_parameters_is_one_revision();
  test_remove_local_mask();
  test_local_instances_persist_and_paste();
  test_c31_sync_and_paste_validate_through_capability_modules();
  test_tree_reads_attributes_from_owners();
  test_b37_checked_sync_copies_only_selected_fragments();
  test_b68_same_path_skips_only_that_target();
  test_retry_matches_stable_target_paths();
  test_copy_paste_moves_committed_fragments_without_a_tree();
  if (g_failures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  return 0;
}
