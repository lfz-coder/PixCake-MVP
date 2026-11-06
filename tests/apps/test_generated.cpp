#include "pixcake/generated/generation_service.hpp"
#include "pixcake/storage/sqlite_ledger_log.hpp"
#include "support/fake_file_system.hpp"
#include "support/temp_dir.hpp"

#include <cstdio>
#include <functional>
#include <memory>
#include <string>

#if defined(__APPLE__) || defined(__unix__)
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#define PIXCAKE_HAVE_FORK 1
#endif

namespace {

int g_failures = 0;

void expect(bool cond, const char* text, int line) {
  if (!cond) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, text);
    ++g_failures;
  }
}

#define EXPECT(cond) expect(static_cast<bool>(cond), #cond, __LINE__)

constexpr const char* kCreative = "/app/creative";

// 远端替身（测试清单 T-03）：按需返回结果、坏返回、超时，或在等待中做点什么。
class FakeTool final : public pixcake::generated::RemoteTool {
 public:
  pixcake::generated::RemoteReply reply;
  std::function<void()> during;
  int calls = 0;
  pixcake::generated::RemoteReply run(const pixcake::generate::GenerationTask&) override {
    ++calls;
    if (during) during();
    return reply;
  }
};

pixcake::generated::RemoteReply image_reply() {
  pixcake::generated::RemoteReply reply;
  reply.image = {1, 2, 3, 4, 5, 6, 7, 8};
  reply.width = 1;
  reply.height = 2;
  return reply;
}

pixcake::generate::GenerationTask task_of(const std::string& id) {
  pixcake::generate::GenerationTask task;
  task.task_id = id;
  task.image_id = "img-1";
  task.source_revision = 3;
  task.tool.tool_id = "creative.remove_passerby";
  task.tool.parameter_mode_id = "creative.remove_passerby";
  task.tool.billing_rule_id = "points";
  return task;
}

struct Book {
  pixcake::test::TempDir dir;
  std::unique_ptr<pixcake::SqliteLedgerLog> log;
  std::unique_ptr<pixcake::LedgerBook> book;

  Book() { open(); }
  void open() {
    book.reset();
    log.reset();
    EXPECT(pixcake::SqliteLedgerLog::open(dir.path() + "/ledger.db", log) == pixcake::ResultCode::Ok);
    EXPECT(pixcake::LedgerBook::open(pixcake::FreeQueuePolicy{}, *log, book) == pixcake::ResultCode::Ok);
  }
  void queued(const std::string& task_id, std::int64_t points) {
    EXPECT(book->credit_points(80) == pixcake::ResultCode::Ok);
    pixcake::TaskQuote quote;
    quote.task_id = task_id;
    quote.tool_id = "creative.remove_passerby";
    quote.origin = "官方";
    quote.quoted_points = points;
    EXPECT(book->quote(quote) == pixcake::ResultCode::Ok);
    EXPECT(book->enqueue(task_id) == pixcake::ResultCode::Ok);
  }
};

void test_c04_result_written_before_ready() {
  Book ledger;
  ledger.queued("task-1", 30);
  pixcake::test::FakeFileSystem files;
  FakeTool tool;
  tool.reply = image_reply();
  pixcake::generated::GenerationService service(*ledger.book, files, kCreative, tool);
  const auto progress = service.run(task_of("task-1"));
  EXPECT(progress.state == pixcake::generate::TaskState::Succeeded);
  EXPECT(progress.code == pixcake::ResultCode::Ok && progress.percent == 100);
  EXPECT(!progress.result_id.empty() && !progress.checksum.empty());
  EXPECT(progress.pixel_file.rfind(std::string(kCreative) + "/", 0) == 0);
  EXPECT(files.files.count(progress.pixel_file) == 1 && files.files[progress.pixel_file] == tool.reply.image);
  EXPECT(ledger.book->ledger().task_status("task-1") == "成功");
  EXPECT(ledger.book->ledger().creative_points() == 50);
}

void test_t03_b80_remote_bad_return_fails_without_file() {
  Book ledger;
  ledger.queued("task-bad", 30);
  pixcake::test::FakeFileSystem files;
  FakeTool tool;
  tool.reply.width = 1;
  tool.reply.height = 1;  // 没有像素
  pixcake::generated::GenerationService service(*ledger.book, files, kCreative, tool);
  const auto progress = service.run(task_of("task-bad"));
  EXPECT(progress.state == pixcake::generate::TaskState::Failed);
  EXPECT(progress.code == pixcake::ResultCode::ImageCorrupt);
  EXPECT(files.files.empty());
  EXPECT(ledger.book->ledger().task_status("task-bad") == "失败");
}

void test_t03_b50_remote_timeout_fails_with_network_failed() {
  Book ledger;
  ledger.queued("task-slow", 30);
  pixcake::test::FakeFileSystem files;
  FakeTool tool;
  tool.reply.code = pixcake::ResultCode::NetworkFailed;
  pixcake::generated::GenerationService service(*ledger.book, files, kCreative, tool);
  const auto progress = service.run(task_of("task-slow"));
  EXPECT(progress.state == pixcake::generate::TaskState::Failed);
  EXPECT(progress.code == pixcake::ResultCode::NetworkFailed);
  EXPECT(files.files.empty());
  EXPECT(ledger.book->ledger().task_status("task-slow") == "失败");
}

// T-03（测试清单第 67 行“尺寸不符”）：远端回答的像素字节数与宽高不符（RGBA8，每像素 4 字节）时判坏返回；
// 不写结果文件，已有的好结果不动。
void test_t03_b80_remote_size_mismatch_fails_and_keeps_previous_result() {
  Book ledger;
  ledger.queued("task-good", 10);
  ledger.queued("task-odd", 10);
  pixcake::test::FakeFileSystem files;
  FakeTool tool;
  tool.reply = image_reply();
  pixcake::generated::GenerationService service(*ledger.book, files, kCreative, tool);
  const auto good = service.run(task_of("task-good"));
  EXPECT(good.state == pixcake::generate::TaskState::Succeeded);
  const auto good_bytes = files.files[good.pixel_file];
  tool.reply.height = 3;  // 1×3 需要 12 字节，只给了 8
  const auto odd = service.run(task_of("task-odd"));
  EXPECT(odd.state == pixcake::generate::TaskState::Failed && odd.code == pixcake::ResultCode::ImageCorrupt);
  EXPECT(ledger.book->ledger().task_status("task-odd") == "失败");
  EXPECT(files.files.size() == 1 && files.files[good.pixel_file] == good_bytes);
}

// T-03：写结果时磁盘满。返回磁盘已满，任务失败，不留半个文件，已有的好结果不动。
void test_t03_disk_full_while_writing_result_keeps_previous_result() {
  Book ledger;
  ledger.queued("task-good", 10);
  ledger.queued("task-full", 10);
  pixcake::test::FakeFileSystem files;
  FakeTool tool;
  tool.reply = image_reply();
  pixcake::generated::GenerationService service(*ledger.book, files, kCreative, tool);
  const auto good = service.run(task_of("task-good"));
  const auto good_bytes = files.files[good.pixel_file];
  files.write_error = pixcake::ResultCode::DiskFull;
  const auto full = service.run(task_of("task-full"));
  EXPECT(full.state == pixcake::generate::TaskState::Failed && full.code == pixcake::ResultCode::DiskFull);
  EXPECT(ledger.book->ledger().task_status("task-full") == "失败");
  EXPECT(files.files.size() == 1 && files.files[good.pixel_file] == good_bytes);
}

// T-03：重复提交同一任务。第二次状态冲突，远端只调一次、只扣一次点、结果文件不被覆盖。
void test_t03_duplicate_submit_runs_once() {
  Book ledger;
  ledger.queued("task-twice", 30);
  pixcake::test::FakeFileSystem files;
  FakeTool tool;
  tool.reply = image_reply();
  pixcake::generated::GenerationService service(*ledger.book, files, kCreative, tool);
  const auto first = service.run(task_of("task-twice"));
  const auto points = ledger.book->ledger().creative_points();
  const auto bytes = files.files[first.pixel_file];
  tool.reply.image = {9, 9, 9, 9, 9, 9, 9, 9};
  const auto second = service.run(task_of("task-twice"));
  EXPECT(first.state == pixcake::generate::TaskState::Succeeded);
  EXPECT(second.state == pixcake::generate::TaskState::Failed && second.code == pixcake::ResultCode::StateConflict);
  EXPECT(tool.calls == 1);
  EXPECT(ledger.book->ledger().creative_points() == points);
  EXPECT(files.files[first.pixel_file] == bytes);
  EXPECT(ledger.book->ledger().task_status("task-twice") == "成功");
}

// 架构第 511 行：远程晚到的结果若任务已取消，不留像素文件、不追加就绪事件、不第二次记账；点不退。
void test_c30_b65_b81_cancel_while_running_discards_late_result() {
  Book ledger;
  ledger.queued("task-cancel", 30);
  pixcake::test::FakeFileSystem files;
  FakeTool tool;
  tool.reply = image_reply();
  tool.during = [&] { EXPECT(ledger.book->cancel("task-cancel") == pixcake::ResultCode::Ok); };
  pixcake::generated::GenerationService service(*ledger.book, files, kCreative, tool);
  const auto progress = service.run(task_of("task-cancel"));
  EXPECT(progress.state == pixcake::generate::TaskState::CancelledRunning);
  EXPECT(progress.code == pixcake::ResultCode::Cancelled);
  EXPECT(files.files.empty());
  EXPECT(ledger.book->ledger().task_status("task-cancel") == "生成中取消");
  EXPECT(ledger.book->ledger().creative_points() == 50);
}

void test_c30_task_not_queued_is_not_sent() {
  Book ledger;
  pixcake::test::FakeFileSystem files;
  FakeTool tool;
  tool.reply = image_reply();
  pixcake::generated::GenerationService service(*ledger.book, files, kCreative, tool);
  const auto progress = service.run(task_of("task-unknown"));
  EXPECT(progress.state == pixcake::generate::TaskState::Failed);
  EXPECT(progress.code == pixcake::ResultCode::StateConflict);
  EXPECT(tool.calls == 0);
}

// B-104、B-106：主进程按崩溃折叠时只动“生成中”的任务。排队中的任务不变，取消不扣点；
// 已成功的任务（结果文件已写、事件已确认）保持成功，结果文件还在。
void test_b104_b106_crash_folds_only_generating_tasks() {
  Book ledger;
  ledger.queued("task-done", 30);
  pixcake::test::FakeFileSystem files;
  FakeTool tool;
  tool.reply = image_reply();
  pixcake::generated::GenerationService service(*ledger.book, files, kCreative, tool);
  const auto done = service.run(task_of("task-done"));
  EXPECT(done.state == pixcake::generate::TaskState::Succeeded);
  ledger.queued("task-waiting", 20);
  const auto points = ledger.book->ledger().creative_points();
  std::vector<std::string> crashed;
  EXPECT(ledger.book->crash_generating(crashed) == pixcake::ResultCode::Ok);
  EXPECT(crashed.empty());
  EXPECT(ledger.book->ledger().task_status("task-done") == "成功");
  EXPECT(files.files.count(done.pixel_file) == 1);
  EXPECT(ledger.book->ledger().task_status("task-waiting") == "排队中");
  EXPECT(ledger.book->ledger().creative_points() == points);
}

#if defined(PIXCAKE_HAVE_FORK)
const char* g_self = nullptr;

// 子进程角色：在全新进程里打开同一份账本，开始生成，等远端时崩溃（退出码 9）。
// 不用 fork 后直接跑：macOS 上 fork 而不 exec 的子进程里再用系统库（SQLite 等）不安全，会随机段错误。
int crash_child(const std::string& directory) {
  std::unique_ptr<pixcake::SqliteLedgerLog> log;
  std::unique_ptr<pixcake::LedgerBook> book;
  if (pixcake::SqliteLedgerLog::open(directory + "/ledger.db", log) != pixcake::ResultCode::Ok ||
      pixcake::LedgerBook::open(pixcake::FreeQueuePolicy{}, *log, book) != pixcake::ResultCode::Ok) {
    return 2;
  }
  pixcake::test::FakeFileSystem files;
  FakeTool tool;
  tool.during = [] { _exit(9); };
  pixcake::generated::GenerationService service(*book, files, kCreative, tool);
  service.run(task_of("task-crash"));
  return 0;
}

// 架构第 307 行：生成进程崩溃视同生成中取消，不退点，任务失败。主进程看到进程退出后折叠。
void test_c30_t03_b105_generated_crash_fails_without_refund() {
  Book ledger;
  ledger.queued("task-crash", 30);
  ledger.book.reset();
  ledger.log.reset();
  const std::string directory = ledger.dir.path();
  char* const args[] = {const_cast<char*>(g_self), const_cast<char*>("--crash-child"),
                        const_cast<char*>(directory.c_str()), nullptr};
  pid_t child = 0;
  EXPECT(posix_spawn(&child, g_self, nullptr, nullptr, args, environ) == 0);
  int status = 0;
  EXPECT(waitpid(child, &status, 0) == child);
  EXPECT(WIFEXITED(status) && WEXITSTATUS(status) == 9);
  ledger.open();
  EXPECT(ledger.book->ledger().task_status("task-crash") == "生成中");
  std::vector<std::string> crashed;
  EXPECT(ledger.book->crash_generating(crashed) == pixcake::ResultCode::Ok);
  EXPECT(crashed == std::vector<std::string>({"task-crash"}));
  EXPECT(ledger.book->ledger().task_status("task-crash") == "失败");
  EXPECT(ledger.book->ledger().creative_points() == 50);
}
#endif

}  // namespace

int main(int argc, char** argv) {
#if defined(PIXCAKE_HAVE_FORK)
  g_self = argv[0];
  if (argc == 3 && std::string(argv[1]) == "--crash-child") return crash_child(argv[2]);
#else
  (void)argc;
  (void)argv;
#endif
  test_t03_b80_remote_size_mismatch_fails_and_keeps_previous_result();
  test_t03_disk_full_while_writing_result_keeps_previous_result();
  test_t03_duplicate_submit_runs_once();
  test_b104_b106_crash_folds_only_generating_tasks();
  test_c04_result_written_before_ready();
  test_t03_b80_remote_bad_return_fails_without_file();
  test_t03_b50_remote_timeout_fails_with_network_failed();
  test_c30_b65_b81_cancel_while_running_discards_late_result();
  test_c30_task_not_queued_is_not_sent();
#if defined(PIXCAKE_HAVE_FORK)
  test_c30_t03_b105_generated_crash_fails_without_refund();
#endif
  if (g_failures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  return 0;
}
