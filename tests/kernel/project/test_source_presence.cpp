#include "pixcake/editdoc/edit_document.hpp"
#include "pixcake/project/project_library.hpp"
#include "pixcake/project/source_deleter.hpp"
#include "pixcake/storage/sqlite_project_store.hpp"
#include "pixcake/support/clock.hpp"
#include "pixcake/support/log_sink.hpp"
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

}  // namespace

int main() {
  pixcake::test::TempDir dir;
  std::unique_ptr<pixcake::SqliteProjectStore> store;
  EXPECT(pixcake::SqliteProjectStore::open(dir.path() + "/library.db", store) == pixcake::ResultCode::Ok);
  FixedClock clock;
  MemoryLog log;
  UnusedDeleter deleter;
  pixcake::test::FakeFileSystem files;
  files.files[kSource] = {9, 9, 9};
  pixcake::ProjectLibrary library(*store, files, clock, deleter, &log);

  const pixcake::ProjectResult project = library.create_project("路径", pixcake::kOriginLocal, false);
  EXPECT(project.code == pixcake::ResultCode::Ok);
  const pixcake::ProjectResult photo = library.add_photo(project.id, kSource, "a.png", credential_bytes());
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

  files.files[kSource] = {9, 9, 9};
  EXPECT(library.startup() == pixcake::ResultCode::Ok);
  EXPECT(library.source_status(photo.id) == pixcake::ResultCode::Ok);
  EXPECT(library.load_photo(photo.id, row) == pixcake::ResultCode::Ok);
  EXPECT(row->presence == pixcake::kPresenceEditable);
  EXPECT(reloaded.set_preview("color.global", "exposure", "2.00") == pixcake::ResultCode::Ok);
  EXPECT(reloaded.commit_preview(reloaded.revision(), "color.global", "exposure", limits(), files, kEdit) ==
         pixcake::ResultCode::Ok);

  const pixcake::ProjectResult denied_photo =
      library.add_photo(project.id, "/pictures/denied.png", "denied.png", credential_bytes());
  const pixcake::ProjectResult gone_photo =
      library.add_photo(project.id, "/pictures/gone.png", "gone.png", {});
  EXPECT(denied_photo.code == pixcake::ResultCode::Ok);
  EXPECT(gone_photo.code == pixcake::ResultCode::Ok);
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
