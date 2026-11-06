#include "pixcake/account/account_session.hpp"
#include "pixcake/account/login_flow.hpp"
#include "pixcake/support/user_notice.hpp"

#include <cstdio>
#include <string>

namespace {

int g_failures = 0;

void expect(bool condition, const char* expression, int line) {
  if (!condition) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, expression);
    ++g_failures;
  }
}

#define EXPECT(expression) expect(static_cast<bool>(expression), #expression, __LINE__)

pixcake::account::AccountSnapshot snapshot() {
  pixcake::account::AccountSnapshot value;
  value.account_id = "account-1";
  value.merchant_id = "merchant-1";
  value.login_method = pixcake::account::LoginMethod::Business;
  value.session_handle = "opaque-session";
  value.remaining_sheets = 12;
  value.creative_points = 300;
  value.subscriptions.push_back({"package-a", 1800000000, true});
  return value;
}

void test_session_and_two_balances() {
  using pixcake::ResultCode;
  pixcake::account::AccountSession session;
  EXPECT(!session.signed_in());
  EXPECT(session.load(std::nullopt).code == ResultCode::Ok);
  EXPECT(!session.signed_in());
  auto value = snapshot();
  EXPECT(pixcake::account::validate(value) == ResultCode::Ok);
  EXPECT(session.sign_in(value) == ResultCode::Ok);
  EXPECT(session.signed_in());
  EXPECT(session.snapshot().remaining_sheets == 12);
  EXPECT(session.snapshot().creative_points == 300);
  value.remaining_sheets = 0;
  value.creative_points = 7;
  EXPECT(session.sign_in(value) == ResultCode::Ok);
  EXPECT(session.snapshot().remaining_sheets == 0);
  EXPECT(session.snapshot().creative_points == 7);
}

void test_bad_cache_keeps_current_session() {
  using pixcake::ResultCode;
  pixcake::account::AccountSession session;
  EXPECT(session.sign_in(snapshot()) == ResultCode::Ok);
  auto invalid = snapshot();
  invalid.creative_points = -1;
  const auto result = session.load(invalid);
  EXPECT(result.code == ResultCode::StateConflict);
  EXPECT(session.signed_in());
  EXPECT(session.snapshot().creative_points == 300);
}

void test_layout_limits_and_logout_scope() {
  using pixcake::ResultCode;
  pixcake::account::AccountSession session;
  EXPECT(session.sign_in(snapshot()) == ResultCode::Ok);
  for (int index = 0; index < 5; ++index) {
    EXPECT(session.add_toolbar_capability("cap-" + std::to_string(index)) == ResultCode::Ok);
  }
  EXPECT(session.add_toolbar_capability("cap-over") == ResultCode::StateConflict);
  for (int index = 0; index < 6; ++index) {
    EXPECT(session.add_favorite_color("color-" + std::to_string(index)) == ResultCode::Ok);
  }
  EXPECT(session.add_favorite_color("color-over") == ResultCode::StateConflict);
  EXPECT(session.remove_toolbar_capability("cap-2") == ResultCode::Ok);
  EXPECT(session.add_toolbar_capability("cap-new") == ResultCode::Ok);
  EXPECT(session.remove_favorite_color("color-2") == ResultCode::Ok);
  EXPECT(session.add_favorite_color("color-new") == ResultCode::Ok);
  EXPECT(session.sign_out() == ResultCode::Ok);
  EXPECT(!session.signed_in());
  EXPECT(session.sign_out() == ResultCode::StateConflict);
  EXPECT(session.add_toolbar_capability("late") == ResultCode::StateConflict);
}

}  // namespace

// S-25：账号会话就是内核看到的登录态（kernel/support 的 SessionState）。
void test_s25_account_session_is_the_session_state() {
  pixcake::account::AccountSession session;
  const pixcake::SessionState& state = session;
  EXPECT(!state.signed_in());
}

// O-13、需求第 223、897 行：验证码错误、过期、未收到、网络失败各有自己的说明，并停留在登录页。
class FakeGateway final : public pixcake::account::LoginGateway {
 public:
  pixcake::ResultCode send = pixcake::ResultCode::Ok;
  pixcake::account::VerifyReply reply;
  int sent = 0;
  pixcake::ResultCode send_code(const std::string&) override {
    ++sent;
    return send;
  }
  pixcake::account::VerifyReply verify(const std::string&, const std::string&) override { return reply; }
};

void test_o13_login_failures_are_distinct_and_stay_on_login() {
  using pixcake::account::LoginOutcome;
  pixcake::account::AccountSession session;
  FakeGateway gateway;
  pixcake::account::LoginFlow flow(session, gateway);

  EXPECT(flow.submit("123456").outcome == LoginOutcome::NoCodeRequested);
  EXPECT(flow.request_code("").outcome == LoginOutcome::PhoneInvalid);
  gateway.send = pixcake::ResultCode::NetworkFailed;
  const auto offline = flow.request_code("13800000000");
  EXPECT(offline.outcome == LoginOutcome::NetworkFailed && offline.stays_on_login);
  gateway.send = pixcake::ResultCode::Ok;
  EXPECT(flow.request_code("13800000000").outcome == LoginOutcome::CodeSent);
  EXPECT(flow.submit("12345").outcome == LoginOutcome::CodeFormatInvalid);

  gateway.reply.verdict = pixcake::account::VerifyVerdict::Wrong;
  const auto wrong = flow.submit("111111");
  gateway.reply.verdict = pixcake::account::VerifyVerdict::Expired;
  const auto expired = flow.submit("222222");
  const auto not_received = flow.report_not_received();
  gateway.reply.code = pixcake::ResultCode::NetworkFailed;
  const auto network = flow.submit("333333");
  const std::vector<pixcake::account::LoginNotice> failures = {wrong, expired, not_received, network};
  EXPECT(wrong.outcome == LoginOutcome::CodeWrong && expired.outcome == LoginOutcome::CodeExpired &&
         not_received.outcome == LoginOutcome::CodeNotReceived && network.outcome == LoginOutcome::NetworkFailed);
  for (std::size_t i = 0; i < failures.size(); ++i) {
    EXPECT(failures[i].stays_on_login && !failures[i].text.empty());
    for (std::size_t j = i + 1; j < failures.size(); ++j) EXPECT(failures[i].text != failures[j].text);
  }
  EXPECT(!session.signed_in());

  gateway.reply = pixcake::account::VerifyReply{};
  gateway.reply.account = snapshot();
  const auto done = flow.submit("444444");
  EXPECT(done.outcome == LoginOutcome::SignedIn && !done.stays_on_login);
  EXPECT(session.signed_in());
}

// O-16：点数或张数不足、网络失败、极速导出占用、状态冲突用不同的文案，不说“出错了”。
void test_o16_notices_do_not_share_text() {
  using pixcake::NoticeScene;
  using pixcake::ResultCode;
  const std::vector<ResultCode> codes = {ResultCode::InsufficientBalance, ResultCode::NetworkFailed,
                                         ResultCode::FastExportBusy, ResultCode::StateConflict};
  for (std::size_t i = 0; i < codes.size(); ++i) {
    const auto left = pixcake::notice_for(NoticeScene::Edit, codes[i]);
    EXPECT(!left.text.empty() && left.text.find("出错了") == std::string::npos);
    for (std::size_t j = i + 1; j < codes.size(); ++j) {
      EXPECT(left.text != pixcake::notice_for(NoticeScene::Edit, codes[j]).text);
    }
  }
  EXPECT(pixcake::notice_for(NoticeScene::Edit, ResultCode::Ok).text.empty());
}

int main() {
  test_o13_login_failures_are_distinct_and_stay_on_login();
  test_o16_notices_do_not_share_text();
  test_s25_account_session_is_the_session_state();
  test_session_and_two_balances();
  test_bad_cache_keeps_current_session();
  test_layout_limits_and_logout_scope();
  return g_failures == 0 ? 0 : 1;
}
