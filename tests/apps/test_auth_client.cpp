#include "auth_client.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <atomic>
#include <cstdio>
#include <thread>

namespace {
using pixcake::ResultCode;
using pixcake::account::AuthOperation;
using pixcake::account::AuthError;
using pixcake::shell::AuthClient;
int failures = 0;
#define EXPECT(value) do { if (!(value)) { std::fprintf(stderr, "FAIL auth client line %d\n", __LINE__); ++failures; } } while (false)
struct MemoryStore final : pixcake::account::CredentialStore {
  std::string token;
  int writes = 0;
  ResultCode read(const std::string&, std::string& out) override { out = token; return ResultCode::Ok; }
  ResultCode write(const std::string&, const std::string& value) override { token = value; ++writes; return ResultCode::Ok; }
  ResultCode erase(const std::string&) override { token.clear(); return ResultCode::Ok; }
};
AuthClient::Reply tokens() {
  AuthClient::Reply value;
  value.account_id = "acc_" + std::string(32, 'a'); value.access_token = std::string(43, 'A'); value.refresh_token = std::string(43, 'R');
  value.session_expires_at = std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::system_clock::now().time_since_epoch()).count() + 3600;
  value.refresh_expires_at = value.session_expires_at + 86400;
  return value;
}
bool finish(AuthClient& client) {
  QElapsedTimer clock; clock.start();
  while (client.view().busy && clock.elapsed() < 3000) { QCoreApplication::processEvents(); QThread::msleep(2); }
  return !client.view().busy;
}
AuthClient::Request login() {
  AuthClient::Request value; value.operation = AuthOperation::PasswordLogin;
  value.phone = "13800000001"; value.password = "Long test passphrase 2026"; return value;
}
void test_login_restore_logout_and_threads() {
  pixcake::account::AccountSession account;
  auto store = std::make_shared<MemoryStore>();
  const auto owner = std::this_thread::get_id();
  std::atomic<bool> worker{false};
  std::atomic<int> revokes{0};
  auto fetch = [&](const AuthClient::Request& input, auto) {
    worker = std::this_thread::get_id() != owner;
    if (input.operation == AuthOperation::Logout) { ++revokes; return AuthClient::Reply{}; }
    if (input.operation == AuthOperation::Configure) { AuthClient::Reply reply; reply.sms_enabled = true; return reply; }
    return tokens();
  };
  AuthClient client(account, {"https://cloud.invalid", ""}, false, store, fetch);
  client.on_changed([&] { EXPECT(std::this_thread::get_id() == owner); });
  EXPECT(client.configure() && finish(client));
  EXPECT(client.view().configured && client.submit(login(), true) && finish(client));
  EXPECT(worker && account.signed_in() && store->writes == 1 && store->token.size() == 43);
  const auto remembered = store->token;
  client.logout(); EXPECT(!account.signed_in() && store->token.empty()); EXPECT(finish(client) && revokes == 1);
  store->token = remembered;
  EXPECT(client.configure() && finish(client));
  EXPECT(account.signed_in() && store->writes == 2);
  client.logout(); EXPECT(finish(client));
  EXPECT(client.submit(login(), false) && finish(client));
  EXPECT(account.signed_in() && store->token.empty());
}
void test_cancelled_login_revokes_late_result() {
  pixcake::account::AccountSession account;
  auto store = std::make_shared<MemoryStore>();
  std::promise<AuthClient::Reply> promise;
  auto pending = promise.get_future().share();
  std::atomic<int> revokes{0};
  AuthClient client(account, {"https://cloud.invalid", ""}, false, store, [&](const auto& input, auto) {
    if (input.operation == AuthOperation::Configure) return AuthClient::Reply{};
    if (input.operation == AuthOperation::Logout) { ++revokes; return AuthClient::Reply{}; }
    return pending.get();
  });
  EXPECT(client.configure() && finish(client));
  EXPECT(client.submit(login(), true)); EXPECT(!client.submit(login(), true));
  client.logout(); promise.set_value(tokens()); EXPECT(finish(client));
  EXPECT(!account.signed_in() && store->token.empty() && store->writes == 0 && revokes == 1);
}
void test_development_mode_requires_explicit_client_opt_in() {
  pixcake::account::AccountSession account;
  auto fetch = [](const auto& input, auto) {
    AuthClient::Reply reply; reply.development = true; reply.sms_enabled = true;
    if (input.operation == AuthOperation::SendCode) {
      reply.challenge_id = "otp_" + std::string(32, 'c'); reply.development_code = "142857"; reply.resend_after = 60;
    }
    return reply;
  };
  AuthClient release(account, {"https://cloud.invalid", ""}, false, {}, fetch);
  EXPECT(release.configure() && finish(release));
  EXPECT(!release.view().configured && release.view().error == AuthError::DevelopmentDisabled);
  AuthClient development(account, {"https://cloud.invalid", ""}, true, {}, fetch);
  EXPECT(development.configure() && finish(development));
  AuthClient::Request request; request.operation = AuthOperation::SendCode; request.phone = "13800000001";
  EXPECT(development.submit(request, false) && finish(development));
  EXPECT(development.view().development_code == "142857" && development.view().phone == request.phone);
  account.sign_out(); QThread::msleep(30); QCoreApplication::processEvents();
  EXPECT(development.view().development_code.empty());
}
}
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  test_login_restore_logout_and_threads(); test_cancelled_login_revokes_late_result();
  test_development_mode_requires_explicit_client_opt_in();
  return failures ? 1 : 0;
}
