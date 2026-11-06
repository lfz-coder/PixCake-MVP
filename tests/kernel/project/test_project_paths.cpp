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
#include <fstream>
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

struct SilentLog : pixcake::LogSink {
  void write(const pixcake::LogRecord&) override {}
};

struct UnusedDeleter : pixcake::SourceDeleter {
  pixcake::ResultCode remove_source(const std::string&) override { return pixcake::ResultCode::StateConflict; }
};

std::string read_prefix(const std::string& path, std::size_t count) {
  std::ifstream in(path, std::ios::binary);
  std::string bytes(count, '\0');
  in.read(bytes.data(), static_cast<std::streamsize>(count));
  bytes.resize(static_cast<std::size_t>(in.gcount()));
  return bytes;
}

}  // namespace

int main() {
  pixcake::test::TempDir dir;
  const std::string db_path = dir.path() + "/library.db";
  std::unique_ptr<pixcake::SqliteProjectStore> store;
  EXPECT(pixcake::SqliteProjectStore::open(db_path, store) == pixcake::ResultCode::Ok);
  EXPECT(store != nullptr);
  EXPECT(store->journal_mode() == "wal");

  FixedClock clock;
  SilentLog log;
  UnusedDeleter deleter;
  pixcake::test::FakeFileSystem files;
  pixcake::ProjectLibrary library(*store, files, clock, deleter, &log);

  const pixcake::ProjectResult created = library.create_project("a'b", "本地", false);
  EXPECT(created.code == pixcake::ResultCode::Ok);
  std::vector<pixcake::ProjectRow> projects;
  EXPECT(library.list_projects(projects) == pixcake::ResultCode::Ok);
  EXPECT(projects.size() == 1);
  EXPECT(projects[0].name == "a'b");
  EXPECT(projects[0].origin == "本地");
  EXPECT(!projects[0].cloud_marked);
  EXPECT(!projects[0].deleted_at.has_value());

  const pixcake::ProjectResult bad_origin = library.create_project("x", "云", false);
  EXPECT(bad_origin.code == pixcake::ResultCode::StateConflict);
  EXPECT(library.list_projects(projects) == pixcake::ResultCode::Ok);
  EXPECT(projects.size() == 1);

  // 相对路径在准入阶段就被拒绝，项目库收不到凭证。
  std::optional<pixcake::AdmittedSource> relative;
  files.files["photos/a.jpg"] = pixcake::test::tiny_png();
  EXPECT(pixcake::image_import::admit_source(files, "photos/a.jpg", relative) == pixcake::ResultCode::StateConflict);
  EXPECT(!relative.has_value());
  std::vector<pixcake::PhotoRow> photos;
  EXPECT(library.list_photos(std::nullopt, photos) == pixcake::ResultCode::Ok);
  EXPECT(photos.empty());

  const pixcake::ProjectResult nameless = library.add_photo(created.id, pixcake::test::admitted_png(files, "/tmp/pic.png"), "", {1});
  EXPECT(nameless.code == pixcake::ResultCode::StateConflict);

  const std::vector<std::uint8_t> credential{1, 2, 0, 255};
  const pixcake::ProjectResult added =
      library.add_photo(created.id, pixcake::test::admitted_png(files, "/tmp/pic.png"), "pic.png", credential);
  EXPECT(added.code == pixcake::ResultCode::Ok);
  EXPECT(added.code != pixcake::ResultCode::SourceMissing);
  EXPECT(library.list_photos(std::nullopt, photos) == pixcake::ResultCode::Ok);
  EXPECT(photos.size() == 1);
  EXPECT(photos[0].absolute_path == "/tmp/pic.png");
  EXPECT(photos[0].file_name == "pic.png");
  EXPECT(photos[0].access_credential == credential);
  EXPECT(photos[0].presence == "可编辑");

  const pixcake::ProjectResult empty_credential =
      library.add_photo(created.id, pixcake::test::admitted_png(files, "/tmp/empty.png"), "empty.png", {});
  EXPECT(empty_credential.code == pixcake::ResultCode::Ok);
  std::optional<pixcake::PhotoRow> loaded;
  EXPECT(library.load_photo(empty_credential.id, loaded) == pixcake::ResultCode::Ok);
  EXPECT(loaded.has_value());
  EXPECT(loaded->access_credential.empty());
  EXPECT(loaded->presence == "可编辑");

  store.reset();
  EXPECT(pixcake::SqliteProjectStore::open(db_path, store) == pixcake::ResultCode::Ok);
  EXPECT(store->journal_mode() == "wal");
  pixcake::ProjectLibrary reopened(*store, files, clock, deleter, nullptr);
  EXPECT(reopened.list_projects(projects) == pixcake::ResultCode::Ok);
  EXPECT(projects.size() == 1);
  EXPECT(projects[0].name == "a'b");
  EXPECT(reopened.list_photos(std::nullopt, photos) == pixcake::ResultCode::Ok);
  EXPECT(photos.size() == 2);
  EXPECT(photos[0].access_credential == credential);

  pixcake::test::TempDir alien_dir;
  const std::string alien_path = alien_dir.path() + "/library.db";
  {
    std::ofstream out(alien_path, std::ios::binary);
    out << "hello";
  }
  std::unique_ptr<pixcake::SqliteProjectStore> alien;
  EXPECT(pixcake::SqliteProjectStore::open(alien_path, alien) == pixcake::ResultCode::IoFailed);
  EXPECT(alien == nullptr);
  EXPECT(read_prefix(alien_path, 5) == "hello");

  pixcake::test::TempDir future_dir;
  const std::string future_path = future_dir.path() + "/library.db";
  sqlite3* raw = nullptr;
  EXPECT(sqlite3_open(future_path.c_str(), &raw) == SQLITE_OK);
  EXPECT(sqlite3_exec(raw, "PRAGMA user_version=4;", nullptr, nullptr, nullptr) == SQLITE_OK);
  EXPECT(sqlite3_exec(raw, "CREATE TABLE marker(id INTEGER);", nullptr, nullptr, nullptr) == SQLITE_OK);
  sqlite3_close(raw);
  std::unique_ptr<pixcake::SqliteProjectStore> future;
  EXPECT(pixcake::SqliteProjectStore::open(future_path, future) == pixcake::ResultCode::StateConflict);
  EXPECT(future == nullptr);
  EXPECT(sqlite3_open(future_path.c_str(), &raw) == SQLITE_OK);
  sqlite3_stmt* version_stmt = nullptr;
  EXPECT(sqlite3_prepare_v2(raw, "PRAGMA user_version;", -1, &version_stmt, nullptr) == SQLITE_OK);
  EXPECT(sqlite3_step(version_stmt) == SQLITE_ROW);
  EXPECT(sqlite3_column_int(version_stmt, 0) == 4);
  sqlite3_finalize(version_stmt);
  sqlite3_stmt* marker_stmt = nullptr;
  EXPECT(sqlite3_prepare_v2(raw, "SELECT name FROM sqlite_master WHERE name = ?;", -1, &marker_stmt,
                            nullptr) == SQLITE_OK);
  EXPECT(sqlite3_bind_text(marker_stmt, 1, "marker", -1, SQLITE_TRANSIENT) == SQLITE_OK);
  EXPECT(sqlite3_step(marker_stmt) == SQLITE_ROW);
  sqlite3_finalize(marker_stmt);
  sqlite3_close(raw);

  return g_failures == 0 ? 0 : 1;
}
