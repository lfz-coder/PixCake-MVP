#include "pixcake/command/command_gate.hpp"
#include "pixcake/exportq/export_book.hpp"
#include "pixcake/exportq/export_queue.hpp"
#include "pixcake/exportq/export_settings.hpp"
#include "pixcake/ledger/credit_ledger.hpp"
#include "pixcake/storage/sqlite_export_log.hpp"
#include "pixcake/support/counting_log_sink.hpp"
#include "support/fake_file_system.hpp"
#include "support/temp_dir.hpp"

#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sqlite3.h>

namespace {

int g_failures = 0;

void expect(bool cond, const char* text, int line) {
  if (!cond) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, text);
    ++g_failures;
  }
}

#define EXPECT(cond) expect(static_cast<bool>(cond), #cond, __LINE__)

pixcake::ExportJob job_of(std::string export_id, std::string original_id, std::string destination) {
  pixcake::ExportJob job;
  job.export_id = std::move(export_id);
  job.original_id = std::move(original_id);
  job.document_revision = 7;
  job.document_snapshot = {7, 7, 7};
  job.destination_path = std::move(destination);
  job.settings_snapshot = "quality=8";
  job.watermark_free_second = false;
  job.print_requested = false;
  return job;
}

const std::vector<std::uint8_t> kBytes = {1, 2, 3, 4};
const std::vector<std::uint8_t> kMismatched = {9, 9, 9};

bool has_kind(const pixcake::ExportQueue& queue, const char* kind) {
  for (const auto& event : queue.events()) {
    if (event.kind == kind) {
      return true;
    }
  }
  return false;
}

// Rename can land bytes that disagree with the start-event checksum, and removing
// that destination can fail. FakeFileSystem always removes a file that exists.
class MismatchFileSystem : public pixcake::test::FakeFileSystem {
 public:
  std::string guarded;
  bool corrupt_on_rename = false;
  pixcake::ResultCode remove_error = pixcake::ResultCode::Ok;

  pixcake::IoResult rename_file(const std::string& from, const std::string& to) override {
    const pixcake::IoResult renamed = pixcake::test::FakeFileSystem::rename_file(from, to);
    if (renamed.ok && corrupt_on_rename && to == guarded) {
      files[to] = kMismatched;
    }
    return renamed;
  }

  pixcake::IoResult remove_file(const std::string& path) override {
    if (!guarded.empty() && path == guarded && remove_error != pixcake::ResultCode::Ok) {
      return pixcake::io_error(remove_error);
    }
    return pixcake::test::FakeFileSystem::remove_file(path);
  }
};

// The sync before the checksum check stays successful. Only a directory sync
// after the bad destination has been unlinked fails.
class PostUnlinkSyncFileSystem : public MismatchFileSystem {
 public:
  pixcake::ResultCode post_unlink_sync_error = pixcake::ResultCode::Ok;
  bool destination_unlinked = false;

  pixcake::IoResult remove_file(const std::string& path) override {
    const pixcake::IoResult removed = MismatchFileSystem::remove_file(path);
    if (removed.ok && !guarded.empty() && path == guarded) {
      destination_unlinked = true;
    }
    return removed;
  }

  pixcake::IoResult sync_directory(const std::string& path) override {
    if (destination_unlinked && post_unlink_sync_error != pixcake::ResultCode::Ok) {
      return pixcake::io_error(post_unlink_sync_error);
    }
    return pixcake::test::FakeFileSystem::sync_directory(path);
  }
};

class RaceDestinationFileSystem : public pixcake::test::FakeFileSystem {
 public:
  std::string destination;
  std::vector<std::uint8_t> raced_bytes;
  bool race_on_publish = false;

  pixcake::IoResult rename_file(const std::string& from, const std::string& to) override {
    if (race_on_publish && to == destination) {
      files[destination] = raced_bytes;
      return pixcake::io_error(pixcake::ResultCode::StateConflict);
    }
    return pixcake::test::FakeFileSystem::rename_file(from, to);
  }
};

void reach_render(pixcake::ExportQueue& queue, const pixcake::ExportJob& job) {
  EXPECT(queue.enqueue(job) == pixcake::ResultCode::Ok);
  EXPECT(queue.prepare(job.export_id) == pixcake::ResultCode::Ok);
  EXPECT(queue.start_rendering(job.export_id) == pixcake::ResultCode::Ok);
}

void test_b66_snapshot_and_fast_export_stay() {
  pixcake::test::FakeFileSystem files;
  auto queue = pixcake::ExportQueue::open("/cache/exports");
  auto job = job_of("exp-1", "orig-1", "/out/a.jpg");
  EXPECT(queue.enqueue(job) == pixcake::ResultCode::Ok);
  job.document_revision = 99;
  job.settings_snapshot = "quality=1";
  const auto before = queue.events().size();
  EXPECT(queue.enqueue(job) == pixcake::ResultCode::Ok);
  EXPECT(queue.events().size() == before);
  pixcake::ExportJob stored;
  EXPECT(queue.find_job("exp-1", stored));
  EXPECT(stored.document_revision == 7);
  EXPECT(stored.settings_snapshot == "format=original;quality=8");

  EXPECT(queue.set_fast_export(true) == pixcake::ResultCode::Ok);
  EXPECT(queue.enabled());
  EXPECT(queue.set_fast_export(true) == pixcake::ResultCode::Ok);
  reach_render(queue, job_of("exp-1", "orig-1", "/out/a.jpg"));
  EXPECT(queue.commit_product("exp-1", kBytes, files) == pixcake::ResultCode::Ok);
  EXPECT(queue.status("exp-1") == "成功");
  EXPECT(queue.enabled());

  auto document = *pixcake::EditDocument::create("img-1");
  pixcake::CommandGate gate(document, queue, files, "/docs/img.pxedit",
                            {pixcake::VersionLimit{"color.global", 1}}, nullptr);
  pixcake::UserCommand preview;
  preview.name = "preview";
  preview.target_id = "img-1";
  preview.revision = 0;
  preview.capability_id = "color.global";
  preview.parameter_id = "exposure";
  preview.display_value = "0";
  EXPECT(gate.submit(preview).code == pixcake::ResultCode::FastExportBusy);
  preview.name = "commit";
  EXPECT(gate.submit(preview).code == pixcake::ResultCode::FastExportBusy);
  EXPECT(queue.set_fast_export(false) == pixcake::ResultCode::Ok);
  EXPECT(!queue.enabled());
  preview.name = "preview";
  EXPECT(gate.submit(preview).code == pixcake::ResultCode::Ok);
}

void test_b32_b47_export_settings_parse_strictly() {
  pixcake::ExportSettings settings;
  for (const char* accepted : {"", "format=jpg;quality=14", "format=jpg;size_limit_kb=500", "copies=2",
                               "format=tiff;srgb=1", "format=tiff16"}) {
    if (pixcake::parse_export_settings(accepted, settings) != pixcake::ResultCode::Ok) {
      std::fprintf(stderr, "rejected valid export settings: %s\n", accepted);
      EXPECT(false);
    }
  }
  for (const char* rejected : {"quality=15", "quality=", "quality=3.5", "quality=-1", "quality=3;quality=4",
                               "quality=3;size_limit_kb=100", "size_limit_kb=0", "format=png", "unknown=1",
                               "quality = 3", "srgb=2", "copies=-1", ";", "quality"}) {
    if (pixcake::parse_export_settings(rejected, settings) != pixcake::ResultCode::StateConflict) {
      std::fprintf(stderr, "accepted invalid export settings: %s\n", rejected);
      EXPECT(false);
    }
  }
  EXPECT(pixcake::parse_export_settings("format=jpg;quality=8;srgb=1;copies=3", settings) == pixcake::ResultCode::Ok);
  EXPECT(settings.format == pixcake::ExportFormat::Jpg);
  EXPECT(settings.quality == std::optional<int>(8));
  EXPECT(!settings.size_limit_kb.has_value());
  EXPECT(settings.convert_to_srgb);
  EXPECT(settings.print_copies == 3);
  EXPECT(pixcake::parse_export_settings("format=tiff16", settings) == pixcake::ResultCode::Ok);
  EXPECT(settings.format == pixcake::ExportFormat::Tiff16);
  EXPECT(pixcake::encode_export_settings(settings) == "format=tiff16");

  // Watermark: the material and all three placements round-trip; a missing
  // placement, a value out of range or a material that is not a stored name is refused.
  {
    pixcake::ExportSettings marked;
    marked.format = pixcake::ExportFormat::Jpg;
    marked.watermark.emplace();
    marked.watermark->material = "0123456789abcdef-2048.png";
    marked.watermark->placements[1] = {25, true, 50, 4, -10, 100, 3};
    const std::string text = pixcake::encode_export_settings(marked);
    EXPECT(text == "format=jpg;wm=0123456789abcdef-2048.png;wm.l=10,0,100,2,3,3,0;wm.p=25,1,50,4,-10,100,3;"
                   "wm.s=10,0,100,2,3,3,0");
    pixcake::ExportSettings back;
    EXPECT(pixcake::parse_export_settings(text, back) == pixcake::ResultCode::Ok);
    EXPECT(back.watermark.has_value() && *back.watermark == *marked.watermark);
    for (const char* bad : {"format=jpg;wm=0123456789abcdef-2048.png;wm.l=10,0,100,2,3,3,0;wm.p=10,0,100,2,3,3,0",
                            "format=jpg;wm.l=10,0,100,2,3,3,0;wm.p=10,0,100,2,3,3,0;wm.s=10,0,100,2,3,3,0",
                            "format=jpg;wm=../x.png;wm.l=10,0,100,2,3,3,0;wm.p=10,0,100,2,3,3,0;wm.s=10,0,100,2,3,3,0",
                            "format=jpg;wm=0123456789abcdef-2048.png;wm.l=101,0,100,2,3,3,0;wm.p=10,0,100,2,3,3,0;"
                            "wm.s=10,0,100,2,3,3,0",
                            "format=jpg;wm=0123456789abcdef-2048.png;wm.l=10,0,100,9,3,3,0;wm.p=10,0,100,2,3,3,0;"
                            "wm.s=10,0,100,2,3,3,0",
                            "format=jpg;wm=0123456789abcdef-2048.png;wm.l=10,0,100,2,3,3;wm.p=10,0,100,2,3,3,0;"
                            "wm.s=10,0,100,2,3,3,0"}) {
      EXPECT(pixcake::parse_export_settings(bad, back) == pixcake::ResultCode::StateConflict);
    }
    EXPECT(pixcake::watermark_shape(4000, 3000) == pixcake::WatermarkShape::Landscape);
    EXPECT(pixcake::watermark_shape(1024, 1536) == pixcake::WatermarkShape::Portrait);
    EXPECT(pixcake::watermark_shape(1000, 995) == pixcake::WatermarkShape::Square);
  }
}

void test_b32_enqueue_rejects_duplicate_quality() {
  auto queue = pixcake::ExportQueue::open("/cache/exports");
  auto job = job_of("dup-quality", "orig", "/out/dup.jpg");
  job.settings_snapshot = "quality=3;quality=99";
  EXPECT(queue.enqueue(job) == pixcake::ResultCode::StateConflict);
  pixcake::ExportJob stored;
  EXPECT(!queue.find_job("dup-quality", stored));
}

void test_b32_enqueue_stores_canonical_settings() {
  auto queue = pixcake::ExportQueue::open("/cache/exports");
  auto job = job_of("canonical", "orig", "/out/canonical.jpg");
  job.settings_snapshot = "quality=8;format=jpg";
  EXPECT(queue.enqueue(job) == pixcake::ResultCode::Ok);
  pixcake::ExportJob stored;
  EXPECT(queue.find_job("canonical", stored));
  EXPECT(stored.settings_snapshot == "format=jpg;quality=8");
}

void test_quality_is_limited_by_export_contract() {
  auto queue = pixcake::ExportQueue::open("/cache/exports");
  auto invalid = job_of("quality-bad", "orig", "/out/quality.jpg");
  invalid.settings_snapshot = "quality=15";
  EXPECT(queue.enqueue(invalid) == pixcake::ResultCode::StateConflict);
  auto valid = job_of("quality-good", "orig", "/out/quality.jpg");
  valid.settings_snapshot = "quality=14";
  EXPECT(queue.enqueue(valid) == pixcake::ResultCode::Ok);
}

void test_b63_stale_export_handle_cannot_publish_after_cancel() {
  pixcake::test::TempDir dir;
  const std::string db = dir.path() + "/export.db";
  const std::string cache = dir.path() + "/cache";
  const std::string destination = dir.path() + "/out.jpg";
  pixcake::PosixFileSystem files;
  std::unique_ptr<pixcake::SqliteExportLog> first_log;
  std::unique_ptr<pixcake::SqliteExportLog> second_log;
  EXPECT(pixcake::SqliteExportLog::open(db, first_log) == pixcake::ResultCode::Ok);
  EXPECT(pixcake::SqliteExportLog::open(db, second_log) == pixcake::ResultCode::Ok);
  std::unique_ptr<pixcake::ExportBook> first;
  std::unique_ptr<pixcake::ExportBook> second;
  EXPECT(pixcake::ExportBook::open(cache, *first_log, files, first) == pixcake::ResultCode::Ok);
  EXPECT(first->queue().enqueue(job_of("stale-export", "original", destination)) == pixcake::ResultCode::Ok);
  EXPECT(first->queue().prepare("stale-export") == pixcake::ResultCode::Ok);
  EXPECT(first->queue().start_rendering("stale-export") == pixcake::ResultCode::Ok);
  EXPECT(pixcake::ExportBook::open(cache, *second_log, files, second) == pixcake::ResultCode::Ok);
  EXPECT(second->queue().cancel("stale-export") == pixcake::ResultCode::Ok);
  EXPECT(first->queue().commit_product("stale-export", kBytes, files) == pixcake::ResultCode::StateConflict);
  EXPECT(!files.exists(destination));
}


void test_enqueue_requires_document_snapshot() {
  auto queue = pixcake::ExportQueue::open("/cache/exports");
  auto job = job_of("exp-empty", "orig-empty", "/out/empty.jpg");
  job.document_snapshot.clear();
  EXPECT(queue.enqueue(job) == pixcake::ResultCode::StateConflict);
  EXPECT(queue.events().empty());
}

void test_commit_once_and_reject_a_different_destination() {
  pixcake::test::FakeFileSystem files;
  auto queue = pixcake::ExportQueue::open("/cache/exports");
  auto job = job_of("exp-1", "orig-1", "/out/a.jpg");
  const auto rejected = queue.events().size();
  auto relative = job;
  relative.export_id = "exp-rel";
  relative.destination_path = "out/a.jpg";
  EXPECT(queue.enqueue(relative) == pixcake::ResultCode::StateConflict);
  auto slash = job;
  slash.export_id = "a/b";
  EXPECT(queue.enqueue(slash) == pixcake::ResultCode::StateConflict);
  auto traversal = job;
  traversal.export_id = "exp-traversal";
  traversal.destination_path = "/out/selected/../outside.jpg";
  EXPECT(queue.enqueue(traversal) == pixcake::ResultCode::StateConflict);
  EXPECT(queue.events().size() == rejected);

  reach_render(queue, job);
  files.files["/out/a.jpg"] = {9, 9};
  const auto occupied = queue.events().size();
  EXPECT(queue.commit_product("exp-1", kBytes, files) == pixcake::ResultCode::StateConflict);
  EXPECT(queue.events().size() == occupied);
  EXPECT(files.files["/out/a.jpg"] == std::vector<std::uint8_t>({9, 9}));
  EXPECT(queue.status("exp-1") == "渲染");

  files.files["/out/a.jpg"] = kBytes;
  EXPECT(queue.commit_product("exp-1", kBytes, files) == pixcake::ResultCode::StateConflict);
  EXPECT(queue.status("exp-1") == "渲染");
  EXPECT(files.writes.empty());
  EXPECT(queue.products().empty());
}

void test_b94_disk_full_then_retry_uses_the_snapshot() {
  pixcake::test::FakeFileSystem files;
  auto queue = pixcake::ExportQueue::open("/cache/exports");
  auto job = job_of("exp-1", "orig-1", "/out/a.jpg");
  reach_render(queue, job);
  files.write_error = pixcake::ResultCode::DiskFull;
  const auto before = queue.events().size();
  EXPECT(queue.commit_product("exp-1", kBytes, files) == pixcake::ResultCode::DiskFull);
  EXPECT(queue.events().size() == before + 2);
  EXPECT(queue.status("exp-1") == "失败");
  EXPECT(files.files.find("/out/a.jpg") == files.files.end());

  auto other = job_of("exp-2", "orig-2", "/out/b.jpg");
  files.write_error = pixcake::ResultCode::Ok;
  reach_render(queue, other);
  EXPECT(queue.commit_product("exp-2", kBytes, files) == pixcake::ResultCode::Ok);
  std::vector<std::uint8_t> written;
  EXPECT(files.read_file("/out/b.jpg", written).ok);
  EXPECT(written == kBytes);

  job.settings_snapshot = "quality=0";
  EXPECT(queue.retry("exp-1") == pixcake::ResultCode::Ok);
  pixcake::ExportJob stored;
  EXPECT(queue.find_job("exp-1", stored));
  EXPECT(stored.settings_snapshot == "format=original;quality=8");
  EXPECT(stored.document_revision == 7);
  EXPECT(queue.prepare("exp-1") == pixcake::ResultCode::Ok);
  EXPECT(queue.start_rendering("exp-1") == pixcake::ResultCode::Ok);
  EXPECT(queue.commit_product("exp-1", kBytes, files) == pixcake::ResultCode::Ok);
  EXPECT(queue.status("exp-1") == "成功");
}


void test_b11_b53_cancel_removes_temp_and_print_is_unavailable() {
  pixcake::test::FakeFileSystem files;
  auto queue = pixcake::ExportQueue::open("/cache/exports");
  auto job = job_of("exp-1", "orig-1", "/out/layout.jpg");
  job.print_requested = true;
  job.settings_snapshot = "copies=2";
  reach_render(queue, job);
  pixcake::ExportJob stored;
  EXPECT(queue.find_job("exp-1", stored));
  EXPECT(stored.settings_snapshot == "format=original;copies=2");
  EXPECT(queue.cancel("exp-1") == pixcake::ResultCode::Ok);
  EXPECT(queue.status("exp-1") == "取消");
  EXPECT(files.files.count("/cache/exports/exp-1") == 0);
  EXPECT(files.files.count("/out/layout.jpg") == 0);
  EXPECT(queue.products().empty());
  const auto cancelled = queue.events().size();
  EXPECT(queue.cancel("exp-1") == pixcake::ResultCode::Ok);
  EXPECT(queue.events().size() == cancelled);

  auto printed = pixcake::ExportQueue::open("/cache/exports");
  auto layout = job_of("exp-p", "orig-p", "/out/layout.jpg");
  layout.print_requested = true;
  reach_render(printed, layout);
  EXPECT(printed.commit_product("exp-p", kBytes, files) == pixcake::ResultCode::Ok);
  bool unavailable = false;
  EXPECT(printed.request_print("exp-p", false, unavailable) == pixcake::ResultCode::Ok);
  EXPECT(unavailable);
  std::vector<std::uint8_t> written;
  EXPECT(files.read_file("/out/layout.jpg", written).ok);
  EXPECT(written == kBytes);
  unavailable = true;
  EXPECT(printed.request_print("exp-p", true, unavailable) == pixcake::ResultCode::Ok);
  EXPECT(!unavailable);
  EXPECT(printed.request_print("missing", false, unavailable) == pixcake::ResultCode::StateConflict);
}

void test_b110_product_facts_match_one_settlement() {
  pixcake::test::FakeFileSystem files;
  auto queue = pixcake::ExportQueue::open("/cache/exports");
  auto normal = job_of("exp-a", "orig-1", "/out/a.jpg");
  auto second = job_of("exp-b", "orig-1", "/out/b.jpg");
  second.watermark_free_second = true;
  reach_render(queue, normal);
  EXPECT(queue.commit_product("exp-a", kBytes, files) == pixcake::ResultCode::Ok);
  reach_render(queue, second);
  EXPECT(queue.commit_product("exp-b", kBytes, files) == pixcake::ResultCode::Ok);
  EXPECT(queue.fail("exp-a") == pixcake::ResultCode::StateConflict);

  pixcake::FreeQueuePolicy policy;
  auto ledger = pixcake::CreditLedger::open(policy, {});
  EXPECT(ledger.credit_sheets(2) == pixcake::ResultCode::Ok);
  std::vector<pixcake::ExportProduct> batch;
  for (const auto& fact : queue.products()) {
    batch.push_back({fact.export_id, fact.original_id, fact.watermark_free_second, fact.product_exists});
  }
  EXPECT(ledger.settle_exports(batch) == pixcake::ResultCode::Ok);
  EXPECT(ledger.export_sheets() == 1);
  EXPECT(ledger.settle_exports(batch) == pixcake::ResultCode::Ok);
  EXPECT(ledger.export_sheets() == 1);
}

void test_commit_returns_remove_error_when_mismatched_destination_cannot_be_deleted() {
  MismatchFileSystem files;
  files.guarded = "/out/a.jpg";
  files.corrupt_on_rename = true;
  files.remove_error = pixcake::ResultCode::PermissionDenied;

  auto queue = pixcake::ExportQueue::open("/cache/exports");
  reach_render(queue, job_of("exp-1", "orig-1", "/out/a.jpg"));
  const auto code = queue.commit_product("exp-1", kBytes, files);
  EXPECT(code == pixcake::ResultCode::IoFailed);
  EXPECT(queue.status("exp-1") == "失败");
  EXPECT(has_kind(queue, "fail"));
  const auto destination = files.files.find("/out/a.jpg");
  EXPECT(destination != files.files.end());
  EXPECT(destination->second == kMismatched);
}


void test_commit_returns_sync_error_after_unlinking_mismatched_destination() {
  PostUnlinkSyncFileSystem files;
  files.guarded = "/out/a.jpg";
  files.corrupt_on_rename = true;
  files.post_unlink_sync_error = pixcake::ResultCode::DiskFull;

  auto queue = pixcake::ExportQueue::open("/cache/exports");
  reach_render(queue, job_of("exp-1", "orig-1", "/out/a.jpg"));
  const auto code = queue.commit_product("exp-1", kBytes, files);
  EXPECT(code == pixcake::ResultCode::IoFailed);
  EXPECT(queue.status("exp-1") == "失败");
  EXPECT(has_kind(queue, "fail"));
  EXPECT(files.files["/out/a.jpg"] == kMismatched);
}






void test_b99_publish_does_not_overwrite_a_file_that_appears_after_preflight() {
  RaceDestinationFileSystem files;
  files.destination = "/out/race.jpg";
  files.raced_bytes = {9, 9, 9};
  files.race_on_publish = true;
  auto queue = pixcake::ExportQueue::open("/cache/exports");
  reach_render(queue, job_of("exp-race", "orig-race", files.destination));
  EXPECT(queue.commit_product("exp-race", kBytes, files) == pixcake::ResultCode::StateConflict);
  EXPECT(files.files[files.destination] == files.raced_bytes);
  EXPECT(queue.status("exp-race") == "提交到目标路径");
  EXPECT(queue.products().empty());
}

void test_enqueue_rejects_destination_outside_selected_root() {
  auto queue = pixcake::ExportQueue::open("/cache/exports");
  auto job = job_of("exp-root", "orig-root", "/selected/escape.jpg");
  job.destination_root = "/selected/root";
  EXPECT(queue.enqueue(job) == pixcake::ResultCode::StateConflict);
  EXPECT(queue.events().empty());
}


void test_duplicate_task_cache_key_is_state_conflict_and_one_row_round_trips() {
  pixcake::test::TempDir dir;
  const std::string path = dir.path() + "/export.db";
  std::unique_ptr<pixcake::SqliteExportLog> log;
  EXPECT(pixcake::SqliteExportLog::open(path, log) == pixcake::ResultCode::Ok);

  pixcake::ExportCache one;
  one.fast_export = true;
  one.statuses.emplace_back("exp-1", "排队");
  EXPECT(log->write_cache(one) == pixcake::ResultCode::Ok);
  std::optional<pixcake::ExportCache> read;
  EXPECT(log->read_cache(read) == pixcake::ResultCode::Ok);
  EXPECT(read.has_value());
  EXPECT(read->fast_export);
  EXPECT(read->statuses.size() == 1);
  EXPECT(read->statuses[0].first == "exp-1");
  EXPECT(read->statuses[0].second == "排队");

  pixcake::ExportCache duplicate;
  duplicate.fast_export = false;
  duplicate.statuses.emplace_back("exp-same", "排队");
  duplicate.statuses.emplace_back("exp-same", "成功");
  const pixcake::ResultCode code = log->write_cache(duplicate);
  if (code != pixcake::ResultCode::StateConflict) {
    std::fprintf(stderr, "duplicate export_id write_cache returned %d (%s), expected StateConflict\n",
                 static_cast<int>(code), pixcake::result_code_label(code));
  }
  EXPECT(code == pixcake::ResultCode::StateConflict);
}

}  // namespace

struct ExportRecords final : pixcake::LogSink {
  std::vector<pixcake::LogRecord> records;
  void write(const pixcake::LogRecord& record) override { records.push_back(record); }
};

// 临时文件第一次写入时，日志里已经有几条导出提交开始记录。
struct WatchingExportFiles final : pixcake::test::FakeFileSystem {
  const pixcake::CountingLogSink* log = nullptr;
  std::uint64_t starts_at_first_write = 99;
  pixcake::IoResult write_new(const std::string& path, const std::vector<std::uint8_t>& data) override {
    if (starts_at_first_write == 99) starts_at_first_write = log->count("export.commit.start");
    return FakeFileSystem::write_new(path, data);
  }
};

void test_o05_export_commit_start_and_finish() {
  ExportRecords kept;
  pixcake::CountingLogSink log(&kept);
  WatchingExportFiles files;
  files.log = &log;
  auto queue = pixcake::ExportQueue::open("/cache/exports");
  queue.set_log(&log);
  auto job = job_of("exp-1", "orig-1", "/out/a.jpg");
  reach_render(queue, job);
  EXPECT(queue.commit_product("exp-1", kBytes, files) == pixcake::ResultCode::Ok);
  EXPECT(files.starts_at_first_write == 1);
  EXPECT(log.count("export.commit.finish", pixcake::ResultCode::Ok) == 1);
  EXPECT(log.count("export.commit.failed") == 0);
  const pixcake::LogRecord* terminal = nullptr;
  for (const auto& record : kept.records) {
    if (record.event == "export.terminal") terminal = &record;
  }
  EXPECT(terminal != nullptr && terminal->task_id == "exp-1" && terminal->phase == "成功" &&
         terminal->module == "exportq" && terminal->path == "/out/a.jpg");
  EXPECT(queue.commit_product("exp-1", kBytes, files) == pixcake::ResultCode::Ok);
  EXPECT(log.count("export.commit.start") == 1);
}

void test_o08_failed_export_records_reason() {
  ExportRecords kept;
  pixcake::CountingLogSink log(&kept);
  pixcake::test::FakeFileSystem files;
  auto queue = pixcake::ExportQueue::open("/cache/exports");
  queue.set_log(&log);
  auto job = job_of("exp-1", "orig-1", "/out/a.jpg");
  reach_render(queue, job);
  files.write_error = pixcake::ResultCode::DiskFull;
  EXPECT(queue.commit_product("exp-1", kBytes, files) == pixcake::ResultCode::DiskFull);
  EXPECT(log.count("export.commit.failed", pixcake::ResultCode::DiskFull) == 1);
  EXPECT(log.count("export.commit.finish") == 0);
  EXPECT(log.count("export.terminal", pixcake::ResultCode::DiskFull) == 1);
  bool failed_terminal = false;
  for (const auto& record : kept.records) {
    failed_terminal = failed_terminal || (record.event == "export.terminal" && record.phase == "失败" &&
                                          record.task_id == "exp-1" && record.code == pixcake::ResultCode::DiskFull);
  }
  EXPECT(failed_terminal);
  EXPECT(queue.failure_reason("exp-1") == pixcake::ResultCode::DiskFull);

  auto other = job_of("exp-2", "orig-2", "/out/b.jpg");
  files.write_error = pixcake::ResultCode::Ok;
  reach_render(queue, other);
  EXPECT(queue.cancel("exp-2") == pixcake::ResultCode::Ok);
  EXPECT(!kept.records.empty() && kept.records.back().event == "export.terminal" &&
         kept.records.back().phase == "取消" && kept.records.back().task_id == "exp-2");
}

// B-03：导出时没有选中图片（任务不带原图编号）返回状态冲突，队列和事件都不变。
void test_b03_export_without_selected_image_is_refused() {
  auto queue = pixcake::ExportQueue::open("/cache/exports");
  const auto before = queue.events().size();
  EXPECT(queue.enqueue(job_of("exp-none", "", "/out/none.jpg")) == pixcake::ResultCode::StateConflict);
  pixcake::ExportJob stored;
  EXPECT(!queue.find_job("exp-none", stored));
  EXPECT(queue.events().size() == before);
}

int main() {
  test_b03_export_without_selected_image_is_refused();
  test_o05_export_commit_start_and_finish();
  test_o08_failed_export_records_reason();
  test_b32_b47_export_settings_parse_strictly();
  test_b32_enqueue_rejects_duplicate_quality();
  test_b32_enqueue_stores_canonical_settings();
  test_b66_snapshot_and_fast_export_stay();
  test_quality_is_limited_by_export_contract();
  test_b63_stale_export_handle_cannot_publish_after_cancel();
  test_enqueue_requires_document_snapshot();
  test_commit_once_and_reject_a_different_destination();
  test_b94_disk_full_then_retry_uses_the_snapshot();
  test_b11_b53_cancel_removes_temp_and_print_is_unavailable();
  test_b110_product_facts_match_one_settlement();
  test_commit_returns_remove_error_when_mismatched_destination_cannot_be_deleted();
  test_commit_returns_sync_error_after_unlinking_mismatched_destination();
  test_duplicate_task_cache_key_is_state_conflict_and_one_row_round_trips();
  test_b99_publish_does_not_overwrite_a_file_that_appears_after_preflight();
  test_enqueue_rejects_destination_outside_selected_root();
  if (g_failures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  return 0;
}
