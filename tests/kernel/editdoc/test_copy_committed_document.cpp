#include "pixcake/editdoc/edit_document.hpp"
#include "pixcake/project/project_library.hpp"
#include "pixcake/project/source_deleter.hpp"
#include "pixcake/storage/sqlite_project_store.hpp"
#include "pixcake/support/clock.hpp"
#include "pixcake/support/log_sink.hpp"
#include "support/admitted_photo.hpp"
#include "support/fake_file_system.hpp"
#include "support/temp_dir.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
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

const char* kSourceDoc = "edits/original.pxedit";
const char* kCopyDoc = "edits/copy-1.pxedit";
const char* kPhoto = "/pictures/a.png";

std::vector<pixcake::VersionLimit> color_limits() {
  return {pixcake::VersionLimit{"color.global", 1}};
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

struct FixedClock : pixcake::Clock {
  std::int64_t unix_seconds() const override { return 1700000000; }
};

struct MemoryLog : pixcake::LogSink {
  void write(const pixcake::LogRecord&) override {}
};

struct FakeDeleter : pixcake::SourceDeleter {
  int calls = 0;
  pixcake::ResultCode remove_source(const std::string&) override {
    ++calls;
    return pixcake::ResultCode::Ok;
  }
};

struct LibraryFixture {
  pixcake::test::TempDir dir;
  std::unique_ptr<pixcake::SqliteProjectStore> store;
  FixedClock clock;
  FakeDeleter deleter;
  MemoryLog log;
  std::unique_ptr<pixcake::ProjectLibrary> library;

  explicit LibraryFixture(pixcake::test::FakeFileSystem& files) { open(files); }

  void open(pixcake::test::FakeFileSystem& files) {
    library.reset();
    store.reset();
    EXPECT(pixcake::SqliteProjectStore::open(dir.path() + "/library.db", store) == pixcake::ResultCode::Ok);
    library = std::make_unique<pixcake::ProjectLibrary>(*store, files, clock, deleter, &log);
  }
};

std::string file_text(const std::filesystem::path& path) {
  std::ifstream input(path);
  return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

void test_sources_stay_in_their_modules() {
  const std::filesystem::path root =
      std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
  const std::filesystem::path editdoc_dir = root / "kernel" / "editdoc";
  const std::filesystem::path project_dir = root / "kernel" / "project";
  EXPECT(std::filesystem::is_directory(editdoc_dir));
  EXPECT(std::filesystem::is_directory(project_dir));
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::recursive_directory_iterator(editdoc_dir)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const std::string text = file_text(entry.path());
    EXPECT(text.find("project_library.hpp") == std::string::npos);
    EXPECT(text.find("sqlite3.h") == std::string::npos);
    EXPECT(text.find("exportq") == std::string::npos);
    EXPECT(text.find("ledger") == std::string::npos);
  }
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::recursive_directory_iterator(project_dir)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    EXPECT(file_text(entry.path()).find("copy_committed") == std::string::npos);
  }
}

void test_creating_a_copy_duplicates_the_committed_document() {
  pixcake::test::FakeFileSystem files;
  const std::vector<std::uint8_t> photo_bytes = pixcake::test::png_with({9, 8, 7});
  files.files[kPhoto] = photo_bytes;
  LibraryFixture world(files);

  auto document = *pixcake::EditDocument::create("img-1");
  document.add_mask_id("face");
  document.set_subject_override({0, 1, 0});
  document.add_manual_ref("liquify-1");
  document.add_preset_ref("preset-1");
  std::vector<std::uint8_t> inner;
  append_u32(inner, 99);
  append_u32(inner, 4);
  const std::string keep = "KEEP";
  inner.insert(inner.end(), keep.begin(), keep.end());
  document.store_fragment("color.global", 1, inner);
  document.append_raw_record(42, {1, 2, 3, 4});
  EXPECT(document.set_preview("color.global", "exposure", "-1.50") == pixcake::ResultCode::Ok);
  EXPECT(document.commit_preview(0, "color.global", "exposure", color_limits(), files, kSourceDoc) ==
         pixcake::ResultCode::Ok);
  EXPECT(document.set_preview("color.global", "exposure", "drag-final") == pixcake::ResultCode::Ok);

  pixcake::EditDocument copy = *pixcake::EditDocument::create("untouched");
  const auto copied =
      pixcake::EditDocument::copy_committed(files, kSourceDoc, kCopyDoc, color_limits(), copy);
  EXPECT(copied.code == pixcake::ResultCode::Ok);
  EXPECT(!copied.absent);
  EXPECT(!copied.recovered_empty);
  EXPECT(files.files[kCopyDoc] == files.files[kSourceDoc]);
  EXPECT(!contains(files.files[kCopyDoc], "drag-final"));
  EXPECT(document.preview("color.global", "exposure") == "drag-final");
  EXPECT(copy.image_id() == "img-1");
  EXPECT(copy.revision() == 1);
  EXPECT(copy.committed_parameter("color.global", "exposure") == "-1.50");
  EXPECT(!copy.preview("color.global", "exposure").has_value());
  EXPECT(copy.content_checksum() == document.content_checksum());
  EXPECT(copy.mask_ids().size() == 1);
  EXPECT(copy.mask_ids()[0] == "face");
  EXPECT(copy.subject_override() == std::vector<std::uint8_t>({0, 1, 0}));
  EXPECT(copy.manual_refs().size() == 1);
  EXPECT(copy.manual_refs()[0] == "liquify-1");
  EXPECT(copy.preset_refs().size() == 1);
  EXPECT(copy.preset_refs()[0] == "preset-1");
  const auto unknown = copy.payloads_of_type(42);
  EXPECT(unknown.size() == 1);
  EXPECT(unknown[0] == std::vector<std::uint8_t>({1, 2, 3, 4}));
  const auto fragment = copy.fragment("color.global");
  EXPECT(fragment.has_value());
  EXPECT(fragment.has_value() && contains(fragment->inner, "KEEP"));

  const pixcake::ProjectResult project = world.library->create_project("相册", pixcake::kOriginLocal, false);
  EXPECT(project.code == pixcake::ResultCode::Ok);
  const pixcake::ProjectResult photo = world.library->add_photo(project.id, pixcake::test::admitted_png(files, kPhoto), "a.png", {1, 2, 0, 255});
  EXPECT(photo.code == pixcake::ResultCode::Ok);
  const pixcake::ProjectResult created = world.library->create_virtual_copy(photo.id, kCopyDoc);
  EXPECT(created.code == pixcake::ResultCode::Ok);
  std::vector<pixcake::PhotoRow> photos;
  EXPECT(world.library->list_photos(project.id, photos) == pixcake::ResultCode::Ok);
  EXPECT(photos.size() == 1);
  std::vector<pixcake::VirtualCopyRow> copies;
  EXPECT(world.library->list_virtual_copies(photo.id, copies) == pixcake::ResultCode::Ok);
  EXPECT(copies.size() == 1);
  EXPECT(copies[0].document_id == kCopyDoc);
  EXPECT(copies[0].source_photo_id == photo.id);
  EXPECT(files.files[kPhoto] == photo_bytes);
  EXPECT(world.deleter.calls == 0);

  pixcake::EditDocument reloaded;
  EXPECT(pixcake::EditDocument::load(reloaded, files, kCopyDoc, color_limits()).code == pixcake::ResultCode::Ok);
  EXPECT(reloaded.committed_parameter("color.global", "exposure") == "-1.50");
  EXPECT(!reloaded.preview("color.global", "exposure").has_value());
  EXPECT(reloaded.revision() == 1);
  EXPECT(reloaded.image_id() == "img-1");

  const auto source_after_copy = files.files[kSourceDoc];
  EXPECT(reloaded.set_preview("color.global", "exposure", "2.00") == pixcake::ResultCode::Ok);
  EXPECT(reloaded.commit_preview(reloaded.revision(), "color.global", "exposure", color_limits(), files, kCopyDoc) ==
         pixcake::ResultCode::Ok);
  EXPECT(files.files[kSourceDoc] == source_after_copy);
  EXPECT(reloaded.committed_parameter("color.global", "exposure") == "2.00");

  EXPECT(document.set_preview("color.global", "exposure", "3.00") == pixcake::ResultCode::Ok);
  EXPECT(document.commit_preview(document.revision(), "color.global", "exposure", color_limits(), files,
                                 kSourceDoc) == pixcake::ResultCode::Ok);
  pixcake::EditDocument copy_again;
  EXPECT(pixcake::EditDocument::load(copy_again, files, kCopyDoc, color_limits()).code == pixcake::ResultCode::Ok);
  EXPECT(copy_again.committed_parameter("color.global", "exposure") == "2.00");
  EXPECT(copy_again.revision() == 2);
  pixcake::EditDocument source_again;
  EXPECT(pixcake::EditDocument::load(source_again, files, kSourceDoc, color_limits()).code ==
         pixcake::ResultCode::Ok);
  EXPECT(source_again.committed_parameter("color.global", "exposure") == "3.00");
}

void test_refuses_empty_or_same_path() {
  pixcake::test::FakeFileSystem files;
  auto document = *pixcake::EditDocument::create("img-1");
  EXPECT(document.set_preview("color.global", "exposure", "-1.50") == pixcake::ResultCode::Ok);
  EXPECT(document.commit_preview(0, "color.global", "exposure", color_limits(), files, kSourceDoc) ==
         pixcake::ResultCode::Ok);
  const auto saved = files.files[kSourceDoc];
  const auto keys_before = files.files.size();
  pixcake::EditDocument copy = *pixcake::EditDocument::create("untouched");

  const auto empty_source =
      pixcake::EditDocument::copy_committed(files, "", kCopyDoc, color_limits(), copy);
  EXPECT(empty_source.code == pixcake::ResultCode::StateConflict);
  EXPECT(copy.image_id() == "untouched");

  const auto empty_destination =
      pixcake::EditDocument::copy_committed(files, kSourceDoc, "", color_limits(), copy);
  EXPECT(empty_destination.code == pixcake::ResultCode::StateConflict);
  EXPECT(copy.image_id() == "untouched");

  const auto same = pixcake::EditDocument::copy_committed(files, kSourceDoc, kSourceDoc, color_limits(), copy);
  EXPECT(same.code == pixcake::ResultCode::StateConflict);
  EXPECT(copy.image_id() == "untouched");
  EXPECT(files.files[kSourceDoc] == saved);
  EXPECT(files.files.size() == keys_before);
  EXPECT(files.files.find(std::string(kSourceDoc) + ".tmp") == files.files.end());
  EXPECT(files.files.find(std::string(kSourceDoc) + ".bak") == files.files.end());
  EXPECT(files.files.find(kCopyDoc) == files.files.end());
}

void test_absent_source_writes_nothing() {
  pixcake::test::FakeFileSystem files;
  pixcake::EditDocument copy = *pixcake::EditDocument::create("untouched");
  const auto result =
      pixcake::EditDocument::copy_committed(files, "edits/missing.pxedit", kCopyDoc, color_limits(), copy);
  EXPECT(result.absent);
  EXPECT(!result.code.has_value());
  EXPECT(copy.image_id() == "untouched");
  EXPECT(files.files.find(kCopyDoc) == files.files.end());
}

void test_corrupt_source_does_not_write_an_empty_copy() {
  pixcake::test::FakeFileSystem files;
  files.files[kSourceDoc] = {1, 2, 3};
  pixcake::EditDocument copy = *pixcake::EditDocument::create("untouched");
  const auto result = pixcake::EditDocument::copy_committed(files, kSourceDoc, kCopyDoc, color_limits(), copy);
  EXPECT(result.code == pixcake::ResultCode::DocumentCorrupt);
  EXPECT(result.recovered_empty);
  EXPECT(copy.image_id() == "untouched");
  EXPECT(!copy.recovered_empty());
  EXPECT(files.files.find(kCopyDoc) == files.files.end());
}

void test_backup_is_the_committed_document() {
  pixcake::test::FakeFileSystem files;
  auto document = *pixcake::EditDocument::create("img-1");
  EXPECT(document.set_preview("color.global", "exposure", "-1.50") == pixcake::ResultCode::Ok);
  EXPECT(document.commit_preview(0, "color.global", "exposure", color_limits(), files, kSourceDoc) ==
         pixcake::ResultCode::Ok);
  files.files[std::string(kSourceDoc) + ".bak"] = files.files[kSourceDoc];
  files.files.erase(kSourceDoc);
  pixcake::EditDocument copy = *pixcake::EditDocument::create("untouched");
  const auto result = pixcake::EditDocument::copy_committed(files, kSourceDoc, kCopyDoc, color_limits(), copy);
  EXPECT(result.code == pixcake::ResultCode::Ok);
  EXPECT(result.used_backup);
  EXPECT(copy.committed_parameter("color.global", "exposure") == "-1.50");
  EXPECT(copy.revision() == 1);
  EXPECT(files.files[kCopyDoc] == files.files[kSourceDoc]);
}

void test_disk_full_keeps_the_source() {
  pixcake::test::FakeFileSystem files;
  auto document = *pixcake::EditDocument::create("img-1");
  EXPECT(document.set_preview("color.global", "exposure", "-1.50") == pixcake::ResultCode::Ok);
  EXPECT(document.commit_preview(0, "color.global", "exposure", color_limits(), files, kSourceDoc) ==
         pixcake::ResultCode::Ok);
  const auto saved = files.files[kSourceDoc];
  files.write_error = pixcake::ResultCode::DiskFull;
  pixcake::EditDocument copy = *pixcake::EditDocument::create("untouched");
  const auto result = pixcake::EditDocument::copy_committed(files, kSourceDoc, kCopyDoc, color_limits(), copy);
  EXPECT(result.code == pixcake::ResultCode::DiskFull);
  EXPECT(copy.image_id() == "untouched");
  EXPECT(files.files[kSourceDoc] == saved);
  EXPECT(files.files.find(kCopyDoc) == files.files.end());
  EXPECT(files.files.find(std::string(kCopyDoc) + ".tmp") == files.files.end());
}

}  // namespace

int main() {
  test_sources_stay_in_their_modules();
  test_creating_a_copy_duplicates_the_committed_document();
  test_refuses_empty_or_same_path();
  test_absent_source_writes_nothing();
  test_corrupt_source_does_not_write_an_empty_copy();
  test_backup_is_the_committed_document();
  test_disk_full_keeps_the_source();
  return g_failures == 0 ? 0 : 1;
}
