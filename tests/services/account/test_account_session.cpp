#include "pixcake/account/account_session.hpp"

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

int main() {
  test_session_and_two_balances();
  test_bad_cache_keeps_current_session();
  test_layout_limits_and_logout_scope();
  return g_failures == 0 ? 0 : 1;
}
