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

void test_new_photo_has_empty_marks() {
  Fixture world;
  const pixcake::ProjectResult project = world.library->create_project("相册", pixcake::kOriginLocal, false);
  EXPECT(project.code == pixcake::ResultCode::Ok);
  const pixcake::ProjectResult photo =
      world.library->add_photo(project.id, "/pictures/a.png", "a.png", {1, 2, 0, 255});
  EXPECT(photo.code == pixcake::ResultCode::Ok);
  std::optional<pixcake::PhotoRow> loaded;
  EXPECT(world.library->load_photo(photo.id, loaded) == pixcake::ResultCode::Ok);
  EXPECT(loaded.has_value());
  EXPECT(loaded->rating == 0);
  EXPECT(loaded->flag.empty());
  EXPECT(loaded->color_label.empty());
  EXPECT(loaded->absolute_path == "/pictures/a.png");
  EXPECT(user_version(world.dir.path() + "/library.db") == 3);
}

void test_version_two_gains_default_marks() {
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
      "CREATE TABLE virtual_copy ("
      "id INTEGER PRIMARY KEY,"
      "project_id INTEGER NOT NULL,"
      "source_photo_id INTEGER NOT NULL,"
      "sequence INTEGER NOT NULL,"
      "document_id TEXT NOT NULL);"
      "INSERT INTO project(name, origin, cloud_marked, deleted_at) VALUES ('旧项目', '本地', 0, NULL);"
      "INSERT INTO photo(project_id, absolute_path, file_name, access_credential, presence) "
      "VALUES (1, '/tmp/old.png', 'old.png', X'0102', '可编辑');"
      "PRAGMA user_version=2;";
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
  EXPECT(photos[0].access_credential == std::vector<std::uint8_t>({1, 2}));
  EXPECT(photos[0].presence == "可编辑");
  EXPECT(photos[0].rating == 0);
  EXPECT(photos[0].flag.empty());
  EXPECT(photos[0].color_label.empty());
  store.reset();
  EXPECT(user_version(path) == 3);
}

}  // namespace

int main() {
  test_new_photo_has_empty_marks();
  test_version_two_gains_default_marks();
  return g_failures == 0 ? 0 : 1;
}
