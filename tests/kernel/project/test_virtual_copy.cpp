#include "pixcake/project/project_library.hpp"
#include "pixcake/project/source_deleter.hpp"
#include "pixcake/storage/sqlite_project_store.hpp"
#include "pixcake/support/clock.hpp"
#include "pixcake/support/log_sink.hpp"
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
#include <utility>
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
  std::int64_t now = 1000000;
  std::int64_t unix_seconds() const override { return now; }
};

struct MemoryLog : pixcake::LogSink {
  void write(const pixcake::LogRecord&) override {}
};

struct FakeDeleter : pixcake::SourceDeleter {
  int calls = 0;
  pixcake::ResultCode next = pixcake::ResultCode::Ok;
  std::string last;

  pixcake::ResultCode remove_source(const std::string& path) override {
    ++calls;
    last = path;
    return next;
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

int user_version(const std::string& path) {
  sqlite3* raw = nullptr;
  if (sqlite3_open(path.c_str(), &raw) != SQLITE_OK) {
    if (raw != nullptr) {
      sqlite3_close(raw);
    }
    return -1;
  }
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(raw, "PRAGMA user_version;", -1, &stmt, nullptr) != SQLITE_OK) {
    sqlite3_close(raw);
    return -1;
  }
  const int rc = sqlite3_step(stmt);
  const int version = rc == SQLITE_ROW ? sqlite3_column_int(stmt, 0) : -1;
  sqlite3_finalize(stmt);
  sqlite3_close(raw);
  return version;
}

std::int64_t add_project_photo(Fixture& world, const char* path, const char* name) {
  const pixcake::ProjectResult project = world.library->create_project("相册", pixcake::kOriginLocal, false);
  EXPECT(project.code == pixcake::ResultCode::Ok);
  const pixcake::ProjectResult photo = world.library->add_photo(project.id, path, name, {1, 2, 0, 255});
  EXPECT(photo.code == pixcake::ResultCode::Ok);
  return photo.id;
}

void test_copy_points_at_original_and_keeps_its_document_id() {
  Fixture world;
  const std::vector<std::uint8_t> original = {1, 2, 3};
  const std::vector<std::uint8_t> document = {9, 9, 9};
  world.files.files["/pictures/a.png"] = original;
  world.files.files["/params/a.pxed"] = document;
  const std::int64_t photo_id = add_project_photo(world, "/pictures/a.png", "a.png");

  const pixcake::ProjectResult first = world.library->create_virtual_copy(photo_id, "doc-1");
  EXPECT(first.code == pixcake::ResultCode::Ok);
  const pixcake::ProjectResult second = world.library->create_virtual_copy(photo_id, "a'b");
  EXPECT(second.code == pixcake::ResultCode::Ok);
  EXPECT(world.deleter.calls == 0);
  EXPECT(world.files.files["/pictures/a.png"] == original);
  EXPECT(world.files.files["/params/a.pxed"] == document);

  std::vector<pixcake::PhotoRow> photos;
  EXPECT(world.library->list_photos(std::nullopt, photos) == pixcake::ResultCode::Ok);
  EXPECT(photos.size() == 1);

  std::vector<pixcake::VirtualCopyRow> copies;
  EXPECT(world.library->list_virtual_copies(photo_id, copies) == pixcake::ResultCode::Ok);
  EXPECT(copies.size() == 2);
  EXPECT(copies[0].id == first.id);
  EXPECT(copies[0].source_photo_id == photo_id);
  EXPECT(copies[0].project_id == photos[0].project_id);
  EXPECT(copies[0].sequence == 1);
  EXPECT(copies[0].document_id == "doc-1");
  EXPECT(copies[1].id == second.id);
  EXPECT(copies[1].sequence == 2);
  EXPECT(copies[1].document_id == "a'b");

  world.reopen();
  EXPECT(world.library->list_virtual_copies(photo_id, copies) == pixcake::ResultCode::Ok);
  EXPECT(copies.size() == 2);
  EXPECT(copies[0].sequence == 1);
  EXPECT(copies[0].document_id == "doc-1");
  EXPECT(copies[1].sequence == 2);
  EXPECT(copies[1].document_id == "a'b");
  EXPECT(user_version(world.dir.path() + "/library.db") == 2);
}

void test_rejected_copy_writes_nothing() {
  Fixture world;
  const std::int64_t photo_id = add_project_photo(world, "/pictures/a.png", "a.png");
  EXPECT(world.library->create_virtual_copy(photo_id, "doc-1").code == pixcake::ResultCode::Ok);

  EXPECT(world.library->create_virtual_copy(photo_id, "").code == pixcake::ResultCode::StateConflict);
  EXPECT(world.library->create_virtual_copy(photo_id, "doc-1").code == pixcake::ResultCode::StateConflict);
  EXPECT(world.library->create_virtual_copy(999, "doc-x").code == pixcake::ResultCode::StateConflict);

  EXPECT(world.library->trash_photo(photo_id).code == pixcake::ResultCode::Ok);
  EXPECT(world.library->create_virtual_copy(photo_id, "doc-2").code == pixcake::ResultCode::StateConflict);

  Fixture trashed_project;
  const pixcake::ProjectResult project =
      trashed_project.library->create_project("回收", pixcake::kOriginLocal, false);
  EXPECT(project.code == pixcake::ResultCode::Ok);
  const pixcake::ProjectResult photo =
      trashed_project.library->add_photo(project.id, "/pictures/b.png", "b.png", {4});
  EXPECT(photo.code == pixcake::ResultCode::Ok);
  EXPECT(trashed_project.library->trash_project(project.id).code == pixcake::ResultCode::Ok);
  EXPECT(trashed_project.library->create_virtual_copy(photo.id, "doc-p").code == pixcake::ResultCode::StateConflict);
  std::vector<pixcake::VirtualCopyRow> none;
  EXPECT(trashed_project.library->list_virtual_copies(photo.id, none) == pixcake::ResultCode::Ok);
  EXPECT(none.empty());

  std::vector<pixcake::VirtualCopyRow> copies;
  EXPECT(world.library->list_virtual_copies(photo_id, copies) == pixcake::ResultCode::Ok);
  EXPECT(copies.size() == 1);
  EXPECT(copies[0].sequence == 1);
  EXPECT(copies[0].document_id == "doc-1");
}

void test_version_one_library_keeps_photos_and_gains_copies() {
  pixcake::test::TempDir dir;
  const std::string path = dir.path() + "/library.db";
  sqlite3* raw = nullptr;
  EXPECT(sqlite3_open(path.c_str(), &raw) == SQLITE_OK);
  const char* schema =
      "CREATE TABLE project ("
      "id INTEGER PRIMARY KEY,"
      "name TEXT NOT NULL,"
      "origin TEXT NOT NULL,"
      "cloud_marked INTEGER NOT NULL,"
      "deleted_at INTEGER);"
      "CREATE TABLE photo ("
      "id INTEGER PRIMARY KEY,"
      "project_id INTEGER NOT NULL,"
      "absolute_path TEXT NOT NULL,"
      "file_name TEXT NOT NULL,"
      "access_credential BLOB NOT NULL,"
      "presence TEXT NOT NULL);"
      "CREATE TABLE trash ("
      "id INTEGER PRIMARY KEY,"
      "subject TEXT NOT NULL,"
      "subject_id INTEGER NOT NULL,"
      "deleted_at INTEGER NOT NULL);"
      "INSERT INTO project(name, origin, cloud_marked, deleted_at) VALUES ('旧项目', '本地', 0, NULL);"
      "INSERT INTO photo(project_id, absolute_path, file_name, access_credential, presence) "
      "VALUES (1, '/tmp/old.png', 'old.png', X'0102', '可编辑');"
      "PRAGMA user_version=1;";
  EXPECT(sqlite3_exec(raw, schema, nullptr, nullptr, nullptr) == SQLITE_OK);
  sqlite3_close(raw);

  std::unique_ptr<pixcake::SqliteProjectStore> store;
  EXPECT(pixcake::SqliteProjectStore::open(path, store) == pixcake::ResultCode::Ok);
  FixedClock clock;
  pixcake::test::FakeFileSystem files;
  FakeDeleter deleter;
  pixcake::ProjectLibrary library(*store, files, clock, deleter, nullptr);
  std::vector<pixcake::PhotoRow> photos;
  EXPECT(library.list_photos(std::nullopt, photos) == pixcake::ResultCode::Ok);
  EXPECT(photos.size() == 1);
  EXPECT(photos[0].absolute_path == "/tmp/old.png");
  EXPECT(photos[0].file_name == "old.png");
  EXPECT(photos[0].access_credential == std::vector<std::uint8_t>({1, 2}));
  EXPECT(photos[0].presence == "可编辑");
  std::vector<pixcake::ProjectRow> projects;
  EXPECT(library.list_projects(projects) == pixcake::ResultCode::Ok);
  EXPECT(projects.size() == 1);
  EXPECT(projects[0].name == "旧项目");

  const pixcake::ProjectResult copy = library.create_virtual_copy(photos[0].id, "old-doc");
  EXPECT(copy.code == pixcake::ResultCode::Ok);
  store.reset();
  EXPECT(user_version(path) == 2);
  EXPECT(pixcake::SqliteProjectStore::open(path, store) == pixcake::ResultCode::Ok);
  pixcake::ProjectLibrary reopened(*store, files, clock, deleter, nullptr);
  std::vector<pixcake::VirtualCopyRow> copies;
  EXPECT(reopened.list_virtual_copies(photos[0].id, copies) == pixcake::ResultCode::Ok);
  EXPECT(copies.size() == 1);
  EXPECT(copies[0].document_id == "old-doc");
  EXPECT(reopened.list_photos(std::nullopt, photos) == pixcake::ResultCode::Ok);
  EXPECT(photos.size() == 1);
  EXPECT(photos[0].access_credential == std::vector<std::uint8_t>({1, 2}));
}

void test_shares_original_without_a_charge() {
  Fixture world;
  const pixcake::ProjectResult project = world.library->create_project("相册", pixcake::kOriginLocal, false);
  EXPECT(project.code == pixcake::ResultCode::Ok);
  const pixcake::ProjectResult photo_a = world.library->add_photo(project.id, "/pictures/a.png", "a.png", {1});
  const pixcake::ProjectResult photo_b = world.library->add_photo(project.id, "/pictures/b.png", "b.png", {2});
  EXPECT(photo_a.code == pixcake::ResultCode::Ok);
  EXPECT(photo_b.code == pixcake::ResultCode::Ok);
  const pixcake::ProjectResult copy_a = world.library->create_virtual_copy(photo_a.id, "doc-a1");
  const pixcake::ProjectResult copy_b = world.library->create_virtual_copy(photo_a.id, "doc-a2");
  EXPECT(copy_a.code == pixcake::ResultCode::Ok);
  EXPECT(copy_b.code == pixcake::ResultCode::Ok);

  const pixcake::ProjectResult other = world.library->create_project("另一本", pixcake::kOriginLocal, false);
  EXPECT(other.code == pixcake::ResultCode::Ok);
  const pixcake::ProjectResult photo_c = world.library->add_photo(other.id, "/pictures/c.png", "c.png", {3});
  EXPECT(photo_c.code == pixcake::ResultCode::Ok);

  bool shares = false;
  EXPECT(world.library->shares_original(pixcake::kTrashPhoto, photo_a.id, pixcake::kTrashVirtualCopy, copy_a.id,
                                        shares) == pixcake::ResultCode::Ok);
  EXPECT(shares);
  EXPECT(world.library->shares_original(pixcake::kTrashPhoto, photo_a.id, pixcake::kTrashVirtualCopy, copy_b.id,
                                        shares) == pixcake::ResultCode::Ok);
  EXPECT(shares);
  EXPECT(world.library->shares_original(pixcake::kTrashVirtualCopy, copy_a.id, pixcake::kTrashVirtualCopy, copy_b.id,
                                        shares) == pixcake::ResultCode::Ok);
  EXPECT(shares);
  EXPECT(world.library->shares_original(pixcake::kTrashPhoto, photo_a.id, pixcake::kTrashPhoto, photo_a.id, shares) ==
         pixcake::ResultCode::Ok);
  EXPECT(shares);
  EXPECT(world.library->shares_original(pixcake::kTrashPhoto, photo_a.id, pixcake::kTrashPhoto, photo_b.id, shares) ==
         pixcake::ResultCode::Ok);
  EXPECT(!shares);
  EXPECT(world.library->shares_original(pixcake::kTrashVirtualCopy, copy_a.id, pixcake::kTrashPhoto, photo_b.id,
                                        shares) == pixcake::ResultCode::Ok);
  EXPECT(!shares);
  EXPECT(world.library->shares_original(pixcake::kTrashPhoto, photo_c.id, pixcake::kTrashVirtualCopy, copy_a.id,
                                        shares) == pixcake::ResultCode::Ok);
  EXPECT(!shares);

  shares = true;
  EXPECT(world.library->shares_original(pixcake::kTrashProject, photo_a.id, pixcake::kTrashPhoto, photo_a.id,
                                        shares) == pixcake::ResultCode::StateConflict);
  EXPECT(!shares);
  shares = true;
  EXPECT(world.library->shares_original(pixcake::kTrashPhoto, 999, pixcake::kTrashPhoto, photo_a.id, shares) ==
         pixcake::ResultCode::StateConflict);
  EXPECT(!shares);
  shares = true;
  EXPECT(world.library->shares_original(pixcake::kTrashVirtualCopy, 999, pixcake::kTrashPhoto, photo_a.id, shares) ==
         pixcake::ResultCode::StateConflict);
  EXPECT(!shares);

  const std::filesystem::path project_dir =
      std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path() / "kernel" / "project";
  EXPECT(std::filesystem::is_directory(project_dir));
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::recursive_directory_iterator(project_dir)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    std::ifstream input(entry.path());
    const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    EXPECT(text.find("扣张") == std::string::npos);
  }
}

}  // namespace

int main() {
  test_copy_points_at_original_and_keeps_its_document_id();
  test_rejected_copy_writes_nothing();
  test_version_one_library_keeps_photos_and_gains_copies();
  test_shares_original_without_a_charge();
  return g_failures == 0 ? 0 : 1;
}
