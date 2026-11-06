#include "pixcake/project/project_library.hpp"
#include "pixcake/project/source_deleter.hpp"
#include "pixcake/storage/sqlite_project_store.hpp"
#include "pixcake/support/clock.hpp"
#include "pixcake/support/log_sink.hpp"
#include "support/admitted_photo.hpp"
#include "support/fake_file_system.hpp"
#include "support/temp_dir.hpp"

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
  pixcake::test::FakeFileSystem* files = nullptr;
  pixcake::FileIdentity expected;

  pixcake::ResultCode remove_source(const std::string& path) override {
    ++calls;
    last = path;
    if (next == pixcake::ResultCode::Ok && files != nullptr) {
      files->files.erase(path);
    }
    return next;
  }

  pixcake::ResultCode remove_source(const std::string& path, const pixcake::FileIdentity& identity) override {
    ++calls;
    last = path;
    expected = identity;
    if (next != pixcake::ResultCode::Ok) return next;
    if (files == nullptr) return pixcake::ResultCode::IoFailed;
    const pixcake::IoResult removed = files->remove_if_identity(path, identity);
    if (removed.ok) return pixcake::ResultCode::Ok;
    if (removed.not_found) return pixcake::ResultCode::SourceMissing;
    return removed.code;
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

  Fixture() {
    EXPECT(pixcake::SqliteProjectStore::open(dir.path() + "/library.db", store) == pixcake::ResultCode::Ok);
    deleter.files = &files;
    library = std::make_unique<pixcake::ProjectLibrary>(*store, files, clock, deleter, &log);
  }
};

class FailingPresenceStore final : public pixcake::ProjectStore {
 public:
  explicit FailingPresenceStore(pixcake::ProjectStore& inner) : inner_(inner) {}

  int fail_on_presence_call = 0;
  int presence_calls = 0;

  pixcake::ResultCode insert_project(const pixcake::ProjectDraft& draft, std::int64_t& id) override {
    return inner_.insert_project(draft, id);
  }
  pixcake::ResultCode update_project_deleted_at(std::int64_t id,
                                                const std::optional<std::int64_t>& value) override {
    return inner_.update_project_deleted_at(id, value);
  }
  pixcake::ResultCode load_project(std::int64_t id, std::optional<pixcake::ProjectRow>& out) override {
    return inner_.load_project(id, out);
  }
  pixcake::ResultCode list_projects(std::vector<pixcake::ProjectRow>& out) override {
    return inner_.list_projects(out);
  }
  pixcake::ResultCode erase_project_index(std::int64_t id) override { return inner_.erase_project_index(id); }
  pixcake::ResultCode insert_photo(const pixcake::PhotoDraft& draft, std::int64_t& id) override {
    return inner_.insert_photo(draft, id);
  }
  pixcake::ResultCode update_photo_access_credential(std::int64_t id,
                                                     const std::vector<std::uint8_t>& credential) override {
    return inner_.update_photo_access_credential(id, credential);
  }
  pixcake::ResultCode update_photo_presence(std::int64_t id, const std::string& presence) override {
    ++presence_calls;
    if (fail_on_presence_call != 0 && presence_calls == fail_on_presence_call) {
      return pixcake::ResultCode::DiskFull;
    }
    return inner_.update_photo_presence(id, presence);
  }
  pixcake::ResultCode update_photo_rating(std::int64_t id, int rating) override {
    return inner_.update_photo_rating(id, rating);
  }
  pixcake::ResultCode update_photo_flag(std::int64_t id, const std::string& flag) override {
    return inner_.update_photo_flag(id, flag);
  }
  pixcake::ResultCode update_photo_color(std::int64_t id, const std::string& color) override {
    return inner_.update_photo_color(id, color);
  }
  pixcake::ResultCode update_photo_ratings(const std::vector<std::int64_t>& ids, int rating) override {
    return inner_.update_photo_ratings(ids, rating);
  }
  pixcake::ResultCode update_photo_flags(const std::vector<std::int64_t>& ids, const std::string& flag) override {
    return inner_.update_photo_flags(ids, flag);
  }
  pixcake::ResultCode update_photo_colors(const std::vector<std::int64_t>& ids, const std::string& color) override {
    return inner_.update_photo_colors(ids, color);
  }
  pixcake::ResultCode load_photo(std::int64_t id, std::optional<pixcake::PhotoRow>& out) override {
    return inner_.load_photo(id, out);
  }
  pixcake::ResultCode update_photo_edit_state(std::int64_t id, const std::string& status,
                                              std::int64_t edited_at) override {
    return inner_.update_photo_edit_state(id, status, edited_at);
  }
  pixcake::ResultCode update_photo_export_status(std::int64_t id, const std::string& status) override {
    return inner_.update_photo_export_status(id, status);
  }
  pixcake::ResultCode query_photos(const pixcake::PhotoQuery& query, std::vector<pixcake::PhotoRow>& out) override {
    return inner_.query_photos(query, out);
  }
  pixcake::ResultCode list_photos(const std::optional<std::int64_t>& project_id,
                                  std::vector<pixcake::PhotoRow>& out) override {
    return inner_.list_photos(project_id, out);
  }
  pixcake::ResultCode erase_photo(std::int64_t id) override { return inner_.erase_photo(id); }
  pixcake::ResultCode insert_virtual_copy(const pixcake::VirtualCopyDraft& draft, std::int64_t& id) override {
    return inner_.insert_virtual_copy(draft, id);
  }
  pixcake::ResultCode load_virtual_copy(std::int64_t id,
                                        std::optional<pixcake::VirtualCopyRow>& out) override {
    return inner_.load_virtual_copy(id, out);
  }
  pixcake::ResultCode load_virtual_copy_by_document(const std::string& document_id,
                                                    std::optional<pixcake::VirtualCopyRow>& out) override {
    return inner_.load_virtual_copy_by_document(document_id, out);
  }
  pixcake::ResultCode list_virtual_copies(std::int64_t photo_id,
                                          std::vector<pixcake::VirtualCopyRow>& out) override {
    return inner_.list_virtual_copies(photo_id, out);
  }
  pixcake::ResultCode erase_virtual_copy(std::int64_t id) override { return inner_.erase_virtual_copy(id); }
  pixcake::ResultCode erase_virtual_copies_of_photo(std::int64_t id) override {
    return inner_.erase_virtual_copies_of_photo(id);
  }
  pixcake::ResultCode insert_trash(const std::string& subject, std::int64_t subject_id,
                                   std::int64_t deleted_at) override {
    return inner_.insert_trash(subject, subject_id, deleted_at);
  }
  pixcake::ResultCode load_trash(const std::string& subject, std::int64_t subject_id,
                                 std::optional<pixcake::TrashRow>& out) override {
    return inner_.load_trash(subject, subject_id, out);
  }
  pixcake::ResultCode list_trash(std::vector<pixcake::TrashRow>& out) override {
    return inner_.list_trash(out);
  }
  pixcake::ResultCode erase_trash(const std::string& subject, std::int64_t subject_id) override {
    return inner_.erase_trash(subject, subject_id);
  }
  pixcake::ResultCode in_transaction(const std::function<pixcake::ResultCode()>& body) override {
    return inner_.in_transaction(body);
  }

 private:
  pixcake::ProjectStore& inner_;
};

std::int64_t trash_time(pixcake::ProjectLibrary& library, const char* subject, std::int64_t id) {
  std::optional<pixcake::TrashRow> row;
  EXPECT(library.load_trash(subject, id, row) == pixcake::ResultCode::Ok);
  EXPECT(row.has_value());
  return row.has_value() ? row->deleted_at : -1;
}

void test_under_thirty_days_can_be_restored() {
  Fixture world;
  world.files.files["/pictures/a.png"] = pixcake::test::png_with({1, 2, 3});
  const pixcake::ProjectResult project = world.library->create_project("保留", pixcake::kOriginLocal, false);
  const pixcake::ProjectResult photo =
      world.library->add_photo(project.id, pixcake::test::admitted_png(world.files, "/pictures/a.png"), "a.png", {1});
  EXPECT(world.library->trash_project(project.id).code == pixcake::ResultCode::Ok);
  EXPECT(world.deleter.calls == 0);
  world.clock.now += 2592000 - 1;
  EXPECT(world.library->startup() == pixcake::ResultCode::Ok);
  EXPECT(trash_time(*world.library, pixcake::kTrashProject, project.id) == 1000000);
  EXPECT(world.library->restore_project(project.id).code == pixcake::ResultCode::Ok);
  std::optional<pixcake::ProjectRow> loaded;
  EXPECT(world.library->load_project(project.id, loaded) == pixcake::ResultCode::Ok);
  EXPECT(loaded.has_value());
  EXPECT(!loaded->deleted_at.has_value());
  std::optional<pixcake::TrashRow> gone;
  EXPECT(world.library->load_trash(pixcake::kTrashPhoto, photo.id, gone) == pixcake::ResultCode::Ok);
  EXPECT(!gone.has_value());
  EXPECT(world.files.files["/pictures/a.png"] == pixcake::test::png_with({1, 2, 3}));
  EXPECT(world.deleter.calls == 0);
}

void test_expired_index_is_removed_without_deleting_the_file() {
  Fixture world;
  world.files.files["/pictures/a.png"] = pixcake::test::png_with({4, 5});
  const pixcake::ProjectResult project = world.library->create_project("到期", pixcake::kOriginLocal, false);
  const pixcake::ProjectResult photo =
      world.library->add_photo(project.id, pixcake::test::admitted_png(world.files, "/pictures/a.png"), "a.png", {1});
  EXPECT(world.library->trash_project(project.id).code == pixcake::ResultCode::Ok);
  world.clock.now += 2592000;
  EXPECT(world.library->startup() == pixcake::ResultCode::Ok);
  std::optional<pixcake::ProjectRow> loaded_project;
  std::optional<pixcake::PhotoRow> loaded_photo;
  EXPECT(world.library->load_project(project.id, loaded_project) == pixcake::ResultCode::Ok);
  EXPECT(!loaded_project.has_value());
  EXPECT(world.library->load_photo(photo.id, loaded_photo) == pixcake::ResultCode::Ok);
  EXPECT(!loaded_photo.has_value());
  std::vector<pixcake::TrashRow> trash;
  EXPECT(world.library->list_trash(trash) == pixcake::ResultCode::Ok);
  EXPECT(trash.empty());
  EXPECT(world.files.files["/pictures/a.png"] == pixcake::test::png_with({4, 5}));
  EXPECT(world.deleter.calls == 0);
}

void test_only_the_expired_photo_is_purged() {
  Fixture world;
  const pixcake::ProjectResult project = world.library->create_project("部分", pixcake::kOriginLocal, false);
  const pixcake::ProjectResult older =
      world.library->add_photo(project.id, pixcake::test::admitted_png(world.files, "/pictures/old.png"), "old.png", {1});
  EXPECT(world.library->trash_photo(older.id).code == pixcake::ResultCode::Ok);
  const std::int64_t older_time = trash_time(*world.library, pixcake::kTrashPhoto, older.id);
  world.clock.now = older_time + 2592000;
  const pixcake::ProjectResult newer =
      world.library->add_photo(project.id, pixcake::test::admitted_png(world.files, "/pictures/new.png"), "new.png", {1});
  EXPECT(world.library->trash_photo(newer.id).code == pixcake::ResultCode::Ok);
  const std::int64_t newer_time = trash_time(*world.library, pixcake::kTrashPhoto, newer.id);
  EXPECT(world.library->startup() == pixcake::ResultCode::Ok);
  std::optional<pixcake::PhotoRow> old_row;
  EXPECT(world.library->load_photo(older.id, old_row) == pixcake::ResultCode::Ok);
  EXPECT(!old_row.has_value());
  EXPECT(trash_time(*world.library, pixcake::kTrashPhoto, newer.id) == newer_time);
  EXPECT(world.deleter.calls == 0);
}

void test_restore_project_brings_back_its_photos() {
  Fixture world;
  const pixcake::ProjectResult project = world.library->create_project("恢复", pixcake::kOriginLocal, false);
  const pixcake::ProjectResult first =
      world.library->add_photo(project.id, pixcake::test::admitted_png(world.files, "/pictures/1.png"), "1.png", {1});
  const pixcake::ProjectResult second =
      world.library->add_photo(project.id, pixcake::test::admitted_png(world.files, "/pictures/2.png"), "2.png", {1});
  world.clock.now = 10;
  EXPECT(world.library->trash_photo(first.id).code == pixcake::ResultCode::Ok);
  world.clock.now = 20;
  EXPECT(world.library->trash_project(project.id).code == pixcake::ResultCode::Ok);
  EXPECT(trash_time(*world.library, pixcake::kTrashPhoto, first.id) == 10);
  EXPECT(world.library->restore_project(project.id).code == pixcake::ResultCode::Ok);
  std::optional<pixcake::ProjectRow> loaded;
  EXPECT(world.library->load_project(project.id, loaded) == pixcake::ResultCode::Ok);
  EXPECT(!loaded->deleted_at.has_value());
  std::optional<pixcake::TrashRow> first_trash;
  std::optional<pixcake::TrashRow> second_trash;
  EXPECT(world.library->load_trash(pixcake::kTrashPhoto, first.id, first_trash) == pixcake::ResultCode::Ok);
  EXPECT(world.library->load_trash(pixcake::kTrashPhoto, second.id, second_trash) == pixcake::ResultCode::Ok);
  EXPECT(!first_trash.has_value());
  EXPECT(!second_trash.has_value());
}

void test_partial_restore_leaves_the_other_photo() {
  Fixture world;
  const pixcake::ProjectResult project = world.library->create_project("挑选", pixcake::kOriginLocal, false);
  const pixcake::ProjectResult first =
      world.library->add_photo(project.id, pixcake::test::admitted_png(world.files, "/pictures/1.png"), "1.png", {1});
  const pixcake::ProjectResult second =
      world.library->add_photo(project.id, pixcake::test::admitted_png(world.files, "/pictures/2.png"), "2.png", {1});
  world.clock.now = 30;
  EXPECT(world.library->trash_photo(first.id).code == pixcake::ResultCode::Ok);
  world.clock.now = 40;
  EXPECT(world.library->trash_photo(second.id).code == pixcake::ResultCode::Ok);
  EXPECT(world.library->restore_photos({}).code == pixcake::ResultCode::StateConflict);
  EXPECT(trash_time(*world.library, pixcake::kTrashPhoto, first.id) == 30);
  EXPECT(trash_time(*world.library, pixcake::kTrashPhoto, second.id) == 40);
  EXPECT(world.library->restore_photos({first.id, 999}).code == pixcake::ResultCode::StateConflict);
  EXPECT(trash_time(*world.library, pixcake::kTrashPhoto, first.id) == 30);
  EXPECT(world.library->restore_photos({first.id}).code == pixcake::ResultCode::Ok);
  std::optional<pixcake::TrashRow> restored;
  EXPECT(world.library->load_trash(pixcake::kTrashPhoto, first.id, restored) == pixcake::ResultCode::Ok);
  EXPECT(!restored.has_value());
  EXPECT(trash_time(*world.library, pixcake::kTrashPhoto, second.id) == 40);
}

void test_restore_photos_fails_while_the_project_is_trashed() {
  Fixture world;
  const pixcake::ProjectResult project = world.library->create_project("整项", pixcake::kOriginLocal, false);
  const pixcake::ProjectResult photo =
      world.library->add_photo(project.id, pixcake::test::admitted_png(world.files, "/pictures/1.png"), "1.png", {1});
  EXPECT(world.library->trash_project(project.id).code == pixcake::ResultCode::Ok);
  const std::int64_t project_time = trash_time(*world.library, pixcake::kTrashProject, project.id);
  const std::int64_t photo_time = trash_time(*world.library, pixcake::kTrashPhoto, photo.id);
  EXPECT(world.library->restore_photos({photo.id}).code == pixcake::ResultCode::StateConflict);
  EXPECT(trash_time(*world.library, pixcake::kTrashProject, project.id) == project_time);
  EXPECT(trash_time(*world.library, pixcake::kTrashPhoto, photo.id) == photo_time);
}

void test_trashing_twice_keeps_the_original_time() {
  Fixture world;
  const pixcake::ProjectResult project = world.library->create_project("重复", pixcake::kOriginLocal, false);
  const pixcake::ProjectResult photo =
      world.library->add_photo(project.id, pixcake::test::admitted_png(world.files, "/pictures/1.png"), "1.png", {1});
  world.clock.now = 70;
  EXPECT(world.library->trash_photo(photo.id).code == pixcake::ResultCode::Ok);
  world.clock.now = 80;
  EXPECT(world.library->trash_photo(photo.id).code == pixcake::ResultCode::StateConflict);
  EXPECT(trash_time(*world.library, pixcake::kTrashPhoto, photo.id) == 70);
}

void test_erase_trashed_project_removes_index_only() {
  Fixture world;
  world.files.files["/pictures/a.png"] = pixcake::test::png_with({8});
  const pixcake::ProjectResult project = world.library->create_project("彻底", pixcake::kOriginLocal, false);
  const pixcake::ProjectResult photo =
      world.library->add_photo(project.id, pixcake::test::admitted_png(world.files, "/pictures/a.png"), "a.png", {1});
  EXPECT(world.library->erase_trashed_project(project.id).code == pixcake::ResultCode::StateConflict);
  std::optional<pixcake::ProjectRow> still;
  EXPECT(world.library->load_project(project.id, still) == pixcake::ResultCode::Ok);
  EXPECT(still.has_value());
  EXPECT(world.library->trash_project(project.id).code == pixcake::ResultCode::Ok);
  EXPECT(world.library->erase_trashed_project(project.id).code == pixcake::ResultCode::Ok);
  EXPECT(world.library->load_project(project.id, still) == pixcake::ResultCode::Ok);
  EXPECT(!still.has_value());
  std::optional<pixcake::PhotoRow> photo_row;
  EXPECT(world.library->load_photo(photo.id, photo_row) == pixcake::ResultCode::Ok);
  EXPECT(!photo_row.has_value());
  EXPECT(world.files.files["/pictures/a.png"] == pixcake::test::png_with({8}));
  EXPECT(world.deleter.calls == 0);
}

void test_delete_source_file() {
  Fixture world;
  world.files.files["/pictures/gone.png"] = pixcake::test::png_with({1});
  world.files.files["/pictures/keep.png"] = pixcake::test::png_with({2});
  const pixcake::ProjectResult project = world.library->create_project("磁盘", pixcake::kOriginLocal, false);
  const pixcake::ProjectResult photo =
      world.library->add_photo(project.id, pixcake::test::admitted_png(world.files, "/pictures/gone.png"), "gone.png", {1});
  EXPECT(world.library->delete_source_file(999).code == pixcake::ResultCode::StateConflict);
  EXPECT(world.deleter.calls == 0);

  const pixcake::ProjectResult removed = world.library->delete_source_file(photo.id);
  EXPECT(removed.code == pixcake::ResultCode::Ok);
  EXPECT(removed.notice.empty());
  EXPECT(world.deleter.calls == 1);
  EXPECT(world.deleter.last == "/pictures/gone.png");
  EXPECT(world.files.files["/pictures/keep.png"] == pixcake::test::png_with({2}));
  EXPECT(!world.files.files.count("/pictures/gone.png"));
  std::optional<pixcake::PhotoRow> row;
  EXPECT(world.library->load_photo(photo.id, row) == pixcake::ResultCode::Ok);
  EXPECT(row->presence == pixcake::kPresenceMissing);
  EXPECT(trash_time(*world.library, pixcake::kTrashPhoto, photo.id) == world.clock.now);

  world.deleter.next = pixcake::ResultCode::PermissionDenied;
  const pixcake::ProjectResult sibling =
      world.library->add_photo(project.id, pixcake::test::admitted_png(world.files, "/pictures/keep.png"), "keep.png", {1});
  const std::vector<std::uint8_t> before = world.files.files["/pictures/keep.png"];
  const pixcake::ProjectResult denied = world.library->delete_source_file(sibling.id);
  EXPECT(denied.code == pixcake::ResultCode::PermissionDenied);
  EXPECT(denied.notice == pixcake::kDiskFileRemains);
  EXPECT(world.files.files["/pictures/keep.png"] == before);
  EXPECT(world.library->load_photo(sibling.id, row) == pixcake::ResultCode::Ok);
  EXPECT(row->presence == pixcake::kPresenceDeletePending);
  const std::int64_t kept = trash_time(*world.library, pixcake::kTrashPhoto, sibling.id);
  world.clock.now += 5;
  const pixcake::ProjectResult denied_again = world.library->delete_source_file(sibling.id);
  EXPECT(denied_again.code == pixcake::ResultCode::PermissionDenied);
  EXPECT(denied_again.notice == pixcake::kDiskFileRemains);
  EXPECT(trash_time(*world.library, pixcake::kTrashPhoto, sibling.id) == kept);
  EXPECT(world.library->startup() == pixcake::ResultCode::Ok);
  EXPECT(world.library->load_photo(sibling.id, row) == pixcake::ResultCode::Ok);
  EXPECT(row->presence == pixcake::kPresenceEditable);
}


void test_file_source_deleter_rejects_relative_paths() {
  pixcake::test::FakeFileSystem files;
  files.files["/pictures/one.png"] = pixcake::test::png_with({1});
  files.files["/pictures/two.png"] = pixcake::test::png_with({2});
  files.files["relative.png"] = {3};
  pixcake::FileSourceDeleter deleter(files);
  EXPECT(deleter.remove_source("relative.png") == pixcake::ResultCode::StateConflict);
  EXPECT(files.files["relative.png"] == std::vector<std::uint8_t>({3}));
  EXPECT(deleter.remove_source("/pictures/one.png") == pixcake::ResultCode::Ok);
  EXPECT(files.files.count("/pictures/one.png") == 0);
  EXPECT(files.files["/pictures/two.png"] == pixcake::test::png_with({2}));
}

void test_delete_uses_enrolled_file_identity() {
  Fixture world;
  world.files.files["/pictures/orig.png"] = pixcake::test::png_with({1, 2, 3});
  const pixcake::ProjectResult project = world.library->create_project("身份", pixcake::kOriginLocal, false);
  const pixcake::ProjectResult photo = world.library->add_photo(project.id, pixcake::test::admitted_png(world.files, "/pictures/orig.png"), "orig.png", {1});
  EXPECT(photo.code == pixcake::ResultCode::Ok);
  world.files.files["/pictures/orig.png"] = pixcake::test::png_with({9, 9, 9});
  EXPECT(world.library->delete_source_file(photo.id).code == pixcake::ResultCode::StateConflict);
  EXPECT(world.files.files["/pictures/orig.png"] == pixcake::test::png_with({9, 9, 9}));
}

}  // namespace

int main() {
  test_under_thirty_days_can_be_restored();
  test_expired_index_is_removed_without_deleting_the_file();
  test_only_the_expired_photo_is_purged();
  test_restore_project_brings_back_its_photos();
  test_partial_restore_leaves_the_other_photo();
  test_restore_photos_fails_while_the_project_is_trashed();
  test_trashing_twice_keeps_the_original_time();
  test_erase_trashed_project_removes_index_only();
  test_delete_source_file();
  test_file_source_deleter_rejects_relative_paths();
  test_delete_uses_enrolled_file_identity();
  return g_failures == 0 ? 0 : 1;
}
