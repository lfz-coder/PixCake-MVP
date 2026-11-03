#include "pixcake/command/command_gate.hpp"
#include "pixcake/exportq/export_book.hpp"
#include "pixcake/exportq/export_queue.hpp"
#include "pixcake/ledger/credit_ledger.hpp"
#include "pixcake/storage/sqlite_export_log.hpp"
#include "support/fake_file_system.hpp"
#include "support/temp_dir.hpp"

#include <cstdio>
#include <memory>
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

class RejectFailSink : public pixcake::ExportEventSink {
 public:
  pixcake::ResultCode code = pixcake::ResultCode::DiskFull;

  pixcake::ResultCode persist(const pixcake::ExportEvent& event, const pixcake::ExportCache&) override {
    if (event.kind == "fail") {
      return code;
    }
    return pixcake::ResultCode::Ok;
  }
};

void reach_render(pixcake::ExportQueue& queue, const pixcake::ExportJob& job) {
  EXPECT(queue.enqueue(job) == pixcake::ResultCode::Ok);
  EXPECT(queue.prepare(job.export_id) == pixcake::ResultCode::Ok);
  EXPECT(queue.start_rendering(job.export_id) == pixcake::ResultCode::Ok);
}

void test_snapshot_and_fast_export_stay() {
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
  EXPECT(stored.settings_snapshot == "quality=8");

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

void test_disk_full_then_retry_uses_the_snapshot() {
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
  EXPECT(stored.settings_snapshot == "quality=8");
  EXPECT(stored.document_revision == 7);
  EXPECT(queue.prepare("exp-1") == pixcake::ResultCode::Ok);
  EXPECT(queue.start_rendering("exp-1") == pixcake::ResultCode::Ok);
  EXPECT(queue.commit_product("exp-1", kBytes, files) == pixcake::ResultCode::Ok);
  EXPECT(queue.status("exp-1") == "成功");
}

void test_rename_failure_and_crash_points() {
  pixcake::test::FakeFileSystem files;
  auto queue = pixcake::ExportQueue::open("/cache/exports");
  reach_render(queue, job_of("exp-1", "orig-1", "/out/a.jpg"));
  files.rename_error = pixcake::ResultCode::IoFailed;
  EXPECT(queue.commit_product("exp-1", kBytes, files) == pixcake::ResultCode::IoFailed);
  EXPECT(queue.status("exp-1") == "提交到目标路径");
  EXPECT(files.files.count("/cache/exports/exp-1") == 1);
  EXPECT(files.files.count("/out/a.jpg") == 0);

  files.rename_error = pixcake::ResultCode::Ok;
  files.files["/out/a.jpg"] = {8};
  EXPECT(queue.recover(files) == pixcake::ResultCode::Ok);
  EXPECT(queue.status("exp-1") == "失败");
  EXPECT(files.files["/out/a.jpg"] == std::vector<std::uint8_t>({8}));
  EXPECT(files.files.count("/cache/exports/exp-1") == 1);
  EXPECT(queue.products().empty());

  EXPECT(files.remove_file("/out/a.jpg").ok);
  EXPECT(files.remove_file("/cache/exports/exp-1").ok);
  EXPECT(queue.retry("exp-1") == pixcake::ResultCode::Ok);
  EXPECT(queue.prepare("exp-1") == pixcake::ResultCode::Ok);
  EXPECT(queue.start_rendering("exp-1") == pixcake::ResultCode::Ok);
  files.rename_error = pixcake::ResultCode::IoFailed;
  EXPECT(queue.commit_product("exp-1", kBytes, files) == pixcake::ResultCode::IoFailed);
  files.rename_error = pixcake::ResultCode::Ok;
  EXPECT(queue.recover(files) == pixcake::ResultCode::Ok);
  EXPECT(queue.status("exp-1") == "成功");
  std::vector<std::uint8_t> written;
  EXPECT(files.read_file("/out/a.jpg", written).ok);
  EXPECT(written == kBytes);
  const auto finished = files.writes.size();
  const auto facts = queue.products().size();
  EXPECT(queue.recover(files) == pixcake::ResultCode::Ok);
  EXPECT(files.writes.size() == finished);
  EXPECT(queue.products().size() == facts);

  auto rendering = pixcake::ExportQueue::open("/cache/exports");
  reach_render(rendering, job_of("exp-r", "orig-r", "/out/r.jpg"));
  files.files["/out/r.jpg"] = kBytes;
  EXPECT(rendering.recover(files) == pixcake::ResultCode::Ok);
  EXPECT(rendering.status("exp-r") == "排队");
  EXPECT(files.files["/out/r.jpg"] == kBytes);
  EXPECT(rendering.products().empty());
}

void test_cancel_removes_temp_and_print_is_unavailable() {
  pixcake::test::FakeFileSystem files;
  auto queue = pixcake::ExportQueue::open("/cache/exports");
  auto job = job_of("exp-1", "orig-1", "/out/layout.jpg");
  job.print_requested = true;
  job.settings_snapshot = "copies=2";
  reach_render(queue, job);
  pixcake::ExportJob stored;
  EXPECT(queue.find_job("exp-1", stored));
  EXPECT(stored.settings_snapshot == "copies=2");
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

void test_product_facts_match_one_settlement() {
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
  EXPECT(code == pixcake::ResultCode::PermissionDenied);
  EXPECT(queue.status("exp-1") == "提交到目标路径");
  EXPECT(!has_kind(queue, "fail"));
  const auto destination = files.files.find("/out/a.jpg");
  EXPECT(destination != files.files.end());
  EXPECT(destination->second == kMismatched);
}

void test_rejected_fail_is_recorded_when_recover_finds_both_files_gone() {
  MismatchFileSystem files;
  files.guarded = "/out/a.jpg";
  files.corrupt_on_rename = true;
  RejectFailSink sink;

  auto queue = pixcake::ExportQueue::open("/cache/exports");
  queue.set_sink(&sink);
  reach_render(queue, job_of("exp-1", "orig-1", "/out/a.jpg"));
  const auto code = queue.commit_product("exp-1", kBytes, files);
  EXPECT(code == pixcake::ResultCode::DiskFull);
  EXPECT(queue.status("exp-1") == "提交到目标路径");
  EXPECT(!has_kind(queue, "fail"));
  EXPECT(files.files.count("/out/a.jpg") == 0);
  EXPECT(files.files.count("/cache/exports/exp-1") == 0);

  queue.set_sink(nullptr);
  EXPECT(queue.recover(files) == pixcake::ResultCode::Ok);
  EXPECT(queue.status("exp-1") == "失败");
  EXPECT(has_kind(queue, "fail"));
}

void test_commit_returns_sync_error_after_unlinking_mismatched_destination() {
  PostUnlinkSyncFileSystem files;
  files.guarded = "/out/a.jpg";
  files.corrupt_on_rename = true;
  files.post_unlink_sync_error = pixcake::ResultCode::DiskFull;

  auto queue = pixcake::ExportQueue::open("/cache/exports");
  reach_render(queue, job_of("exp-1", "orig-1", "/out/a.jpg"));
  const auto code = queue.commit_product("exp-1", kBytes, files);
  EXPECT(code == pixcake::ResultCode::DiskFull);
  EXPECT(queue.status("exp-1") == "提交到目标路径");
  EXPECT(!has_kind(queue, "fail"));
  EXPECT(files.files.count("/out/a.jpg") == 0);
}

void test_recover_returns_sync_error_after_unlinking_mismatched_destination() {
  PostUnlinkSyncFileSystem files;
  files.guarded = "/out/a.jpg";
  files.post_unlink_sync_error = pixcake::ResultCode::DiskFull;
  files.rename_error = pixcake::ResultCode::IoFailed;

  auto queue = pixcake::ExportQueue::open("/cache/exports");
  reach_render(queue, job_of("exp-1", "orig-1", "/out/a.jpg"));
  EXPECT(queue.commit_product("exp-1", kBytes, files) == pixcake::ResultCode::IoFailed);
  EXPECT(queue.status("exp-1") == "提交到目标路径");
  EXPECT(!has_kind(queue, "fail"));

  files.rename_error = pixcake::ResultCode::Ok;
  files.files["/out/a.jpg"] = kMismatched;
  const auto code = queue.recover(files);
  EXPECT(code == pixcake::ResultCode::Ok);
  EXPECT(queue.status("exp-1") == "失败");
  EXPECT(has_kind(queue, "fail"));
  EXPECT(files.files["/out/a.jpg"] == kMismatched);
}

void test_recover_returns_remove_error_when_renamed_destination_cannot_be_deleted() {
  MismatchFileSystem files;
  files.guarded = "/out/a.jpg";
  files.remove_error = pixcake::ResultCode::PermissionDenied;
  files.rename_error = pixcake::ResultCode::IoFailed;

  auto queue = pixcake::ExportQueue::open("/cache/exports");
  reach_render(queue, job_of("exp-1", "orig-1", "/out/a.jpg"));
  EXPECT(queue.commit_product("exp-1", kBytes, files) == pixcake::ResultCode::IoFailed);
  EXPECT(queue.status("exp-1") == "提交到目标路径");
  EXPECT(files.files.count("/cache/exports/exp-1") == 1);
  EXPECT(!has_kind(queue, "fail"));

  files.rename_error = pixcake::ResultCode::Ok;
  files.files["/cache/exports/exp-1"] = kMismatched;
  const auto before = queue.events().size();
  const auto code = queue.recover(files);
  EXPECT(code == pixcake::ResultCode::PermissionDenied);
  EXPECT(queue.status("exp-1") == "提交到目标路径");
  EXPECT(queue.events().size() == before);
  EXPECT(!has_kind(queue, "fail"));
  const auto destination = files.files.find("/out/a.jpg");
  EXPECT(destination != files.files.end());
  EXPECT(destination->second == kMismatched);
}

void test_reopen_repairs_cache_and_finishes_rename() {
  pixcake::test::TempDir dir;
  const std::string path = dir.path() + "/export.db";
  pixcake::test::FakeFileSystem files;
  {
    std::unique_ptr<pixcake::SqliteExportLog> log;
    EXPECT(pixcake::SqliteExportLog::open(path, log) == pixcake::ResultCode::Ok);
    std::unique_ptr<pixcake::ExportBook> book;
    EXPECT(pixcake::ExportBook::open("/cache/exports", *log, files, book) == pixcake::ResultCode::Ok);
    EXPECT(book->queue().set_fast_export(true) == pixcake::ResultCode::Ok);
    auto job = job_of("exp-1", "orig-1", "/out/a.jpg");
    reach_render(book->queue(), job);
    files.rename_error = pixcake::ResultCode::IoFailed;
    EXPECT(book->queue().commit_product("exp-1", kBytes, files) == pixcake::ResultCode::IoFailed);
    EXPECT(book->queue().status("exp-1") == "提交到目标路径");
  }

  sqlite3* raw = nullptr;
  EXPECT(sqlite3_open(path.c_str(), &raw) == SQLITE_OK);
  EXPECT(sqlite3_exec(raw, "UPDATE export_cache SET fast_export = 0 WHERE id = 1;", nullptr, nullptr, nullptr) ==
         SQLITE_OK);
  EXPECT(sqlite3_exec(raw, "UPDATE export_task_cache SET status = '排队' WHERE export_id = 'exp-1';", nullptr,
                      nullptr, nullptr) == SQLITE_OK);
  sqlite3_close(raw);

  files.rename_error = pixcake::ResultCode::Ok;
  std::unique_ptr<pixcake::SqliteExportLog> reopened_log;
  EXPECT(pixcake::SqliteExportLog::open(path, reopened_log) == pixcake::ResultCode::Ok);
  std::unique_ptr<pixcake::ExportBook> reopened;
  EXPECT(pixcake::ExportBook::open("/cache/exports", *reopened_log, files, reopened) == pixcake::ResultCode::Ok);
  EXPECT(reopened->queue().enabled());
  EXPECT(reopened->queue().status("exp-1") == "成功");
  std::vector<std::uint8_t> written;
  EXPECT(files.read_file("/out/a.jpg", written).ok);
  EXPECT(written == kBytes);

  sqlite3_stmt* cache = nullptr;
  EXPECT(sqlite3_open(path.c_str(), &raw) == SQLITE_OK);
  EXPECT(sqlite3_prepare_v2(raw, "SELECT fast_export FROM export_cache WHERE id = 1;", -1, &cache, nullptr) ==
         SQLITE_OK);
  EXPECT(sqlite3_step(cache) == SQLITE_ROW);
  EXPECT(sqlite3_column_int(cache, 0) == 1);
  sqlite3_finalize(cache);
  sqlite3_stmt* task = nullptr;
  EXPECT(sqlite3_prepare_v2(raw, "SELECT status FROM export_task_cache WHERE export_id = ?;", -1, &task, nullptr) ==
         SQLITE_OK);
  EXPECT(sqlite3_bind_text(task, 1, "exp-1", -1, SQLITE_TRANSIENT) == SQLITE_OK);
  EXPECT(sqlite3_step(task) == SQLITE_ROW);
  EXPECT(std::string(reinterpret_cast<const char*>(sqlite3_column_text(task, 0))) == "成功");
  sqlite3_finalize(task);
  sqlite3_close(raw);

  pixcake::test::TempDir future_dir;
  const std::string future_path = future_dir.path() + "/export.db";
  EXPECT(sqlite3_open(future_path.c_str(), &raw) == SQLITE_OK);
  EXPECT(sqlite3_exec(raw, "PRAGMA user_version=2;", nullptr, nullptr, nullptr) == SQLITE_OK);
  sqlite3_close(raw);
  std::unique_ptr<pixcake::SqliteExportLog> future;
  EXPECT(pixcake::SqliteExportLog::open(future_path, future) == pixcake::ResultCode::StateConflict);
  EXPECT(future == nullptr);
  EXPECT(sqlite3_open(future_path.c_str(), &raw) == SQLITE_OK);
  sqlite3_stmt* version = nullptr;
  EXPECT(sqlite3_prepare_v2(raw, "PRAGMA user_version;", -1, &version, nullptr) == SQLITE_OK);
  EXPECT(sqlite3_step(version) == SQLITE_ROW);
  EXPECT(sqlite3_column_int(version, 0) == 2);
  sqlite3_finalize(version);
  sqlite3_stmt* tables = nullptr;
  EXPECT(sqlite3_prepare_v2(raw, "SELECT count(*) FROM sqlite_master WHERE type = 'table' AND name = 'export_event';",
                            -1, &tables, nullptr) == SQLITE_OK);
  EXPECT(sqlite3_step(tables) == SQLITE_ROW);
  EXPECT(sqlite3_column_int(tables, 0) == 0);
  sqlite3_finalize(tables);
  sqlite3_close(raw);
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

int main() {
  test_snapshot_and_fast_export_stay();
  test_commit_once_and_reject_a_different_destination();
  test_disk_full_then_retry_uses_the_snapshot();
  test_rename_failure_and_crash_points();
  test_cancel_removes_temp_and_print_is_unavailable();
  test_product_facts_match_one_settlement();
  test_commit_returns_remove_error_when_mismatched_destination_cannot_be_deleted();
  test_commit_returns_sync_error_after_unlinking_mismatched_destination();
  test_recover_returns_sync_error_after_unlinking_mismatched_destination();
  test_rejected_fail_is_recorded_when_recover_finds_both_files_gone();
  test_recover_returns_remove_error_when_renamed_destination_cannot_be_deleted();
  test_reopen_repairs_cache_and_finishes_rename();
  test_duplicate_task_cache_key_is_state_conflict_and_one_row_round_trips();
  if (g_failures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  return 0;
}
