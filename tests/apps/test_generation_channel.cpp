#include "pixcake/generated/generation_channel.hpp"
#include "pixcake/generation_session/generation_flow.hpp"
#include "pixcake/storage/sqlite_ledger_log.hpp"
#include "support/fake_file_system.hpp"
#include "support/temp_dir.hpp"

#include <sys/socket.h>

#include <cstdio>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

namespace {

int g_failures = 0;

void expect(bool condition, const char* expression, int line) {
  if (!condition) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, expression);
    ++g_failures;
  }
}

#define EXPECT(expression) expect(static_cast<bool>(expression), #expression, __LINE__)

pixcake::generate::GenerationTask task(const std::string& id) {
  pixcake::generate::GenerationTask value;
  value.task_id = id;
  value.image_id = "image-1";
  value.source_revision = 3;
  value.tool.tool_id = "creative.fixture";
  value.tool.parameter_mode_id = "fixture-default";
  value.tool.billing_rule_id = "fixture-points";
  value.tool.output_count = 1;
  value.tool.cancellable = true;
  return value;
}

class FakeTool final : public pixcake::generated::RemoteTool {
 public:
  pixcake::generated::RemoteReply reply;
  int delay_ms = 0;
  std::atomic<bool> started{false};

  pixcake::generated::RemoteReply run(const pixcake::generate::GenerationTask&) override {
    started.store(true, std::memory_order_release);
    if (delay_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    return reply;
  }
};

struct Book {
  pixcake::test::TempDir directory;
  std::unique_ptr<pixcake::SqliteLedgerLog> log;
  std::unique_ptr<pixcake::LedgerBook> book;

  Book() {
    EXPECT(pixcake::SqliteLedgerLog::open(directory.path() + "/ledger.db", log) == pixcake::ResultCode::Ok);
    EXPECT(pixcake::LedgerBook::open(pixcake::FreeQueuePolicy{}, *log, book) == pixcake::ResultCode::Ok);
  }

  void queue(const std::string& task_id) {
    EXPECT(book->credit_points(20) == pixcake::ResultCode::Ok);
    pixcake::TaskQuote quote;
    quote.task_id = task_id;
    quote.tool_id = "creative.fixture";
    quote.origin = "官方";
    quote.quoted_points = 4;
    EXPECT(book->quote(quote) == pixcake::ResultCode::Ok);
    EXPECT(book->enqueue(task_id) == pixcake::ResultCode::Ok);
  }
};

void test_submit_round_trip_over_process_channel() {
  Book ledger;
  ledger.queue("channel-task");
  pixcake::test::FakeFileSystem files;
  FakeTool tool;
  tool.reply.results.push_back({{1, 2, 3, 4}, 1, 1, {5}});
  pixcake::generated::GenerationService service(*ledger.book, files, "/fixture/creative", tool);

  int fds[2] = {-1, -1};
  EXPECT(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
  pixcake::preview_channel::Channel server_channel{pixcake::preview_channel::UniqueFd{fds[0]}};
  pixcake::generated::GenerationClient client{
      pixcake::preview_channel::Channel{pixcake::preview_channel::UniqueFd{fds[1]}}};
  pixcake::generated::GenerationServer server(service);
  pixcake::ResultCode server_code = pixcake::ResultCode::IoFailed;
  pixcake::ResultCode confirmation_code = pixcake::ResultCode::IoFailed;
  std::string server_diagnostic;
  std::thread worker([&] {
    server_code = server.serve_once(server_channel, 1000, server_diagnostic);
    confirmation_code = server.serve_once(server_channel, 1000, server_diagnostic);
  });

  pixcake::ipc::GenerationProgressMessage progress;
  auto document = pixcake::LayerDocument::create("image-1", 3);
  EXPECT(document.has_value());
  pixcake::generation_session::GenerationFlow flow(client, files, *document, "/layers/channel.pxlayer");
  std::string diagnostic;
  const auto client_code = flow.run(task("channel-task"), progress, 1000, diagnostic);
  worker.join();

  EXPECT(client_code == pixcake::ResultCode::Ok);
  EXPECT(server_code == pixcake::ResultCode::Ok);
  EXPECT(confirmation_code == pixcake::ResultCode::Ok);
  EXPECT(progress.state == pixcake::generate::TaskState::Succeeded && progress.results.size() == 1);
  EXPECT(progress.results.size() == 1 &&
         progress.results.front().continuation_parameters == std::vector<std::uint8_t>({5}));
  EXPECT(files.files.count(progress.pixel_file) == 1);
  EXPECT(files.files.count("/layers/channel.pxlayer") == 1 && document->generations().size() == 1);
  EXPECT(ledger.book->ledger().task_status("channel-task") == "成功");
}

void test_cancel_message_reaches_generation_service() {
  Book ledger;
  ledger.queue("cancel-task");
  pixcake::test::FakeFileSystem files;
  FakeTool tool;
  pixcake::generated::GenerationService service(*ledger.book, files, "/fixture/creative", tool);

  int fds[2] = {-1, -1};
  EXPECT(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
  pixcake::preview_channel::Channel server_channel{pixcake::preview_channel::UniqueFd{fds[0]}};
  pixcake::generated::GenerationClient client{
      pixcake::preview_channel::Channel{pixcake::preview_channel::UniqueFd{fds[1]}}};
  pixcake::generated::GenerationServer server(service);
  pixcake::ResultCode server_code = pixcake::ResultCode::IoFailed;
  std::string server_diagnostic;
  std::thread worker([&] {
    server_code = server.serve_once(server_channel, 1000, server_diagnostic);
  });
  std::string diagnostic;
  EXPECT(client.cancel("cancel-task", diagnostic) == pixcake::ResultCode::Ok);
  worker.join();
  EXPECT(server_code == pixcake::ResultCode::Ok);
  EXPECT(ledger.book->ledger().task_status("cancel-task") == "排队取消");
}

void test_async_cancel_drops_a_late_provider_result() {
  Book ledger;
  ledger.queue("async-cancel-task");
  pixcake::test::FakeFileSystem files;
  FakeTool tool;
  tool.delay_ms = 150;
  tool.reply.results.push_back({{1, 2, 3, 4}, 1, 1, {}});
  pixcake::generated::GenerationService service(*ledger.book, files, "/fixture/creative", tool);

  int fds[2] = {-1, -1};
  EXPECT(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
  pixcake::preview_channel::Channel server_channel{pixcake::preview_channel::UniqueFd{fds[0]}};
  pixcake::generated::GenerationClient client{
      pixcake::preview_channel::Channel{pixcake::preview_channel::UniqueFd{fds[1]}}};
  pixcake::generated::GenerationServer server(service);
  pixcake::ResultCode server_code = pixcake::ResultCode::Ok;
  std::string server_diagnostic;
  std::thread server_thread([&] { server_code = server.serve(server_channel, -1, server_diagnostic); });

  pixcake::ipc::GenerationProgressMessage progress;
  pixcake::ResultCode submit_code = pixcake::ResultCode::IoFailed;
  std::string submit_diagnostic;
  std::atomic<bool> cancel_requested{false};
  std::thread cancel_thread([&] {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!tool.started.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    cancel_requested.store(true, std::memory_order_release);
  });
  submit_code = client.submit(task("async-cancel-task"), progress, 5000, submit_diagnostic, &cancel_requested);
  cancel_thread.join();

  EXPECT(submit_code == pixcake::ResultCode::Ok);
  EXPECT(progress.state == pixcake::generate::TaskState::CancelledRunning);
  EXPECT(progress.code == pixcake::ResultCode::Cancelled);
  EXPECT(ledger.book->ledger().task_status("async-cancel-task") == "生成中取消");
  EXPECT(ledger.book->ledger().creative_points() == 16);
  EXPECT(files.files.empty());

  client = pixcake::generated::GenerationClient{};
  server_thread.join();
  EXPECT(server_code == pixcake::ResultCode::IoFailed);
}

void test_pre_cancel_does_not_send_a_task() {
  int fds[2] = {-1, -1};
  EXPECT(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
  pixcake::preview_channel::Channel peer{pixcake::preview_channel::UniqueFd{fds[0]}};
  pixcake::generated::GenerationClient client{
      pixcake::preview_channel::Channel{pixcake::preview_channel::UniqueFd{fds[1]}}};
  std::atomic<bool> cancel_requested{true};
  pixcake::ipc::GenerationProgressMessage progress;
  std::string diagnostic;
  EXPECT(client.submit(task("pre-cancelled"), progress, 1000, diagnostic, &cancel_requested) ==
         pixcake::ResultCode::Cancelled);
  EXPECT(!peer.readable(0));
}

}  // namespace

int main() {
  test_submit_round_trip_over_process_channel();
  test_cancel_message_reaches_generation_service();
  test_async_cancel_drops_a_late_provider_result();
  test_pre_cancel_does_not_send_a_task();
  return g_failures == 0 ? 0 : 1;
}
