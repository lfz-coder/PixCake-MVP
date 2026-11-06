#include "pixcake/project/project_library.hpp"
#include "pixcake/project/source_deleter.hpp"
#include "pixcake/storage/sqlite_project_store.hpp"
#include "pixcake/support/clock.hpp"
#include "pixcake/support/log_sink.hpp"
#include "support/admitted_photo.hpp"
#include "support/fake_file_system.hpp"
#include "support/temp_dir.hpp"

#include <sqlite3.h>

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

struct Fixture {
  pixcake::test::TempDir dir;
  std::unique_ptr<pixcake::SqliteProjectStore> store;
  FixedClock clock;
  pixcake::test::FakeFileSystem files;
  FakeDeleter deleter;
  MemoryLog log;
  std::unique_ptr<pixcake::ProjectLibrary> library;

  Fixture() { reopen(); }

  void reopen() {
    library.reset();
    store.reset();
    EXPECT(pixcake::SqliteProjectStore::open(dir.path() + "/library.db", store) == pixcake::ResultCode::Ok);
    library = std::make_unique<pixcake::ProjectLibrary>(*store, files, clock, deleter, &log);
  }
};


std::vector<std::uint8_t> tiny_jpeg() {
  // SOI + SOF0（高 3、宽 4、一个分量），足以通过准入的格式和尺寸检查。
  return {0xff, 0xd8, 0xff, 0xc0, 0x00, 0x0b, 0x08, 0x00, 0x03, 0x00, 0x04, 0x01, 0x01, 0x11, 0x00};
}

pixcake::AdmittedSource admitted_jpeg(pixcake::test::FakeFileSystem& files, const std::string& path) {
  files.files[path] = tiny_jpeg();
  std::optional<pixcake::AdmittedSource> admitted;
  EXPECT(pixcake::image_import::admit_source(files, path, admitted) == pixcake::ResultCode::Ok);
  if (!admitted) std::abort();
  return *admitted;
}

std::vector<std::string> names(const std::vector<pixcake::PhotoRow>& rows) {
  std::vector<std::string> out;
  for (const auto& row : rows) out.push_back(row.file_name);
  return out;
}

std::vector<std::string> query(Fixture& world, const pixcake::PhotoQuery& q) {
  std::vector<pixcake::PhotoRow> rows;
  EXPECT(world.library->query_photos(q, rows) == pixcake::ResultCode::Ok);
  return names(rows);
}

// P-12：格式取自准入时识别的内容格式，文件大小取自准入的文件身份。
void test_p12_admission_records_content_format() {
  Fixture world;
  const auto project = world.library->create_project("相册", pixcake::kOriginLocal, false);
  const auto png = world.library->add_photo(project.id, pixcake::test::admitted_png(world.files, "/p/a.png"), "a.png", {1});
  // 扩展名写错也按内容识别。
  const auto jpg = world.library->add_photo(project.id, admitted_jpeg(world.files, "/p/b.png"), "b.png", {1});
  std::optional<pixcake::PhotoRow> row;
  EXPECT(world.library->load_photo(png.id, row) == pixcake::ResultCode::Ok && row && row->format == "PNG");
  EXPECT(world.library->load_photo(jpg.id, row) == pixcake::ResultCode::Ok && row && row->format == "JPEG");
  EXPECT(row && row->source_identity.size == tiny_jpeg().size());
  EXPECT(row && row->edit_status.empty() && row->export_status.empty() && row->edited_at == 0 && row->capture_time == 0);
}

// P-12：需求 §6.2 筛选取交集，§6.3 排序可升可降；只返回本项目的图。
void test_p12_filter_and_sort_photos() {
  Fixture world;
  const auto project = world.library->create_project("相册", pixcake::kOriginLocal, false);
  const auto other = world.library->create_project("别的", pixcake::kOriginLocal, false);
  auto add = [&](std::int64_t pid, const char* name, bool jpeg) {
    const std::string path = std::string("/p/") + std::to_string(pid) + name;
    const auto source = jpeg ? admitted_jpeg(world.files, path) : pixcake::test::admitted_png(world.files, path);
    const auto added = world.library->add_photo(pid, source, name, {1});
    EXPECT(added.code == pixcake::ResultCode::Ok);
    return added.id;
  };
  const auto a = add(project.id, "a.png", false);
  const auto b = add(project.id, "b.jpg", true);
  const auto c = add(project.id, "c.png", false);
  const auto d = add(project.id, "d.jpg", true);
  add(other.id, "e.png", false);
  EXPECT(world.library->set_photo_ratings({a}, 5).code == pixcake::ResultCode::Ok);
  EXPECT(world.library->set_photo_ratings({b}, 2).code == pixcake::ResultCode::Ok);
  EXPECT(world.library->set_photo_ratings({c}, 4).code == pixcake::ResultCode::Ok);
  EXPECT(world.library->set_photo_flags({a, c}, pixcake::kFlagPick).code == pixcake::ResultCode::Ok);
  EXPECT(world.library->set_photo_colors({a, d}, pixcake::kColorRed).code == pixcake::ResultCode::Ok);
  EXPECT(world.library->mark_photo_edited(a, 100).code == pixcake::ResultCode::Ok);
  EXPECT(world.library->mark_photo_edited(c, 200).code == pixcake::ResultCode::Ok);
  EXPECT(world.library->mark_photo_exported(b).code == pixcake::ResultCode::Ok);

  pixcake::PhotoQuery q;
  q.project_id = project.id;
  EXPECT(query(world, q) == std::vector<std::string>({"a.png", "b.jpg", "c.png", "d.jpg"}));

  q.rating_min = 2;
  q.formats = {"PNG"};
  q.sort = pixcake::PhotoSortKey::FileName;
  q.descending = true;
  EXPECT(query(world, q) == std::vector<std::string>({"c.png", "a.png"}));

  pixcake::PhotoQuery marks;
  marks.project_id = project.id;
  marks.flags = {pixcake::kFlagPick};
  marks.colors = {pixcake::kColorRed};
  EXPECT(query(world, marks) == std::vector<std::string>({"a.png"}));

  pixcake::PhotoQuery equal;
  equal.project_id = project.id;
  equal.rating_min = 4;
  equal.rating_max = 4;
  EXPECT(query(world, equal) == std::vector<std::string>({"c.png"}));

  pixcake::PhotoQuery edited;
  edited.project_id = project.id;
  edited.edit_statuses = {pixcake::kEditStatusEdited};
  edited.sort = pixcake::PhotoSortKey::EditTime;
  edited.descending = true;
  EXPECT(query(world, edited) == std::vector<std::string>({"c.png", "a.png"}));

  pixcake::PhotoQuery unedited_or_exported;
  unedited_or_exported.project_id = project.id;
  unedited_or_exported.edit_statuses = {""};
  unedited_or_exported.export_statuses = {pixcake::kExportStatusExported};
  EXPECT(query(world, unedited_or_exported) == std::vector<std::string>({"b.jpg"}));

  pixcake::PhotoQuery by_type;
  by_type.project_id = project.id;
  by_type.sort = pixcake::PhotoSortKey::FileType;
  EXPECT(query(world, by_type) == std::vector<std::string>({"b.jpg", "d.jpg", "a.png", "c.png"}));

  pixcake::PhotoQuery by_rating;
  by_rating.project_id = project.id;
  by_rating.sort = pixcake::PhotoSortKey::Rating;
  by_rating.descending = true;
  EXPECT(query(world, by_rating) == std::vector<std::string>({"a.png", "c.png", "b.jpg", "d.jpg"}));

  pixcake::PhotoQuery everywhere;
  everywhere.formats = {"PNG"};
  EXPECT(query(world, everywhere).size() == 3);
}

void test_p12_query_rejects_out_of_range_conditions() {
  Fixture world;
  std::vector<pixcake::PhotoRow> rows;
  pixcake::PhotoQuery q;
  q.rating_min = 6;
  EXPECT(world.library->query_photos(q, rows) == pixcake::ResultCode::StateConflict);
  q = {};
  q.rating_min = 4;
  q.rating_max = 3;
  EXPECT(world.library->query_photos(q, rows) == pixcake::ResultCode::StateConflict);
  q = {};
  q.edit_statuses = {"半成品"};
  EXPECT(world.library->query_photos(q, rows) == pixcake::ResultCode::StateConflict);
  EXPECT(world.library->mark_photo_edited(999, 1).code != pixcake::ResultCode::Ok);
}

// P-12：没有新列的旧库打开后补列，照片不丢，格式按扩展名回填。
void test_p12_old_library_gains_columns() {
  pixcake::test::TempDir dir;
  const std::string path = dir.path() + "/library.db";
  sqlite3* raw = nullptr;
  EXPECT(sqlite3_open(path.c_str(), &raw) == SQLITE_OK);
  const char* schema =
      "CREATE TABLE project (id INTEGER PRIMARY KEY, name TEXT NOT NULL, origin TEXT NOT NULL,"
      "cloud_marked INTEGER NOT NULL, deleted_at INTEGER);"
      "CREATE TABLE photo (id INTEGER PRIMARY KEY, project_id INTEGER NOT NULL, absolute_path TEXT NOT NULL,"
      "file_name TEXT NOT NULL, access_credential BLOB NOT NULL, presence TEXT NOT NULL,"
      "source_identity_valid INTEGER NOT NULL DEFAULT 0, source_device INTEGER NOT NULL DEFAULT 0,"
      "source_inode INTEGER NOT NULL DEFAULT 0, source_size INTEGER NOT NULL DEFAULT 0,"
      "rating INTEGER NOT NULL DEFAULT 0, flag TEXT NOT NULL DEFAULT '', color_label TEXT NOT NULL DEFAULT '');"
      "CREATE TABLE trash (id INTEGER PRIMARY KEY, subject TEXT NOT NULL, subject_id INTEGER NOT NULL,"
      "deleted_at INTEGER NOT NULL);"
      "CREATE TABLE virtual_copy (id INTEGER PRIMARY KEY, project_id INTEGER NOT NULL,"
      "source_photo_id INTEGER NOT NULL, sequence INTEGER NOT NULL, document_id TEXT NOT NULL);"
      "INSERT INTO project(name, origin, cloud_marked, deleted_at) VALUES ('旧项目', '本地', 0, NULL);"
      "INSERT INTO photo(project_id, absolute_path, file_name, access_credential, presence, rating) "
      "VALUES (1, '/tmp/old.JPEG', 'old.JPEG', X'01', '可编辑', 3);"
      "INSERT INTO photo(project_id, absolute_path, file_name, access_credential, presence) "
      "VALUES (1, '/tmp/scan.tif', 'scan.tif', X'01', '可编辑');"
      "PRAGMA user_version=3;";
  EXPECT(sqlite3_exec(raw, schema, nullptr, nullptr, nullptr) == SQLITE_OK);
  sqlite3_close(raw);

  std::unique_ptr<pixcake::SqliteProjectStore> store;
  EXPECT(pixcake::SqliteProjectStore::open(path, store) == pixcake::ResultCode::Ok);
  std::vector<pixcake::PhotoRow> rows;
  pixcake::PhotoQuery q;
  q.formats = {"JPEG"};
  EXPECT(store->query_photos(q, rows) == pixcake::ResultCode::Ok);
  EXPECT(rows.size() == 1 && rows[0].file_name == "old.JPEG" && rows[0].rating == 3);
  q.formats = {"TIFF"};
  EXPECT(store->query_photos(q, rows) == pixcake::ResultCode::Ok);
  EXPECT(rows.size() == 1 && rows[0].file_name == "scan.tif" && rows[0].edit_status.empty());
}

bool plan_mentions(pixcake::SqliteProjectStore& store, const pixcake::PhotoQuery& q, const std::string& text) {
  std::vector<std::string> plan;
  EXPECT(store.explain_photo_query(q, plan) == pixcake::ResultCode::Ok);
  for (const auto& line : plan) {
    if (line.find(text) != std::string::npos) return true;
  }
  return false;
}

// P-12：按项目筛选并排序走 (project_id, 排序列) 复合索引，不建临时排序。
void test_p12_query_uses_project_indexes() {
  Fixture world;
  pixcake::PhotoQuery q;
  q.project_id = 1;
  const std::pair<pixcake::PhotoSortKey, const char*> keys[] = {
      {pixcake::PhotoSortKey::FileName, "idx_photo_project_file_name"},
      {pixcake::PhotoSortKey::EditTime, "idx_photo_project_edited_at"},
      {pixcake::PhotoSortKey::CaptureTime, "idx_photo_project_capture_time"},
      {pixcake::PhotoSortKey::Rating, "idx_photo_project_rating"},
      {pixcake::PhotoSortKey::FileType, "idx_photo_project_format"},
      {pixcake::PhotoSortKey::FileSize, "idx_photo_project_size"},
  };
  for (const auto& key : keys) {
    q.sort = key.first;
    q.descending = key.first == pixcake::PhotoSortKey::EditTime;
    EXPECT(plan_mentions(*world.store, q, key.second));
    EXPECT(!plan_mentions(*world.store, q, "TEMP B-TREE"));
  }
  pixcake::PhotoQuery exported;
  exported.project_id = 1;
  exported.export_statuses = {pixcake::kExportStatusExported};
  EXPECT(plan_mentions(*world.store, exported, "idx_photo_project_export_status"));
  pixcake::PhotoQuery edited;
  edited.project_id = 1;
  edited.edit_statuses = {pixcake::kEditStatusEdited};
  EXPECT(plan_mentions(*world.store, edited, "idx_photo_project_edit_status"));
}

}  // namespace

int main() {
  test_p12_admission_records_content_format();
  test_p12_filter_and_sort_photos();
  test_p12_query_rejects_out_of_range_conditions();
  test_p12_old_library_gains_columns();
  test_p12_query_uses_project_indexes();
  if (g_failures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  return 0;
}
