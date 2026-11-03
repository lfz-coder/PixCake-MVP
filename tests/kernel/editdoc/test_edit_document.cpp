#include "pixcake/editdoc/edit_document.hpp"
#include "pixcake/support/checksum.hpp"
#include "support/fake_file_system.hpp"

#include <algorithm>
#include <cstdint>
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

const char* kPath = "docs/img.pxedit";

std::vector<pixcake::VersionLimit> color_limits() {
  return {pixcake::VersionLimit{"color.global", 1}, pixcake::VersionLimit{"portrait.blemish", 1}};
}

bool contains(const std::vector<std::uint8_t>& haystack, const std::string& needle) {
  return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end()) != haystack.end();
}

void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xffu));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
  out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffu));
  out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffu));
}

void test_c24_preview_is_not_written() {
  auto created = pixcake::EditDocument::create("img-1");
  EXPECT(created.has_value());
  auto document = std::move(*created);
  EXPECT(document.set_preview("color.global", "exposure", "drag-1") == pixcake::ResultCode::Ok);
  EXPECT(document.set_preview("color.global", "exposure", "drag-final") == pixcake::ResultCode::Ok);
  EXPECT(document.revision() == 0);
  EXPECT(!contains(document.encode(), "drag-final"));
  pixcake::test::FakeFileSystem files;
  EXPECT(document.write_snapshot(files, kPath) == pixcake::ResultCode::Ok);
  EXPECT(!contains(files.files[kPath], "drag-final"));
  pixcake::EditDocument loaded;
  const auto result = pixcake::EditDocument::load(loaded, files, kPath, color_limits());
  EXPECT(result.code == pixcake::ResultCode::Ok);
  EXPECT(!loaded.preview("color.global", "exposure").has_value());
  EXPECT(!loaded.committed_parameter("color.global", "exposure").has_value());
}

void test_b61_commit_keeps_latest_display_value() {
  auto document = *pixcake::EditDocument::create("img-1");
  pixcake::test::FakeFileSystem files;
  EXPECT(document.set_preview("color.global", "exposure", "drag-1") == pixcake::ResultCode::Ok);
  EXPECT(document.set_preview("color.global", "exposure", "drag-2") == pixcake::ResultCode::Ok);
  EXPECT(document.set_preview("color.global", "exposure", "-1.50") == pixcake::ResultCode::Ok);
  EXPECT(document.commit_preview(0, "color.global", "exposure", color_limits(), files, kPath) ==
         pixcake::ResultCode::Ok);
  EXPECT(document.revision() == 1);
  EXPECT(document.committed_parameter("color.global", "exposure") == "-1.50");
  pixcake::EditDocument loaded;
  const auto result = pixcake::EditDocument::load(loaded, files, kPath, color_limits());
  EXPECT(result.code == pixcake::ResultCode::Ok);
  EXPECT(loaded.revision() == 1);
  EXPECT(loaded.committed_parameter("color.global", "exposure") == "-1.50");
  EXPECT(loaded.content_checksum() == document.content_checksum());
}

void test_c26_other_capability_bytes_stay() {
  auto document = *pixcake::EditDocument::create("img-1");
  std::vector<std::uint8_t> portrait{1, 2, 3, 4, 5, 6, 7, 8};
  document.store_fragment("portrait.blemish", 1, portrait);
  pixcake::test::FakeFileSystem files;
  document.set_preview("color.global", "exposure", "0.00");
  EXPECT(document.commit_preview(0, "color.global", "exposure", color_limits(), files, kPath) ==
         pixcake::ResultCode::Ok);
  const auto kept = document.fragment("portrait.blemish");
  EXPECT(kept.has_value());
  EXPECT(kept->inner == portrait);
}

void test_b85_unknown_inner_field_survives_commit() {
  auto document = *pixcake::EditDocument::create("img-1");
  std::vector<std::uint8_t> inner;
  append_u32(inner, 99);
  append_u32(inner, 4);
  const std::string keep = "KEEP";
  inner.insert(inner.end(), keep.begin(), keep.end());
  document.store_fragment("color.global", 1, inner);
  document.append_raw_record(42, {1, 2, 3, 4});
  document.set_preview("color.global", "exposure", "-1.50");
  pixcake::test::FakeFileSystem files;
  EXPECT(document.commit_preview(0, "color.global", "exposure", color_limits(), files, kPath) ==
         pixcake::ResultCode::Ok);
  pixcake::EditDocument loaded;
  EXPECT(pixcake::EditDocument::load(loaded, files, kPath, color_limits()).code == pixcake::ResultCode::Ok);
  const auto fragment = loaded.fragment("color.global");
  EXPECT(fragment.has_value());
  EXPECT(contains(fragment->inner, "KEEP"));
  EXPECT(loaded.committed_parameter("color.global", "exposure") == "-1.50");
  const auto unknown = loaded.payloads_of_type(42);
  EXPECT(unknown.size() == 1);
  EXPECT(unknown[0] == std::vector<std::uint8_t>({1, 2, 3, 4}));
}

void test_b84_too_new_fragment_is_not_rewritten() {
  auto document = *pixcake::EditDocument::create("img-1");
  const std::vector<std::uint8_t> original{9, 8, 7};
  document.store_fragment("color.global", 99, original);
  pixcake::test::FakeFileSystem files;
  EXPECT(document.write_snapshot(files, kPath) == pixcake::ResultCode::Ok);
  pixcake::EditDocument loaded;
  EXPECT(pixcake::EditDocument::load(loaded, files, kPath, color_limits()).code == pixcake::ResultCode::Ok);
  EXPECT(loaded.fragment_too_new("color.global"));
  loaded.set_preview("color.global", "exposure", "1.00");
  const auto before = loaded.encode();
  EXPECT(loaded.commit_preview(0, "color.global", "exposure", color_limits(), files, kPath) ==
         pixcake::ResultCode::CapabilityTooNew);
  EXPECT(loaded.revision() == 0);
  EXPECT(loaded.encode() == before);
  EXPECT(loaded.fragment("color.global")->inner == original);
}

void test_unknown_capability_is_kept_and_refused() {
  auto document = *pixcake::EditDocument::create("img-1");
  const std::vector<std::uint8_t> original{5, 5, 5};
  document.store_fragment("future.tool", 99, original);
  document.set_preview("future.tool", "exposure", "1");
  pixcake::test::FakeFileSystem files;
  EXPECT(document.commit_preview(0, "future.tool", "exposure", color_limits(), files, kPath) ==
         pixcake::ResultCode::StateConflict);
  EXPECT(document.fragment("future.tool")->inner == original);
  EXPECT(document.revision() == 0);
  document.set_preview("color.global", "exposure", "0.05");
  EXPECT(document.commit_preview(0, "color.global", "exposure", color_limits(), files, kPath) ==
         pixcake::ResultCode::Ok);
  EXPECT(document.fragment("future.tool")->inner == original);
  EXPECT(document.committed_parameter("color.global", "exposure") == "0.05");
}

void test_b93_disk_full_does_not_advance_revision() {
  auto document = *pixcake::EditDocument::create("img-1");
  pixcake::test::FakeFileSystem files;
  EXPECT(document.write_snapshot(files, kPath) == pixcake::ResultCode::Ok);
  const auto before = files.files[kPath];
  document.set_preview("color.global", "exposure", "0.10");
  files.write_error = pixcake::ResultCode::DiskFull;
  EXPECT(document.commit_preview(0, "color.global", "exposure", color_limits(), files, kPath) ==
         pixcake::ResultCode::DiskFull);
  EXPECT(document.revision() == 0);
  EXPECT(files.files[kPath] == before);
}

void test_b92_checksum_ignores_unrelated_source_path() {
  auto document = *pixcake::EditDocument::create("img-1");
  const auto first = document.content_checksum();
  const std::string moved = "/Users/moved/photo.png";
  EXPECT(document.content_checksum() == first);
  EXPECT(!contains(document.encode(), moved));
}

void test_envelope_round_trip() {
  auto document = *pixcake::EditDocument::create("img-1");
  document.add_mask_id("face");
  document.add_mask_id("sky");
  document.set_subject_override({0, 1, 0});
  document.add_manual_ref("liquify-1");
  document.add_preset_ref("preset-1");
  pixcake::test::FakeFileSystem files;
  EXPECT(document.write_snapshot(files, kPath) == pixcake::ResultCode::Ok);
  pixcake::EditDocument loaded;
  const auto result = pixcake::EditDocument::load(loaded, files, kPath, color_limits());
  EXPECT(result.code == pixcake::ResultCode::Ok);
  EXPECT(loaded.image_id() == "img-1");
  EXPECT(loaded.document_version() == 1);
  EXPECT(loaded.mask_ids().size() == 2);
  EXPECT(loaded.mask_ids()[0] == "face");
  EXPECT(loaded.mask_ids()[1] == "sky");
  EXPECT(loaded.subject_override() == std::vector<std::uint8_t>({0, 1, 0}));
  EXPECT(loaded.manual_refs().size() == 1);
  EXPECT(loaded.manual_refs()[0] == "liquify-1");
  EXPECT(loaded.preset_refs()[0] == "preset-1");
  EXPECT(loaded.content_checksum() == document.content_checksum());
}

void test_b73_stale_revision_does_not_overwrite() {
  auto document = *pixcake::EditDocument::create("img-1");
  pixcake::test::FakeFileSystem files;
  document.set_preview("color.global", "exposure", "0.20");
  EXPECT(document.commit_preview(0, "color.global", "exposure", color_limits(), files, kPath) ==
         pixcake::ResultCode::Ok);
  const auto saved = files.files[kPath];
  document.set_preview("color.global", "exposure", "9.99");
  EXPECT(document.commit_preview(0, "color.global", "exposure", color_limits(), files, kPath) ==
         pixcake::ResultCode::StateConflict);
  EXPECT(document.revision() == 1);
  EXPECT(files.files[kPath] == saved);
  EXPECT(document.committed_parameter("color.global", "exposure") == "0.20");
}

void test_empty_image_id_is_refused() {
  EXPECT(!pixcake::EditDocument::create("").has_value());
}

void test_fragment_replacement_keeps_other_records() {
  auto document = *pixcake::EditDocument::create("img-1");
  document.store_fragment("color.global", 1, {1, 2});
  document.store_fragment("color.local", 1, {3});
  document.store_fragment("portrait.blemish", 1, {4});
  document.append_raw_record(40, {9, 9});
  pixcake::test::FakeFileSystem files;
  EXPECT(document.write_snapshot(files, kPath) == pixcake::ResultCode::Ok);

  std::vector<pixcake::FragmentView> replacements(1);
  replacements[0].capability_id = "color.global";
  replacements[0].version = 1;
  replacements[0].inner = {7, 7};
  EXPECT(document.commit_fragment_replacement(0, {"color.global", "color.local"}, replacements, files,
                                              kPath) == pixcake::ResultCode::Ok);
  EXPECT(document.revision() == 1);
  EXPECT(document.fragment("color.global")->inner == std::vector<std::uint8_t>({7, 7}));
  EXPECT(!document.fragment("color.local").has_value());
  EXPECT(document.fragment("portrait.blemish")->inner == std::vector<std::uint8_t>({4}));
  EXPECT(document.payloads_of_type(40).size() == 1);

  const auto saved = files.files[kPath];
  EXPECT(document.commit_fragment_replacement(0, {"portrait.blemish"}, {}, files, kPath) ==
         pixcake::ResultCode::StateConflict);
  EXPECT(document.revision() == 1);
  EXPECT(files.files[kPath] == saved);

  files.write_error = pixcake::ResultCode::DiskFull;
  EXPECT(document.commit_fragment_replacement(1, {"portrait.blemish"}, {}, files, kPath) ==
         pixcake::ResultCode::DiskFull);
  EXPECT(document.revision() == 1);
  EXPECT(document.fragment("portrait.blemish").has_value());
  EXPECT(files.files[kPath] == saved);
}

}  // namespace

int main() {
  test_c24_preview_is_not_written();
  test_b61_commit_keeps_latest_display_value();
  test_c26_other_capability_bytes_stay();
  test_b85_unknown_inner_field_survives_commit();
  test_b84_too_new_fragment_is_not_rewritten();
  test_unknown_capability_is_kept_and_refused();
  test_b93_disk_full_does_not_advance_revision();
  test_b92_checksum_ignores_unrelated_source_path();
  test_envelope_round_trip();
  test_b73_stale_revision_does_not_overwrite();
  test_empty_image_id_is_refused();
  test_fragment_replacement_keeps_other_records();
  return g_failures == 0 ? 0 : 1;
}
