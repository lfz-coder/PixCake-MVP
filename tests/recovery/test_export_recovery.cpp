// 测试清单 T-08：导出任务的崩溃点、换名失败、重开恢复（从 tests/kernel/exportq/test_export_queue.cpp 迁来）。
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

class TempCleanupFailureFileSystem : public pixcake::test::FakeFileSystem {
 public:
  pixcake::ResultCode remove_error = pixcake::ResultCode::Ok;
  bool fail_temp_remove = true;

  pixcake::IoResult remove_file(const std::string& path) override {
    if (fail_temp_remove && path.find("/cache/") != std::string::npos &&
        remove_error != pixcake::ResultCode::Ok) {
      return pixcake::io_error(remove_error);
    }
    return pixcake::test::FakeFileSystem::remove_file(path);
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


























}  // namespace

void test_b95_b96_b97_rename_failure_and_crash_points() {
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

void test_b113_reopen_repairs_cache_and_finishes_rename() {
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
  pixcake::ExportJob reopened_job;
  EXPECT(reopened->queue().find_job("exp-1", reopened_job));
  EXPECT(reopened_job.document_snapshot == std::vector<std::uint8_t>({7, 7, 7}));
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
  EXPECT(code == pixcake::ResultCode::StateConflict);
  EXPECT(queue.status("exp-1") == "提交到目标路径");
  EXPECT(queue.events().size() == before);
  EXPECT(!has_kind(queue, "fail"));
  EXPECT(files.files.count("/out/a.jpg") == 0);
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
  EXPECT(files.files["/out/a.jpg"] == kMismatched);
  EXPECT(files.files.count("/cache/exports/exp-1") == 0);

  queue.set_sink(nullptr);
  EXPECT(queue.recover(files) == pixcake::ResultCode::Ok);
  EXPECT(queue.status("exp-1") == "失败");
  EXPECT(has_kind(queue, "fail"));
}

void test_b98_recovery_rejects_unowned_identical_temp_file() {
  pixcake::test::TempDir dir;
  pixcake::PosixFileSystem files;
  const std::string cache = dir.path() + "/cache";
  const std::string destination = dir.path() + "/owned.jpg";
  EXPECT(files.create_directory(cache).ok);
  auto queue = pixcake::ExportQueue::open(cache);
  auto job = job_of("owned-temp", "orig", destination);
  reach_render(queue, job);
  // Reach the durable temp-end state through the normal writer first, then
  // replace the cache entry with identical bytes.  The identity must still
  // prevent recovery from claiming it.
  EXPECT(queue.commit_product("owned-temp", kBytes, files) == pixcake::ResultCode::Ok);
  auto events = queue.events();
  for (auto& event : events) {
    if (event.kind == "commit-end") event.kind = "commit-begin";
  }
  auto recovered = pixcake::ExportQueue::open(cache, std::move(events));
  EXPECT(files.remove_file(destination).ok);
  EXPECT(files.write_new(cache + "/owned-temp", kBytes).ok);
  EXPECT(recovered.recover(files) == pixcake::ResultCode::StateConflict);
  EXPECT(!files.exists(destination));
}

void test_recovery_rejects_persisted_path_escape_without_touching_user_file() {
  pixcake::ExportEvent enqueue;
  enqueue.kind = "enqueue";
  enqueue.export_id = "../owned_by_user";
  enqueue.original_id = "orig-escape";
  enqueue.document_revision = 1;
  enqueue.document_snapshot = {1};
  enqueue.destination_path = "/out/escape.jpg";
  enqueue.destination_root = "/out";
  enqueue.settings_snapshot = "quality=8";
  pixcake::ExportEvent prepare = enqueue;
  prepare.kind = "prepare";
  pixcake::ExportEvent render = enqueue;
  render.kind = "render";
  pixcake::ExportEvent temp = enqueue;
  temp.kind = "temp-begin";

  pixcake::test::FakeFileSystem files;
  files.files["/cache/owned_by_user"] = {8, 8};
  auto queue = pixcake::ExportQueue::open("/cache/exports", {enqueue, prepare, render, temp});
  EXPECT(queue.recover(files) == pixcake::ResultCode::StateConflict);
  EXPECT(files.files["/cache/owned_by_user"] == std::vector<std::uint8_t>({8, 8}));
  EXPECT(queue.status("../owned_by_user") == "写临时文件");
}

void test_p11_failed_temp_cleanup_remains_retryable_after_reopen() {
  TempCleanupFailureFileSystem files;
  files.sync_file_error = pixcake::ResultCode::IoFailed;
  files.remove_error = pixcake::ResultCode::PermissionDenied;
  auto queue = pixcake::ExportQueue::open("/cache/exports");
  reach_render(queue, job_of("cleanup-pending", "original", "/out/cleanup.jpg"));
  EXPECT(queue.commit_product("cleanup-pending", kBytes, files) == pixcake::ResultCode::IoFailed);
  EXPECT(queue.status("cleanup-pending") == "待清理临时文件");
  EXPECT(queue.failure_reason("cleanup-pending") == pixcake::ResultCode::PermissionDenied);
  EXPECT(queue.recover(files) == pixcake::ResultCode::PermissionDenied);
  files.fail_temp_remove = false;
  EXPECT(queue.recover(files) == pixcake::ResultCode::Ok);
  EXPECT(queue.status("cleanup-pending") == "失败");
}

void test_failure_reason_survives_reopen_and_other_jobs_continue() {
  pixcake::test::TempDir dir;
  const std::string path = dir.path() + "/export.db";
  pixcake::test::FakeFileSystem files;
  std::unique_ptr<pixcake::SqliteExportLog> log;
  EXPECT(pixcake::SqliteExportLog::open(path, log) == pixcake::ResultCode::Ok);
  std::unique_ptr<pixcake::ExportBook> book;
  EXPECT(pixcake::ExportBook::open("/cache/exports", *log, files, book) == pixcake::ResultCode::Ok);
  auto bad = job_of("exp-bad", "orig-bad", "/out/bad.jpg");
  auto good = job_of("exp-good", "orig-good", "/out/good.jpg");
  reach_render(book->queue(), bad);
  reach_render(book->queue(), good);
  files.write_error = pixcake::ResultCode::PermissionDenied;
  EXPECT(book->queue().commit_product("exp-bad", kBytes, files) == pixcake::ResultCode::PermissionDenied);
  EXPECT(book->queue().failure_reason("exp-bad") == pixcake::ResultCode::PermissionDenied);
  files.write_error = pixcake::ResultCode::Ok;
  EXPECT(book->queue().commit_product("exp-good", kBytes, files) == pixcake::ResultCode::Ok);
  book.reset();
  log.reset();
  std::unique_ptr<pixcake::SqliteExportLog> reopened_log;
  EXPECT(pixcake::SqliteExportLog::open(path, reopened_log) == pixcake::ResultCode::Ok);
  std::unique_ptr<pixcake::ExportBook> reopened;
  EXPECT(pixcake::ExportBook::open("/cache/exports", *reopened_log, files, reopened) == pixcake::ResultCode::Ok);
  EXPECT(reopened->queue().failure_reason("exp-bad") == pixcake::ResultCode::PermissionDenied);
  EXPECT(reopened->queue().status("exp-good") == "成功");
}

int main() {
  test_b95_b96_b97_rename_failure_and_crash_points();
  test_b113_reopen_repairs_cache_and_finishes_rename();
  test_recover_returns_sync_error_after_unlinking_mismatched_destination();
  test_recover_returns_remove_error_when_renamed_destination_cannot_be_deleted();
  test_rejected_fail_is_recorded_when_recover_finds_both_files_gone();
  test_b98_recovery_rejects_unowned_identical_temp_file();
  test_recovery_rejects_persisted_path_escape_without_touching_user_file();
  test_p11_failed_temp_cleanup_remains_retryable_after_reopen();
  test_failure_reason_survives_reopen_and_other_jobs_continue();
  return 0;
}
