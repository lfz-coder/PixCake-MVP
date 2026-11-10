#include "cloud_transfer_client.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <cassert>
#include <filesystem>
#include <thread>
#include <unistd.h>

using namespace pixcake;
using namespace pixcake::shell;
namespace {
bool until(const std::function<bool()>& predicate) {
  QElapsedTimer timer; timer.start();
  while (!predicate() && timer.elapsed() < 5000) { QCoreApplication::processEvents(); QThread::msleep(5); }
  return predicate();
}
struct Blocked final : net::HttpTransport {
  std::atomic_bool& entered;
  std::atomic_bool& release;
  std::thread::id owner;
  Blocked(std::atomic_bool& a, std::atomic_bool& b) : entered(a), release(b), owner(std::this_thread::get_id()) {}
  net::HttpResponse send(const net::HttpRequest&, std::chrono::milliseconds) override {
    assert(std::this_thread::get_id() == owner);
    entered = true;
    while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds{5});
    net::HttpResponse response; response.transport_code = ResultCode::NetworkFailed; return response;
  }
};
account::AccountSnapshot identity(char suffix) {
  account::AccountSnapshot snapshot; snapshot.account_id = "acc_" + std::string(32, suffix);
  snapshot.session_handle = std::string(43, suffix); return snapshot;
}
}
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  char pattern[] = "/tmp/pixcake-sync-owner-XXXXXX"; assert(::mkdtemp(pattern));
  const std::filesystem::path root(pattern);
  std::filesystem::create_directory(root / "a"); std::filesystem::create_directory(root / "b");
  std::atomic_bool entered{false}, release{false};
  account::AccountSession account;
  assert(account.sign_in(identity('a')) == ResultCode::Ok);
  const auto owner = std::this_thread::get_id();
  CloudTransferClient client(account, {"https://sync.invalid", ""}, nullptr, [&] {
    return std::make_unique<Blocked>(entered, release);
  });
  client.on_changed([&] { assert(std::this_thread::get_id() == owner); });
  client.set_directory((root / "a").string());
  assert(client.view().configured && !client.view().pending);
  assert(client.download("prj_" + std::string(32, 'a'), "sp_" + std::string(32, 'a'), 1));
  assert(until([&] { return entered.load(); }));
  assert(client.view().busy);
  // A late result from the old account cannot expose its pending task in B.
  client.pause(); assert(client.view().busy && client.view().phase == cloud_sync::Phase::Paused);
  account.sign_out(); assert(account.sign_in(identity('b')) == ResultCode::Ok);
  client.set_directory((root / "b").string()); release = true;
  assert(until([&] { return !client.view().busy; }));
  assert(client.view().configured && !client.view().pending);
  assert(client.view().phase == cloud_sync::Phase::Idle);
  // Project enumeration also runs on the worker. An invalid local selection
  // reports a failure without creating a pending upload or touching account A.
  assert(client.upload(999, "sp_" + std::string(32, 'b')));
  assert(until([&] { return !client.view().busy; }));
  assert(client.view().phase == cloud_sync::Phase::Failed && !client.view().pending);
  client.set_directory((root / "a").string());
  assert(client.view().pending && client.view().phase == cloud_sync::Phase::Paused);
  client.pause(); account.sign_out();
  assert(!client.view().configured && !client.view().pending);
  std::filesystem::remove_all(root);
}
