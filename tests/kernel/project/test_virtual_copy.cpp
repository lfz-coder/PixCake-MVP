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
  EXPECT(user_version(world.dir.path() + "/library.db") == 3);
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
  EXPECT(photos[0].rating == 0);
  EXPECT(photos[0].flag.empty());
  EXPECT(photos[0].color_label.empty());
  std::vector<pixcake::ProjectRow> projects;
  EXPECT(library.list_projects(projects) == pixcake::ResultCode::Ok);
  EXPECT(projects.size() == 1);
  EXPECT(projects[0].name == "旧项目");

  const pixcake::ProjectResult copy = library.create_virtual_copy(photos[0].id, "old-doc");
  EXPECT(copy.code == pixcake::ResultCode::Ok);
  store.reset();
  EXPECT(user_version(path) == 3);
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

std::int64_t deleted_at(Fixture& world, const char* subject, std::int64_t id) {
  std::optional<pixcake::TrashRow> row;
  EXPECT(world.library->load_trash(subject, id, row) == pixcake::ResultCode::Ok);
  EXPECT(row.has_value());
  return row.has_value() ? row->deleted_at : -1;
}

bool in_trash(Fixture& world, const char* subject, std::int64_t id) {
  std::optional<pixcake::TrashRow> row;
  EXPECT(world.library->load_trash(subject, id, row) == pixcake::ResultCode::Ok);
  return row.has_value();
}

void test_trashing_a_copy_leaves_the_original() {
  Fixture world;
  const std::int64_t photo_id = add_project_photo(world, "/pictures/a.png", "a.png");
  const pixcake::ProjectResult first = world.library->create_virtual_copy(photo_id, "doc-1");
  const pixcake::ProjectResult second = world.library->create_virtual_copy(photo_id, "doc-2");
  EXPECT(first.code == pixcake::ResultCode::Ok);
  EXPECT(second.code == pixcake::ResultCode::Ok);

  EXPECT(world.library->trash_virtual_copy(first.id).code == pixcake::ResultCode::Ok);
  EXPECT(in_trash(world, pixcake::kTrashVirtualCopy, first.id));
  EXPECT(!in_trash(world, pixcake::kTrashPhoto, photo_id));
  EXPECT(!in_trash(world, pixcake::kTrashVirtualCopy, second.id));
  const std::int64_t kept = deleted_at(world, pixcake::kTrashVirtualCopy, first.id);
  world.clock.now += 50;
  EXPECT(world.library->trash_virtual_copy(first.id).code == pixcake::ResultCode::StateConflict);
  EXPECT(deleted_at(world, pixcake::kTrashVirtualCopy, first.id) == kept);
  EXPECT(world.deleter.calls == 0);
}

void test_trashing_the_original_takes_copies_with_it() {
  Fixture world;
  world.files.files["/pictures/a.png"] = {7, 8};
  const std::int64_t photo_id = add_project_photo(world, "/pictures/a.png", "a.png");
  const pixcake::ProjectResult early = world.library->create_virtual_copy(photo_id, "doc-early");
  const pixcake::ProjectResult later = world.library->create_virtual_copy(photo_id, "doc-later");
  EXPECT(early.code == pixcake::ResultCode::Ok);
  EXPECT(later.code == pixcake::ResultCode::Ok);
  EXPECT(world.library->trash_virtual_copy(early.id).code == pixcake::ResultCode::Ok);
  const std::int64_t early_time = deleted_at(world, pixcake::kTrashVirtualCopy, early.id);
  world.clock.now += 80;
  EXPECT(world.library->trash_photo(photo_id).code == pixcake::ResultCode::Ok);
  EXPECT(deleted_at(world, pixcake::kTrashPhoto, photo_id) == world.clock.now);
  EXPECT(deleted_at(world, pixcake::kTrashVirtualCopy, later.id) == world.clock.now);
  EXPECT(deleted_at(world, pixcake::kTrashVirtualCopy, early.id) == early_time);

  Fixture project_world;
  const pixcake::ProjectResult project =
      project_world.library->create_project("整本", pixcake::kOriginLocal, false);
  EXPECT(project.code == pixcake::ResultCode::Ok);
  const pixcake::ProjectResult photo =
      project_world.library->add_photo(project.id, "/pictures/b.png", "b.png", {1});
  EXPECT(photo.code == pixcake::ResultCode::Ok);
  const pixcake::ProjectResult kept = project_world.library->create_virtual_copy(photo.id, "kept");
  const pixcake::ProjectResult moved = project_world.library->create_virtual_copy(photo.id, "moved");
  EXPECT(kept.code == pixcake::ResultCode::Ok);
  EXPECT(moved.code == pixcake::ResultCode::Ok);
  EXPECT(project_world.library->trash_virtual_copy(kept.id).code == pixcake::ResultCode::Ok);
  const std::int64_t kept_time = deleted_at(project_world, pixcake::kTrashVirtualCopy, kept.id);
  project_world.clock.now += 20;
  EXPECT(project_world.library->trash_project(project.id).code == pixcake::ResultCode::Ok);
  EXPECT(deleted_at(project_world, pixcake::kTrashProject, project.id) == project_world.clock.now);
  EXPECT(deleted_at(project_world, pixcake::kTrashPhoto, photo.id) == project_world.clock.now);
  EXPECT(deleted_at(project_world, pixcake::kTrashVirtualCopy, moved.id) == project_world.clock.now);
  EXPECT(deleted_at(project_world, pixcake::kTrashVirtualCopy, kept.id) == kept_time);
}

void test_delete_source_file_trashes_copies_without_a_second_path() {
  Fixture world;
  world.files.files["/pictures/a.png"] = {1, 2, 3};
  world.files.files["/pictures/other.png"] = {4};
  const std::int64_t photo_id = add_project_photo(world, "/pictures/a.png", "a.png");
  const pixcake::ProjectResult copy = world.library->create_virtual_copy(photo_id, "doc-1");
  EXPECT(copy.code == pixcake::ResultCode::Ok);
  const pixcake::ProjectResult removed = world.library->delete_source_file(photo_id);
  EXPECT(removed.code == pixcake::ResultCode::Ok);
  EXPECT(removed.notice.empty());
  EXPECT(world.deleter.calls == 1);
  EXPECT(world.deleter.last == "/pictures/a.png");
  EXPECT(world.files.files["/pictures/other.png"] == std::vector<std::uint8_t>({4}));
  EXPECT(in_trash(world, pixcake::kTrashPhoto, photo_id));
  EXPECT(in_trash(world, pixcake::kTrashVirtualCopy, copy.id));
  std::optional<pixcake::PhotoRow> photo;
  EXPECT(world.library->load_photo(photo_id, photo) == pixcake::ResultCode::Ok);
  EXPECT(photo.has_value());
  EXPECT(photo->presence == pixcake::kPresenceMissing);

  Fixture denied;
  denied.files.files["/pictures/a.png"] = {9};
  denied.deleter.next = pixcake::ResultCode::PermissionDenied;
  const std::int64_t denied_photo = add_project_photo(denied, "/pictures/a.png", "a.png");
  const pixcake::ProjectResult denied_copy = denied.library->create_virtual_copy(denied_photo, "doc-denied");
  EXPECT(denied_copy.code == pixcake::ResultCode::Ok);
  const pixcake::ProjectResult denial = denied.library->delete_source_file(denied_photo);
  EXPECT(denial.code == pixcake::ResultCode::PermissionDenied);
  EXPECT(denial.notice == pixcake::kDiskFileRemains);
  EXPECT(denied.files.files["/pictures/a.png"] == std::vector<std::uint8_t>({9}));
  EXPECT(denied.deleter.calls == 1);
  EXPECT(in_trash(denied, pixcake::kTrashPhoto, denied_photo));
  EXPECT(in_trash(denied, pixcake::kTrashVirtualCopy, denied_copy.id));
  std::optional<pixcake::PhotoRow> still;
  EXPECT(denied.library->load_photo(denied_photo, still) == pixcake::ResultCode::Ok);
  EXPECT(still.has_value());
  EXPECT(still->presence == pixcake::kPresenceEditable);

  Fixture again;
  const std::int64_t again_photo = add_project_photo(again, "/pictures/a.png", "a.png");
  const pixcake::ProjectResult again_copy = again.library->create_virtual_copy(again_photo, "doc-again");
  EXPECT(again_copy.code == pixcake::ResultCode::Ok);
  EXPECT(again.library->trash_photo(again_photo).code == pixcake::ResultCode::Ok);
  const std::int64_t copy_time = deleted_at(again, pixcake::kTrashVirtualCopy, again_copy.id);
  again.clock.now += 30;
  EXPECT(again.library->delete_source_file(again_photo).code == pixcake::ResultCode::Ok);
  EXPECT(deleted_at(again, pixcake::kTrashVirtualCopy, again_copy.id) == copy_time);
  EXPECT(deleted_at(again, pixcake::kTrashPhoto, again_photo) == copy_time);
}

void test_expired_copy_index_is_removed_without_the_file() {
  Fixture world;
  world.files.files["/pictures/a.png"] = {3, 3, 3};
  const std::int64_t photo_id = add_project_photo(world, "/pictures/a.png", "a.png");
  const pixcake::ProjectResult copy = world.library->create_virtual_copy(photo_id, "doc-1");
  EXPECT(copy.code == pixcake::ResultCode::Ok);
  EXPECT(world.library->trash_virtual_copy(copy.id).code == pixcake::ResultCode::Ok);
  const std::int64_t trashed_at = world.clock.now;
  world.clock.now = trashed_at + 2591999;
  EXPECT(world.library->startup() == pixcake::ResultCode::Ok);
  std::optional<pixcake::VirtualCopyRow> loaded;
  EXPECT(world.library->load_virtual_copy(copy.id, loaded) == pixcake::ResultCode::Ok);
  EXPECT(loaded.has_value());
  EXPECT(world.files.files["/pictures/a.png"] == std::vector<std::uint8_t>({3, 3, 3}));
  EXPECT(world.deleter.calls == 0);

  world.clock.now = trashed_at + 2592000;
  EXPECT(world.library->startup() == pixcake::ResultCode::Ok);
  EXPECT(world.library->load_virtual_copy(copy.id, loaded) == pixcake::ResultCode::Ok);
  EXPECT(!loaded.has_value());
  std::optional<pixcake::PhotoRow> photo;
  EXPECT(world.library->load_photo(photo_id, photo) == pixcake::ResultCode::Ok);
  EXPECT(photo.has_value());
  EXPECT(world.files.files["/pictures/a.png"] == std::vector<std::uint8_t>({3, 3, 3}));
  EXPECT(world.deleter.calls == 0);

  Fixture expired_photo;
  expired_photo.files.files["/pictures/a.png"] = {5};
  const std::int64_t expired_id = add_project_photo(expired_photo, "/pictures/a.png", "a.png");
  const pixcake::ProjectResult expired_copy = expired_photo.library->create_virtual_copy(expired_id, "doc-photo");
  EXPECT(expired_copy.code == pixcake::ResultCode::Ok);
  EXPECT(expired_photo.library->trash_photo(expired_id).code == pixcake::ResultCode::Ok);
  expired_photo.clock.now += 2592000;
  EXPECT(expired_photo.library->startup() == pixcake::ResultCode::Ok);
  std::vector<pixcake::PhotoRow> photos;
  EXPECT(expired_photo.library->list_photos(std::nullopt, photos) == pixcake::ResultCode::Ok);
  EXPECT(photos.empty());
  std::vector<pixcake::VirtualCopyRow> copies;
  EXPECT(expired_photo.library->list_virtual_copies(expired_id, copies) == pixcake::ResultCode::Ok);
  EXPECT(copies.empty());
  EXPECT(expired_photo.files.files["/pictures/a.png"] == std::vector<std::uint8_t>({5}));
  EXPECT(expired_photo.deleter.calls == 0);

  Fixture erased;
  erased.files.files["/pictures/a.png"] = {6};
  const pixcake::ProjectResult project = erased.library->create_project("清掉", pixcake::kOriginLocal, false);
  EXPECT(project.code == pixcake::ResultCode::Ok);
  const pixcake::ProjectResult erased_photo = erased.library->add_photo(project.id, "/pictures/a.png", "a.png", {1});
  EXPECT(erased_photo.code == pixcake::ResultCode::Ok);
  EXPECT(erased.library->create_virtual_copy(erased_photo.id, "doc-erased").code == pixcake::ResultCode::Ok);
  EXPECT(erased.library->trash_project(project.id).code == pixcake::ResultCode::Ok);
  EXPECT(erased.library->erase_trashed_project(project.id).code == pixcake::ResultCode::Ok);
  EXPECT(erased.library->list_virtual_copies(erased_photo.id, copies) == pixcake::ResultCode::Ok);
  EXPECT(copies.empty());
  EXPECT(erased.library->list_photos(std::nullopt, photos) == pixcake::ResultCode::Ok);
  EXPECT(photos.empty());
  EXPECT(erased.files.files["/pictures/a.png"] == std::vector<std::uint8_t>({6}));
  EXPECT(erased.deleter.calls == 0);
}

void test_restoring_a_copy_brings_the_original_back() {
  Fixture world;
  const std::int64_t photo_id = add_project_photo(world, "/pictures/a.png", "a.png");
  const pixcake::ProjectResult first = world.library->create_virtual_copy(photo_id, "doc-1");
  const pixcake::ProjectResult second = world.library->create_virtual_copy(photo_id, "doc-2");
  EXPECT(first.code == pixcake::ResultCode::Ok);
  EXPECT(second.code == pixcake::ResultCode::Ok);
  EXPECT(world.library->trash_photo(photo_id).code == pixcake::ResultCode::Ok);
  const std::int64_t second_time = deleted_at(world, pixcake::kTrashVirtualCopy, second.id);
  EXPECT(world.library->restore_virtual_copies({first.id}).code == pixcake::ResultCode::Ok);
  EXPECT(!in_trash(world, pixcake::kTrashVirtualCopy, first.id));
  EXPECT(!in_trash(world, pixcake::kTrashPhoto, photo_id));
  EXPECT(deleted_at(world, pixcake::kTrashVirtualCopy, second.id) == second_time);

  Fixture active_original;
  const std::int64_t active_photo = add_project_photo(active_original, "/pictures/a.png", "a.png");
  const pixcake::ProjectResult active_copy = active_original.library->create_virtual_copy(active_photo, "doc-active");
  EXPECT(active_copy.code == pixcake::ResultCode::Ok);
  EXPECT(active_original.library->trash_virtual_copy(active_copy.id).code == pixcake::ResultCode::Ok);
  EXPECT(active_original.library->restore_virtual_copies({active_copy.id}).code == pixcake::ResultCode::Ok);
  EXPECT(!in_trash(active_original, pixcake::kTrashVirtualCopy, active_copy.id));
  EXPECT(!in_trash(active_original, pixcake::kTrashPhoto, active_photo));

  Fixture blocked;
  const pixcake::ProjectResult blocked_project =
      blocked.library->create_project("还在回收站", pixcake::kOriginLocal, false);
  EXPECT(blocked_project.code == pixcake::ResultCode::Ok);
  const pixcake::ProjectResult blocked_photo =
      blocked.library->add_photo(blocked_project.id, "/pictures/a.png", "a.png", {1});
  EXPECT(blocked_photo.code == pixcake::ResultCode::Ok);
  const pixcake::ProjectResult blocked_copy = blocked.library->create_virtual_copy(blocked_photo.id, "doc-blocked");
  EXPECT(blocked_copy.code == pixcake::ResultCode::Ok);
  EXPECT(blocked.library->trash_project(blocked_project.id).code == pixcake::ResultCode::Ok);
  const std::int64_t project_time = deleted_at(blocked, pixcake::kTrashProject, blocked_project.id);
  const std::int64_t photo_time = deleted_at(blocked, pixcake::kTrashPhoto, blocked_photo.id);
  const std::int64_t copy_time = deleted_at(blocked, pixcake::kTrashVirtualCopy, blocked_copy.id);
  EXPECT(blocked.library->restore_virtual_copies({blocked_copy.id}).code == pixcake::ResultCode::StateConflict);
  EXPECT(deleted_at(blocked, pixcake::kTrashProject, blocked_project.id) == project_time);
  EXPECT(deleted_at(blocked, pixcake::kTrashPhoto, blocked_photo.id) == photo_time);
  EXPECT(deleted_at(blocked, pixcake::kTrashVirtualCopy, blocked_copy.id) == copy_time);

  Fixture rejected;
  const std::int64_t rejected_photo = add_project_photo(rejected, "/pictures/a.png", "a.png");
  const pixcake::ProjectResult kept = rejected.library->create_virtual_copy(rejected_photo, "doc-kept");
  const pixcake::ProjectResult loose = rejected.library->create_virtual_copy(rejected_photo, "doc-loose");
  EXPECT(kept.code == pixcake::ResultCode::Ok);
  EXPECT(loose.code == pixcake::ResultCode::Ok);
  EXPECT(rejected.library->trash_virtual_copy(kept.id).code == pixcake::ResultCode::Ok);
  const std::int64_t kept_time = deleted_at(rejected, pixcake::kTrashVirtualCopy, kept.id);
  EXPECT(rejected.library->restore_virtual_copies({}).code == pixcake::ResultCode::StateConflict);
  EXPECT(rejected.library->restore_virtual_copies({999}).code == pixcake::ResultCode::StateConflict);
  EXPECT(rejected.library->restore_virtual_copies({loose.id}).code == pixcake::ResultCode::StateConflict);
  EXPECT(rejected.library->restore_virtual_copies({kept.id, 999}).code == pixcake::ResultCode::StateConflict);
  EXPECT(deleted_at(rejected, pixcake::kTrashVirtualCopy, kept.id) == kept_time);
  EXPECT(!in_trash(rejected, pixcake::kTrashVirtualCopy, loose.id));

  Fixture photo_only;
  const std::int64_t photo_only_id = add_project_photo(photo_only, "/pictures/a.png", "a.png");
  const pixcake::ProjectResult photo_only_copy = photo_only.library->create_virtual_copy(photo_only_id, "doc-photo");
  EXPECT(photo_only_copy.code == pixcake::ResultCode::Ok);
  EXPECT(photo_only.library->trash_photo(photo_only_id).code == pixcake::ResultCode::Ok);
  const std::int64_t photo_only_time = deleted_at(photo_only, pixcake::kTrashVirtualCopy, photo_only_copy.id);
  EXPECT(photo_only.library->restore_photos({photo_only_id}).code == pixcake::ResultCode::Ok);
  EXPECT(!in_trash(photo_only, pixcake::kTrashPhoto, photo_only_id));
  EXPECT(deleted_at(photo_only, pixcake::kTrashVirtualCopy, photo_only_copy.id) == photo_only_time);

  Fixture restored_project;
  const pixcake::ProjectResult whole = restored_project.library->create_project("整本恢复", pixcake::kOriginLocal, false);
  EXPECT(whole.code == pixcake::ResultCode::Ok);
  const pixcake::ProjectResult whole_photo =
      restored_project.library->add_photo(whole.id, "/pictures/a.png", "a.png", {1});
  EXPECT(whole_photo.code == pixcake::ResultCode::Ok);
  const pixcake::ProjectResult whole_copy = restored_project.library->create_virtual_copy(whole_photo.id, "doc-whole");
  EXPECT(whole_copy.code == pixcake::ResultCode::Ok);
  EXPECT(restored_project.library->trash_project(whole.id).code == pixcake::ResultCode::Ok);
  const std::int64_t whole_copy_time = deleted_at(restored_project, pixcake::kTrashVirtualCopy, whole_copy.id);
  EXPECT(restored_project.library->restore_project(whole.id).code == pixcake::ResultCode::Ok);
  EXPECT(!in_trash(restored_project, pixcake::kTrashProject, whole.id));
  EXPECT(!in_trash(restored_project, pixcake::kTrashPhoto, whole_photo.id));
  EXPECT(deleted_at(restored_project, pixcake::kTrashVirtualCopy, whole_copy.id) == whole_copy_time);
}

}  // namespace

int main() {
  test_copy_points_at_original_and_keeps_its_document_id();
  test_rejected_copy_writes_nothing();
  test_version_one_library_keeps_photos_and_gains_copies();
  test_shares_original_without_a_charge();
  test_trashing_a_copy_leaves_the_original();
  test_trashing_the_original_takes_copies_with_it();
  test_delete_source_file_trashes_copies_without_a_second_path();
  test_expired_copy_index_is_removed_without_the_file();
  test_restoring_a_copy_brings_the_original_back();
  return g_failures == 0 ? 0 : 1;
}
