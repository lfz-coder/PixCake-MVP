#include "pixcake/editdoc/edit_document.hpp"
#include "pixcake/project/project_library.hpp"
#include "pixcake/project/source_access.hpp"
#include "pixcake/project/source_deleter.hpp"
#include "pixcake/storage/sqlite_project_store.hpp"
#include "pixcake/support/clock.hpp"
#include "pixcake/support/log_sink.hpp"
#include "support/admitted_photo.hpp"
#include "support/fake_file_system.hpp"
#include "support/temp_dir.hpp"

#include <cstdint>
#include <cstdio>
#include <map>
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

const char* kSource = "/pictures/a.png";
const char* kEdit = "edits/1.pxedit";
const char* kCredential = "credential-do-not-log";

struct FixedClock : pixcake::Clock {
  std::int64_t unix_seconds() const override { return 1700000000; }
};

struct MemoryLog : pixcake::LogSink {
  std::vector<pixcake::LogRecord> records;
  void write(const pixcake::LogRecord& record) override { records.push_back(record); }
};

struct UnusedDeleter : pixcake::SourceDeleter {
  pixcake::ResultCode remove_source(const std::string&) override { return pixcake::ResultCode::StateConflict; }
};

std::vector<pixcake::VersionLimit> limits() { return {pixcake::VersionLimit{"color.global", 1}}; }

std::vector<std::uint8_t> credential_bytes() {
  return std::vector<std::uint8_t>(kCredential, kCredential + std::char_traits<char>::length(kCredential));
}

bool record_mentions_credential(const pixcake::LogRecord& record) {
  return record.message.find(kCredential) != std::string::npos ||
         record.path.find(kCredential) != std::string::npos;
}

// S-10、需求第 118 行：原图只记绝对路径和系统授予的书签或安全范围。替身按书签字节给出解析结果。
struct FakeAccess final : pixcake::SourceAccess {
  std::map<std::vector<std::uint8_t>, pixcake::ResolvedSource> answers;
  pixcake::ResolvedSource resolve(const std::vector<std::uint8_t>& bookmark) override {
    const auto found = answers.find(bookmark);
    if (found == answers.end()) {
      pixcake::ResolvedSource none;
      none.code = pixcake::ResultCode::PermissionDenied;
      return none;
    }
    return found->second;
  }
};

pixcake::ResolvedSource at(const std::string& path) {
  pixcake::ResolvedSource resolved;
  resolved.absolute_path = path;
  return resolved;
}

struct World {
  pixcake::test::TempDir dir;
  std::unique_ptr<pixcake::SqliteProjectStore> store;
  FixedClock clock;
  MemoryLog log;
  UnusedDeleter deleter;
  pixcake::test::FakeFileSystem files;
  std::unique_ptr<pixcake::ProjectLibrary> library;
  std::int64_t project = 0;
  World() {
    EXPECT(pixcake::SqliteProjectStore::open(dir.path() + "/library.db", store) == pixcake::ResultCode::Ok);
    library = std::make_unique<pixcake::ProjectLibrary>(*store, files, clock, deleter, &log);
    project = library->create_project("书签", pixcake::kOriginLocal, false).id;
  }
  std::int64_t add(const std::string& path, std::vector<std::uint8_t> bookmark) {
    files.files[path] = pixcake::test::tiny_png();
    return library->add_photo(project, pixcake::test::admitted_png(files, path), "x.png", std::move(bookmark)).id;
  }
  pixcake::PhotoRow row(std::int64_t id) {
    std::optional<pixcake::PhotoRow> loaded;
    EXPECT(library->load_photo(id, loaded) == pixcake::ResultCode::Ok && loaded.has_value());
    return loaded.value_or(pixcake::PhotoRow{});
  }
};

void test_s10_admission_requires_bookmark() {
  World world;
  world.files.files["/pictures/none.png"] = pixcake::test::tiny_png();
  const auto refused = world.library->add_photo(world.project,
                                                pixcake::test::admitted_png(world.files, "/pictures/none.png"),
                                                "none.png", {});
  EXPECT(refused.code == pixcake::ResultCode::PermissionDenied);
  std::vector<pixcake::PhotoRow> photos;
  EXPECT(world.library->list_photos(world.project, photos) == pixcake::ResultCode::Ok && photos.empty());
}

void test_s10_bookmarks_refresh_and_moved_source_is_missing() {
  World world;
  const auto stale = world.add("/pictures/stale.png", {1});
  const auto moved = world.add("/pictures/moved.png", {2});
  const auto gone = world.add("/pictures/gone.png", {3});
  const auto denied = world.add("/pictures/denied.png", {4});
  FakeAccess access;
  auto refreshed = at("/pictures/stale.png");
  refreshed.stale = true;
  refreshed.refreshed = {9, 9};
  access.answers[{1}] = refreshed;
  access.answers[{2}] = at("/elsewhere/moved.png");
  pixcake::ResolvedSource missing;
  missing.code = pixcake::ResultCode::SourceMissing;
  access.answers[{3}] = missing;
  EXPECT(world.library->refresh_source_access(access) == pixcake::ResultCode::PermissionDenied);
  EXPECT(world.row(stale).presence == pixcake::kPresenceEditable);
  EXPECT(world.row(stale).access_credential == std::vector<std::uint8_t>({9, 9}));
  // 需求第 118 行：用户挪走或重命名原图后标“原文件缺失”，不跟着书签换路径。
  EXPECT(world.row(moved).presence == pixcake::kPresenceMissing);
  EXPECT(world.row(moved).absolute_path == "/pictures/moved.png");
  EXPECT(world.row(gone).presence == pixcake::kPresenceMissing);
  // 无权读取不改状态，返回权限不足。
  EXPECT(world.row(denied).presence == pixcake::kPresenceEditable);
  // 原文件回到原路径后自动恢复。
  access.answers[{2}] = at("/pictures/moved.png");
  access.answers[{3}] = at("/pictures/gone.png");
  access.answers[{4}] = at("/pictures/denied.png");
  access.answers[{9, 9}] = at("/pictures/stale.png");
  EXPECT(world.library->refresh_source_access(access) == pixcake::ResultCode::Ok);
  EXPECT(world.row(moved).presence == pixcake::kPresenceEditable);
  EXPECT(world.row(gone).presence == pixcake::kPresenceEditable);
  for (const auto& record : world.log.records) EXPECT(!record_mentions_credential(record));
}

}  // namespace

int main() {
  test_s10_admission_requires_bookmark();
  test_s10_bookmarks_refresh_and_moved_source_is_missing();
  pixcake::test::TempDir dir;
  std::unique_ptr<pixcake::SqliteProjectStore> store;
  EXPECT(pixcake::SqliteProjectStore::open(dir.path() + "/library.db", store) == pixcake::ResultCode::Ok);
  FixedClock clock;
  MemoryLog log;
  UnusedDeleter deleter;
  pixcake::test::FakeFileSystem files;
  files.files[kSource] = pixcake::test::tiny_png();
  pixcake::ProjectLibrary library(*store, files, clock, deleter, &log);

  const pixcake::ProjectResult project = library.create_project("路径", pixcake::kOriginLocal, false);
  EXPECT(project.code == pixcake::ResultCode::Ok);
  const pixcake::ProjectResult photo = library.add_photo(project.id, pixcake::test::admitted_png(files, kSource), "a.png", credential_bytes());
  EXPECT(photo.code == pixcake::ResultCode::Ok);

  auto created = pixcake::EditDocument::create(std::to_string(photo.id));
  EXPECT(created.has_value());
  pixcake::EditDocument document = std::move(*created);
  EXPECT(document.set_preview("color.global", "exposure", "1.00") == pixcake::ResultCode::Ok);
  EXPECT(document.commit_preview(0, "color.global", "exposure", limits(), files, kEdit) == pixcake::ResultCode::Ok);
  const std::uint64_t checksum = document.content_checksum();
  const std::vector<std::uint8_t> edit_bytes = files.files[kEdit];

  files.files.erase(kSource);
  EXPECT(library.refresh_source_presence() == pixcake::ResultCode::Ok);
  std::optional<pixcake::PhotoRow> row;
  EXPECT(library.load_photo(photo.id, row) == pixcake::ResultCode::Ok);
  EXPECT(row.has_value());
  EXPECT(row->presence == pixcake::kPresenceMissing);
  EXPECT(library.source_status(photo.id) == pixcake::ResultCode::SourceMissing);
  EXPECT(files.files[kEdit] == edit_bytes);

  pixcake::EditDocument reloaded;
  EXPECT(pixcake::EditDocument::load(reloaded, files, kEdit, limits()).code == pixcake::ResultCode::Ok);
  EXPECT(reloaded.content_checksum() == checksum);
  EXPECT(reloaded.set_preview("color.global", "exposure", "1.50") == pixcake::ResultCode::Ok);
  EXPECT(reloaded.commit_preview(reloaded.revision(), "color.global", "exposure", limits(), files, kEdit) ==
         pixcake::ResultCode::Ok);

  EXPECT(!log.records.empty());
  bool saw_missing = false;
  for (const pixcake::LogRecord& record : log.records) {
    EXPECT(!record_mentions_credential(record));
    if (record.code == pixcake::ResultCode::SourceMissing) {
      saw_missing = true;
      EXPECT(record.path == kSource);
      EXPECT(record.message == "project");
    }
  }
  EXPECT(saw_missing);

  files.files[kSource] = pixcake::test::tiny_png();
  EXPECT(library.startup() == pixcake::ResultCode::Ok);
  EXPECT(library.source_status(photo.id) == pixcake::ResultCode::Ok);
  EXPECT(library.load_photo(photo.id, row) == pixcake::ResultCode::Ok);
  EXPECT(row->presence == pixcake::kPresenceEditable);
  EXPECT(reloaded.set_preview("color.global", "exposure", "2.00") == pixcake::ResultCode::Ok);
  EXPECT(reloaded.commit_preview(reloaded.revision(), "color.global", "exposure", limits(), files, kEdit) ==
         pixcake::ResultCode::Ok);

  const pixcake::ProjectResult denied_photo =
      library.add_photo(project.id, pixcake::test::admitted_png(files, "/pictures/denied.png"), "denied.png", credential_bytes());
  const pixcake::ProjectResult gone_photo =
      library.add_photo(project.id, pixcake::test::admitted_png(files, "/pictures/gone.png"), "gone.png", {1});
  EXPECT(denied_photo.code == pixcake::ResultCode::Ok);
  EXPECT(gone_photo.code == pixcake::ResultCode::Ok);
  // 两张图都经准入加入项目；之后一张变成无权读取，一张被移走。
  files.files.erase("/pictures/gone.png");
  files.files["/pictures/denied.png"] = {1};
  files.probe_errors["/pictures/denied.png"] = pixcake::ResultCode::PermissionDenied;
  EXPECT(library.refresh_source_presence() == pixcake::ResultCode::PermissionDenied);
  EXPECT(library.load_photo(denied_photo.id, row) == pixcake::ResultCode::Ok);
  EXPECT(row->presence == pixcake::kPresenceEditable);
  EXPECT(library.load_photo(gone_photo.id, row) == pixcake::ResultCode::Ok);
  EXPECT(row->presence == pixcake::kPresenceMissing);
  EXPECT(library.source_status(photo.id) == pixcake::ResultCode::Ok);

  return g_failures == 0 ? 0 : 1;
}
