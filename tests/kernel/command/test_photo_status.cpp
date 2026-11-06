#include "pixcake/command/command_gate.hpp"
#include "pixcake/command/photo_status.hpp"
#include "pixcake/project/project_library.hpp"
#include "pixcake/project/source_deleter.hpp"
#include "pixcake/storage/sqlite_project_store.hpp"
#include "pixcake/support/clock.hpp"
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

struct FixedClock : pixcake::Clock {
  std::int64_t unix_seconds() const override { return 1700000123; }
};

struct NoDeleter : pixcake::SourceDeleter {
  pixcake::ResultCode remove_source(const std::string&) override { return pixcake::ResultCode::Ok; }
};

struct Off : pixcake::FastExportLatch {
  bool enabled() const override { return false; }
};

struct RecordingLog : pixcake::LogSink {
  std::vector<pixcake::LogRecord> records;
  void write(const pixcake::LogRecord& record) override { records.push_back(record); }
};

struct World {
  pixcake::test::TempDir dir;
  std::unique_ptr<pixcake::SqliteProjectStore> store;
  pixcake::test::FakeFileSystem files;
  FixedClock clock;
  NoDeleter deleter;
  std::unique_ptr<pixcake::ProjectLibrary> library;
  std::int64_t photo = 0;
  std::int64_t other = 0;

  World() {
    EXPECT(pixcake::SqliteProjectStore::open(dir.path() + "/library.db", store) == pixcake::ResultCode::Ok);
    library = std::make_unique<pixcake::ProjectLibrary>(*store, files, clock, deleter, nullptr);
    const auto project = library->create_project("相册", pixcake::kOriginLocal, false);
    photo = library->add_photo(project.id, pixcake::test::admitted_png(files, "/p/a.png"), "a.png", {1}).id;
    other = library->add_photo(project.id, pixcake::test::admitted_png(files, "/p/b.png"), "b.png", {1}).id;
  }

  pixcake::PhotoRow row(std::int64_t id) {
    std::optional<pixcake::PhotoRow> loaded;
    EXPECT(library->load_photo(id, loaded) == pixcake::ResultCode::Ok && loaded.has_value());
    return loaded.value_or(pixcake::PhotoRow{});
  }
};

pixcake::UserCommand command(const char* name, std::uint64_t revision, const char* value) {
  pixcake::UserCommand c;
  c.name = name;
  c.target_id = "img-1";
  c.revision = revision;
  c.capability_id = "color.global";
  c.parameter_id = "exposure";
  c.display_value = value;
  return c;
}

const std::vector<pixcake::VersionLimit> kLimits = {{"color.global", 1}};
const char* kPath = "docs/img-1.pxedit";

// P-12 / 需求 §6.4：修图状态归命令入口的提交（性能清单第 55 行）。预览不算，提交成功才记“已编辑”。
void test_p12_commit_marks_photo_edited() {
  World world;
  auto document = *pixcake::EditDocument::create("img-1");
  pixcake::test::FakeFileSystem doc_files;
  EXPECT(document.write_snapshot(doc_files, kPath) == pixcake::ResultCode::Ok);
  Off latch;
  pixcake::CommandGate gate(document, latch, doc_files, kPath, kLimits, nullptr);
  pixcake::PhotoEditRecorder recorder(*world.library, world.photo, world.clock, nullptr);
  gate.set_commit_listener(&recorder);
  EXPECT(gate.submit(command("preview", 0, "1.00")).code == pixcake::ResultCode::Ok);
  EXPECT(world.row(world.photo).edit_status.empty());
  EXPECT(gate.submit(command("commit", 0, "1.00")).code == pixcake::ResultCode::Ok);
  const auto edited = world.row(world.photo);
  EXPECT(edited.edit_status == pixcake::kEditStatusEdited && edited.edited_at == 1700000123);
  EXPECT(world.row(world.other).edit_status.empty());
}

void test_p12_failed_commit_does_not_mark_edited() {
  World world;
  auto document = *pixcake::EditDocument::create("img-1");
  pixcake::test::FakeFileSystem doc_files;
  EXPECT(document.write_snapshot(doc_files, kPath) == pixcake::ResultCode::Ok);
  Off latch;
  pixcake::CommandGate gate(document, latch, doc_files, kPath, kLimits, nullptr);
  pixcake::PhotoEditRecorder recorder(*world.library, world.photo, world.clock, nullptr);
  gate.set_commit_listener(&recorder);
  EXPECT(gate.submit(command("preview", 0, "1.00")).code == pixcake::ResultCode::Ok);
  doc_files.write_error = pixcake::ResultCode::DiskFull;
  EXPECT(gate.submit(command("commit", 0, "1.00")).code == pixcake::ResultCode::DiskFull);
  EXPECT(world.row(world.photo).edit_status.empty());
  doc_files.write_error = pixcake::ResultCode::Ok;
  EXPECT(gate.submit(command("commit", 5, "1.00")).code == pixcake::ResultCode::StateConflict);
  EXPECT(world.row(world.photo).edit_status.empty());
}

// 状态记录失败不改变提交结果（提交已经落盘），但写一条日志。
void test_p12_status_write_failure_is_logged() {
  World world;
  auto document = *pixcake::EditDocument::create("img-1");
  pixcake::test::FakeFileSystem doc_files;
  EXPECT(document.write_snapshot(doc_files, kPath) == pixcake::ResultCode::Ok);
  Off latch;
  RecordingLog log;
  pixcake::CommandGate gate(document, latch, doc_files, kPath, kLimits, nullptr);
  pixcake::PhotoEditRecorder recorder(*world.library, 999, world.clock, &log);
  gate.set_commit_listener(&recorder);
  EXPECT(gate.submit(command("preview", 0, "1.00")).code == pixcake::ResultCode::Ok);
  EXPECT(gate.submit(command("commit", 0, "1.00")).code == pixcake::ResultCode::Ok);
  EXPECT(document.revision() == 1);
  EXPECT(log.records.size() == 1 && log.records[0].event == "project.status.failed" &&
         log.records[0].module == "command");
}

// 导出状态归导出事件（性能清单第 55 行）：成品存在的任务把对应图片记为“已导出”。
void test_p12_exported_products_mark_photos_exported() {
  World world;
  const std::map<std::string, std::int64_t> originals = {{"img-a", world.photo}, {"img-b", world.other}};
  const auto photo_of = [&originals](const std::string& original_id) -> std::optional<std::int64_t> {
    const auto found = originals.find(original_id);
    if (found == originals.end()) return std::nullopt;
    return found->second;
  };
  const std::vector<pixcake::ExportProductFact> products = {
      {"export-1", "img-a", false, true, ""},
      {"export-2", "img-b", false, false, ""},
      {"export-3", "img-unknown", false, true, ""},
  };
  EXPECT(pixcake::record_export_statuses(products, *world.library, photo_of) == pixcake::ResultCode::Ok);
  EXPECT(world.row(world.photo).export_status == pixcake::kExportStatusExported);
  EXPECT(world.row(world.other).export_status.empty());
}

}  // namespace

int main() {
  test_p12_commit_marks_photo_edited();
  test_p12_failed_commit_does_not_mark_edited();
  test_p12_status_write_failure_is_logged();
  test_p12_exported_products_mark_photos_exported();
  if (g_failures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  return 0;
}
