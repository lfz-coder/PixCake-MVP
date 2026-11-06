// 测试清单 T-08：删除原图在终态写失败后由启动扫描收敛（从 tests/kernel/project/test_recycle_bin.cpp 迁来）。
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













}  // namespace

void test_b102_delete_source_file_recovers_when_final_presence_write_fails() {
  pixcake::test::TempDir dir;
  std::unique_ptr<pixcake::SqliteProjectStore> sqlite;
  EXPECT(pixcake::SqliteProjectStore::open(dir.path() + "/library.db", sqlite) == pixcake::ResultCode::Ok);
  FailingPresenceStore store(*sqlite);
  FixedClock clock;
  pixcake::test::FakeFileSystem files;
  files.files["/pictures/recover.png"] = pixcake::test::png_with({1, 2, 3});
  FakeDeleter deleter;
  deleter.files = &files;
  MemoryLog log;
  pixcake::ProjectLibrary library(store, files, clock, deleter, &log);
  const pixcake::ProjectResult project = library.create_project("恢复删除", pixcake::kOriginLocal, false);
  const pixcake::ProjectResult photo = library.add_photo(project.id, pixcake::test::admitted_png(files, "/pictures/recover.png"), "recover.png", {1});
  store.fail_on_presence_call = 2;  // intent succeeds; post-delete state write fails.
  EXPECT(library.delete_source_file(photo.id).code == pixcake::ResultCode::DiskFull);
  std::optional<pixcake::PhotoRow> pending;
  EXPECT(library.load_photo(photo.id, pending) == pixcake::ResultCode::Ok);
  EXPECT(pending.has_value());
  EXPECT(pending->presence == pixcake::kPresenceDeletePending);
  EXPECT(!files.files.count("/pictures/recover.png"));
  store.fail_on_presence_call = 0;
  EXPECT(library.startup() == pixcake::ResultCode::Ok);
  EXPECT(library.load_photo(photo.id, pending) == pixcake::ResultCode::Ok);
  EXPECT(pending.has_value());
  EXPECT(pending->presence == pixcake::kPresenceMissing);
}

int main() {
  test_b102_delete_source_file_recovers_when_final_presence_write_fails();
  return g_failures == 0 ? 0 : 1;
}
