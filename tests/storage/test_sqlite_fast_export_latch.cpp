#include "pixcake/command/command_gate.hpp"
#include "pixcake/exportq/export_book.hpp"
#include "pixcake/storage/sqlite_export_log.hpp"
#include "pixcake/storage/sqlite_fast_export_latch.hpp"
#include "support/fake_file_system.hpp"
#include "support/temp_dir.hpp"

#include <sqlite3.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

extern "C" char** environ;

namespace {

int g_failures = 0;

void expect(bool cond, const char* text, int line) {
  if (!cond) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, text);
    ++g_failures;
  }
}

#define EXPECT(cond) expect(static_cast<bool>(cond), #cond, __LINE__)

const char* g_self = nullptr;

// 另一个进程里要做的事。用 posix_spawn 重新启动测试程序本身（不是 fork 出的副本）：
// SQLite 的锁状态是进程内全局的，fork 而不 exec 的子进程会继承父进程的记录，不代表真实场景。
int run_helper(const std::string& db, const std::string& action) {
  std::unique_ptr<pixcake::SqliteExportLog> log;
  pixcake::test::FakeFileSystem files;
  std::unique_ptr<pixcake::ExportBook> book;
  if (pixcake::SqliteExportLog::open(db, log) != pixcake::ResultCode::Ok ||
      pixcake::ExportBook::open("/cache/exports", *log, files, book) != pixcake::ResultCode::Ok) {
    return 2;
  }
  pixcake::ExportQueue& queue = book->queue();
  if (action == "on") return queue.set_fast_export(true) == pixcake::ResultCode::Ok ? 0 : 1;
  if (action == "off") return queue.set_fast_export(false) == pixcake::ResultCode::Ok ? 0 : 1;
  if (action == "on-and-drain") {
    pixcake::ExportJob job;
    job.export_id = "e1";
    job.original_id = "orig-e1";
    job.document_revision = 3;
    job.document_snapshot = {3, 3, 3};
    job.destination_path = "/out/e1.jpg";
    job.settings_snapshot = "quality=8";
    const bool ok = queue.set_fast_export(true) == pixcake::ResultCode::Ok &&
                    queue.enqueue(job) == pixcake::ResultCode::Ok && queue.cancel("e1") == pixcake::ResultCode::Ok &&
                    queue.status("e1") == "取消";
    return ok ? 0 : 1;
  }
  return 3;
}

bool in_other_process(const std::string& db, const char* action) {
  std::string db_arg = db;
  char helper[] = "--helper";
  std::string action_arg = action;
  char* argv[] = {const_cast<char*>(g_self), helper, db_arg.data(), action_arg.data(), nullptr};
  pid_t child = 0;
  if (posix_spawn(&child, g_self, nullptr, nullptr, argv, environ) != 0) return false;
  int status = 0;
  return waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

std::string fresh_database(const pixcake::test::TempDir& dir) {
  const std::string db = dir.path() + "/export.db";
  std::unique_ptr<pixcake::SqliteExportLog> log;
  EXPECT(pixcake::SqliteExportLog::open(db, log) == pixcake::ResultCode::Ok);
  return db;
}

pixcake::ResultCode submit(pixcake::EditDocument& document, const pixcake::FastExportLatch& latch,
                           pixcake::test::FakeFileSystem& files, const char* name, const char* value) {
  pixcake::CommandGate gate(document, latch, files, "/docs/img-1.pxedit", {{"color.global", 1}}, nullptr);
  pixcake::UserCommand command;
  command.name = name;
  command.target_id = "img-1";
  command.revision = document.revision();
  command.capability_id = "color.global";
  command.parameter_id = "exposure";
  command.display_value = value;
  return gate.submit(command).code;
}


void test_b62_other_process_sees_fast_export() {
  pixcake::test::TempDir dir;
  const std::string db = fresh_database(dir);
  std::unique_ptr<pixcake::SqliteFastExportLatch> latch;
  EXPECT(pixcake::SqliteFastExportLatch::open(db, latch) == pixcake::ResultCode::Ok);
  if (!latch) return;
  EXPECT(!latch->enabled());
  EXPECT(in_other_process(db, "on"));
  bool on = false;
  EXPECT(latch->read(on) == pixcake::ResultCode::Ok && on);
  EXPECT(latch->enabled());
  pixcake::test::FakeFileSystem files;
  auto document = *pixcake::EditDocument::create("img-1");
  EXPECT(submit(document, *latch, files, "preview", "1.00") == pixcake::ResultCode::FastExportBusy);
  EXPECT(submit(document, *latch, files, "commit", "1.00") == pixcake::ResultCode::FastExportBusy);
  EXPECT(document.revision() == 0);
  EXPECT(files.writes.empty());
}

void test_b62_empty_queue_keeps_fast_export() {
  pixcake::test::TempDir dir;
  const std::string db = fresh_database(dir);
  std::unique_ptr<pixcake::SqliteFastExportLatch> latch;
  EXPECT(pixcake::SqliteFastExportLatch::open(db, latch) == pixcake::ResultCode::Ok);
  if (!latch) return;
  EXPECT(in_other_process(db, "on-and-drain"));
  EXPECT(latch->enabled());
  pixcake::test::FakeFileSystem files;
  auto document = *pixcake::EditDocument::create("img-1");
  EXPECT(submit(document, *latch, files, "preview", "1.00") == pixcake::ResultCode::FastExportBusy);
  EXPECT(in_other_process(db, "off"));
  EXPECT(!latch->enabled());
  EXPECT(submit(document, *latch, files, "preview", "1.00") == pixcake::ResultCode::Ok);
  EXPECT(submit(document, *latch, files, "commit", "1.00") == pixcake::ResultCode::Ok);
  EXPECT(document.revision() == 1);
}

void test_b62_unreadable_log_fails_closed() {
  pixcake::test::TempDir dir;
  const std::string db = fresh_database(dir);
  std::unique_ptr<pixcake::SqliteFastExportLatch> latch;
  EXPECT(pixcake::SqliteFastExportLatch::open(db, latch) == pixcake::ResultCode::Ok);
  if (!latch) return;
  sqlite3* raw = nullptr;
  EXPECT(sqlite3_open(db.c_str(), &raw) == SQLITE_OK);
  EXPECT(sqlite3_exec(raw, "DROP TABLE export_event;", nullptr, nullptr, nullptr) == SQLITE_OK);
  sqlite3_close(raw);
  bool on = false;
  EXPECT(latch->read(on) != pixcake::ResultCode::Ok);
  EXPECT(latch->enabled());
}

void test_b62_missing_database_is_reported() {
  pixcake::test::TempDir dir;
  std::unique_ptr<pixcake::SqliteFastExportLatch> latch;
  EXPECT(pixcake::SqliteFastExportLatch::open(dir.path() + "/none.db", latch) == pixcake::ResultCode::IoFailed);
  EXPECT(latch == nullptr);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 4 && std::string(argv[1]) == "--helper") return run_helper(argv[2], argv[3]);
  g_self = argv[0];
  test_b62_other_process_sees_fast_export();
  test_b62_empty_queue_keeps_fast_export();
  test_b62_unreadable_log_fails_closed();
  test_b62_missing_database_is_reported();
  return g_failures == 0 ? 0 : 1;
}
