#include "cloud_client.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <queue>
#include <thread>

namespace {
using pixcake::ResultCode;
using pixcake::shell::CloudClient;
using pixcake::ui::shell::CloudState;
using namespace std::chrono_literals;
int failures = 0;
void expect(bool condition, int line) { if (!condition) { std::fprintf(stderr, "FAIL line %d\n", line); ++failures; } }
#define EXPECT(value) expect(static_cast<bool>(value), __LINE__)
const std::string account_id = "acc_" + std::string(32, 'a');
void login(pixcake::account::AccountSession& account) {
  pixcake::account::AccountSnapshot snapshot; snapshot.account_id = account_id; snapshot.session_handle = std::string(43, 'T');
  EXPECT(account.sign_in(snapshot) == ResultCode::Ok);
}
CloudClient::Reply ok() {
  CloudClient::Reply reply;
  reply.account = {account_id, std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::system_clock::now().time_since_epoch()).count() + 3600};
  reply.spaces = {{"space", "Team", pixcake::cloud::SpaceKind::Team, {pixcake::cloud::Permission::Read}}};
  reply.selected_space = "space";
  reply.projects = {{"project", "space", "Project", 1, 1, 1}};
  return reply;
}
bool await(CloudClient& client) {
  QElapsedTimer time; time.start();
  while (client.view().busy && time.elapsed() < 2000) { QCoreApplication::processEvents(); QThread::msleep(2); }
  return !client.view().busy;
}
struct Queue {
  std::mutex mutex;
  std::queue<std::shared_future<CloudClient::Reply>> replies;
  std::atomic<int> calls{0};
  std::promise<std::thread::id> worker;
  std::shared_ptr<std::promise<CloudClient::Reply>> add() {
    auto promise = std::make_shared<std::promise<CloudClient::Reply>>();
    std::lock_guard<std::mutex> lock(mutex); replies.push(promise->get_future().share()); return promise;
  }
  CloudClient::Fetch fetch() {
    return [this](const auto&, const auto&, auto) {
      if (++calls == 1) worker.set_value(std::this_thread::get_id());
      std::shared_future<CloudClient::Reply> next;
      { std::lock_guard<std::mutex> lock(mutex); next = replies.front(); replies.pop(); }
      return next.get();
    };
  }
};
struct SafeLog final : pixcake::LogSink {
  int records = 0;
  void write(const pixcake::LogRecord& record) override {
    ++records;
    EXPECT(record.module == "cloud" && record.event == "cloud.directory.query");
    EXPECT(record.message.empty() && record.path.empty() && record.document_id.empty() && record.task_id.empty());
  }
};
void test_background_retention_denial_and_generation() {
  pixcake::account::AccountSession account; login(account);
  Queue queue;
  SafeLog log;
  CloudClient client(account, {"https://cloud.invalid", "", 500ms}, queue.fetch(), &log);
  int changes = 0;
  const auto owner = std::this_thread::get_id();
  client.on_changed([&] { EXPECT(std::this_thread::get_id() == owner); ++changes; });
  auto first = queue.add();
  EXPECT(client.refresh("space") && client.view().state == CloudState::Loading);
  EXPECT(!client.refresh() && client.view().busy);
  EXPECT(queue.worker.get_future().get() != owner);
  first->set_value(ok()); EXPECT(await(client));
  EXPECT(client.view().projects.size() == 1 && client.view().state == CloudState::Ready);
  auto failed = queue.add(); EXPECT(client.refresh("another"));
  CloudClient::Reply failure; failure.code = ResultCode::NetworkFailed; failed->set_value(failure);
  EXPECT(await(client));
  EXPECT(client.view().state == CloudState::Failed && client.view().selected_space == "space");
  EXPECT(client.view().has_snapshot && client.view().projects.size() == 1);
  auto denied = queue.add(); EXPECT(client.refresh()); failure.code = ResultCode::PermissionDenied; denied->set_value(failure);
  EXPECT(await(client)); EXPECT(client.view().state == CloudState::Denied && client.view().spaces.empty());
  auto late = queue.add(); EXPECT(client.refresh());
  login(account); // Same account and token, new authentication generation.
  EXPECT(client.view().spaces.empty());
  late->set_value(ok()); EXPECT(await(client));
  EXPECT(client.view().state == CloudState::Idle && client.view().projects.empty());
  auto logout = queue.add(); EXPECT(client.refresh()); account.sign_out();
  EXPECT(client.view().state == CloudState::SignedOut && client.view().spaces.empty());
  logout->set_value(ok()); EXPECT(await(client)); EXPECT(!account.signed_in());
  EXPECT(changes > 0);
  EXPECT(log.records == 5);
}
void test_restoration_and_shutdown() {
  pixcake::account::AccountSession account;
  Queue queue;
  auto client = std::make_unique<CloudClient>(account, pixcake::shell::CloudOptions{"https://cloud.invalid", "", 500ms}, queue.fetch());
  auto first = queue.add(); EXPECT(client->restore(std::string(43, 'T')));
  EXPECT(!account.signed_in()); first->set_value(ok()); EXPECT(await(*client));
  EXPECT(account.signed_in() && account.snapshot().account_id == account_id);
  EXPECT(!client->restore(std::string(43, 'X')));
  auto last = queue.add(); EXPECT(client->refresh());
  std::thread release([last] { std::this_thread::sleep_for(20ms); last->set_value(ok()); });
  client.reset(); release.join(); QCoreApplication::processEvents();
  CloudClient disabled(account, {});
  EXPECT(disabled.view().state == CloudState::NotConfigured && !disabled.refresh());
}
void test_restore_cannot_revive_a_logged_out_generation() {
  pixcake::account::AccountSession account;
  Queue queue;
  CloudClient client(account, {"https://cloud.invalid", "", 500ms}, queue.fetch());
  auto late = queue.add(); EXPECT(client.restore(std::string(43, 'T')));
  EXPECT(account.sign_out() == ResultCode::StateConflict);
  late->set_value(ok()); EXPECT(await(client));
  EXPECT(!account.signed_in() && client.view().state == CloudState::SignedOut);
  auto expired = queue.add(); EXPECT(client.restore(std::string(43, 'T')));
  auto reply = ok(); reply.account.session_expires_at = 1; expired->set_value(reply); EXPECT(await(client));
  EXPECT(!account.signed_in() && client.view().state == CloudState::Denied && client.view().spaces.empty());
}
}  // namespace
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  test_background_retention_denial_and_generation(); test_restoration_and_shutdown();
  test_restore_cannot_revive_a_logged_out_generation();
  return failures ? 1 : 0;
}
