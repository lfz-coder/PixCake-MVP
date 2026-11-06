#include "pixcake/generated/generation_service.hpp"
#include "pixcake/storage/sqlite_ledger_log.hpp"
#include "support/fake_file_system.hpp"
#include "support/temp_dir.hpp"

#include <cstdio>
#include <functional>
#include <memory>
#include <string>

#if defined(__APPLE__) || defined(__unix__)
#include <sys/wait.h>
#include <unistd.h>
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

void test_t03_remote_bad_return_fails_without_file() {
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

void test_t03_remote_timeout_fails_with_network_failed() {
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

// 架构第 511 行：远程晚到的结果若任务已取消，不留像素文件、不追加就绪事件、不第二次记账；点不退。
void test_c30_cancel_while_running_discards_late_result() {
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

#if defined(PIXCAKE_HAVE_FORK)
// 架构第 307 行：生成进程崩溃视同生成中取消，不退点，任务失败。主进程看到进程退出后折叠。
void test_c30_t03_generated_crash_fails_without_refund() {
  Book ledger;
  ledger.queued("task-crash", 30);
  ledger.book.reset();
  ledger.log.reset();
  const pid_t child = fork();
  if (child == 0) {
    Book* reopened = &ledger;
    reopened->open();
    pixcake::test::FakeFileSystem files;
    FakeTool tool;
    tool.during = [] { _exit(9); };
    pixcake::generated::GenerationService service(*reopened->book, files, kCreative, tool);
    service.run(task_of("task-crash"));
    _exit(0);
  }
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

int main() {
  test_c04_result_written_before_ready();
  test_t03_remote_bad_return_fails_without_file();
  test_t03_remote_timeout_fails_with_network_failed();
  test_c30_cancel_while_running_discards_late_result();
  test_c30_task_not_queued_is_not_sent();
#if defined(PIXCAKE_HAVE_FORK)
  test_c30_t03_generated_crash_fails_without_refund();
#endif
  if (g_failures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  return 0;
}
