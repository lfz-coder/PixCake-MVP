#include "pixcake/image_import/import_coordinator.hpp"
#include "pixcake/storage/sqlite_project_store.hpp"
#include "pixcake/support/clock.hpp"
#include "pixcake/support/log_sink.hpp"
#include "pixcake/project/source_deleter.hpp"
#include "support/fake_file_system.hpp"
#include "support/temp_dir.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <vector>

namespace {

int g_failures = 0;

void expect(bool condition, const char* expression, int line) {
  if (!condition) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, expression);
    ++g_failures;
  }
}

#define EXPECT(expression) expect(static_cast<bool>(expression), #expression, __LINE__)

struct FixedClock final : pixcake::Clock {
  std::int64_t unix_seconds() const override { return 1700000000; }
};

struct SilentLog final : pixcake::LogSink {
  void write(const pixcake::LogRecord&) override {}
};

struct UnusedDeleter final : pixcake::SourceDeleter {
  pixcake::ResultCode remove_source(const std::string&) override { return pixcake::ResultCode::StateConflict; }
};

std::vector<std::uint8_t> png_header(std::uint32_t width, std::uint32_t height) {
  std::vector<std::uint8_t> bytes = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
  bytes.resize(33, 0);
  bytes[8] = 0;
  bytes[9] = 0;
  bytes[10] = 0;
  bytes[11] = 13;
  bytes[12] = 'I';
  bytes[13] = 'H';
  bytes[14] = 'D';
  bytes[15] = 'R';
  bytes[16] = static_cast<std::uint8_t>(width >> 24);
  bytes[17] = static_cast<std::uint8_t>(width >> 16);
  bytes[18] = static_cast<std::uint8_t>(width >> 8);
  bytes[19] = static_cast<std::uint8_t>(width);
  bytes[20] = static_cast<std::uint8_t>(height >> 24);
  bytes[21] = static_cast<std::uint8_t>(height >> 16);
  bytes[22] = static_cast<std::uint8_t>(height >> 8);
  bytes[23] = static_cast<std::uint8_t>(height);
  return bytes;
}

}  // namespace

int main() {
  pixcake::test::TempDir directory;
  std::unique_ptr<pixcake::SqliteProjectStore> store;
  EXPECT(pixcake::SqliteProjectStore::open(directory.path() + "/library.db", store) == pixcake::ResultCode::Ok);
  FixedClock clock;
  SilentLog log;
  UnusedDeleter deleter;
  pixcake::test::FakeFileSystem files;
  files.files["/photos/ok.png"] = png_header(100, 50);
  files.files["/photos/also.png"] = png_header(20, 10);
  files.files["/photos/broken.png"] = {0x89, 'P', 'N'};
  pixcake::ProjectLibrary project(*store, files, clock, deleter, &log);
  const pixcake::ProjectResult created = project.create_project("导入", pixcake::kOriginLocal, false);
  EXPECT(created.code == pixcake::ResultCode::Ok);
  pixcake::image_import::ImportCoordinator coordinator(project, files);

  const auto empty = coordinator.import_into(created.id, {});
  EXPECT(empty.overall_code == pixcake::ResultCode::StateConflict);
  EXPECT(empty.items.empty());
  EXPECT(empty.accepted_count == 0);

  const std::vector<pixcake::image_import::ImportCandidate> candidates = {
      {"/photos/ok.png", "ok.png", {1, 2}},
      {"/photos/broken.png", "broken.png", {}},
      {"/photos/missing.png", "missing.png", {}},
      {"/photos/also.png", "also.png", {3}},
  };
  const auto batch = coordinator.import_into(created.id, candidates);
  EXPECT(batch.overall_code == pixcake::ResultCode::ImageCorrupt);
  EXPECT(batch.items.size() == 4);
  EXPECT(batch.accepted_count == 2);
  EXPECT(batch.items[0].code == pixcake::ResultCode::Ok && batch.items[0].photo_id > 0);
  EXPECT(batch.items[0].probed && batch.items[0].source.width == 100 && batch.items[0].source.identity.valid);
  EXPECT(batch.items[1].code == pixcake::ResultCode::ImageCorrupt && !batch.items[1].probed);
  EXPECT(batch.items[2].code == pixcake::ResultCode::SourceMissing && !batch.items[2].probed);
  EXPECT(batch.items[3].code == pixcake::ResultCode::Ok && batch.items[3].photo_id > 0);
  std::vector<pixcake::PhotoRow> photos;
  EXPECT(project.list_photos(std::nullopt, photos) == pixcake::ResultCode::Ok);
  EXPECT(photos.size() == 2);
  EXPECT(photos[0].source_identity.valid && photos[1].source_identity.valid);
  EXPECT(files.writes.empty());

  const auto bad_project = coordinator.import_into(999, {{"/photos/ok.png", "again.png", {}}});
  EXPECT(bad_project.overall_code == pixcake::ResultCode::StateConflict);
  EXPECT(bad_project.items.size() == 1 && bad_project.items[0].probed);
  EXPECT(bad_project.accepted_count == 0);
  EXPECT(project.list_photos(std::nullopt, photos) == pixcake::ResultCode::Ok);
  EXPECT(photos.size() == 2);
  return g_failures == 0 ? 0 : 1;
}
