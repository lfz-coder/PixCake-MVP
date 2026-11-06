#pragma once

#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pixcake::account {

enum class LoginMethod {
  Sms,
  Wechat,
  Business,
};

enum class SessionState {
  SignedOut,
  SignedIn,
};

struct Subscription {
  std::string package_id;
  std::int64_t expires_at = 0;
  bool shared_by_merchant = false;
};

struct AccountSnapshot {
  std::string account_id;
  std::optional<std::string> merchant_id;
  LoginMethod login_method = LoginMethod::Sms;
  std::string session_handle;
  std::int64_t remaining_sheets = 0;
  std::int64_t creative_points = 0;
  std::vector<Subscription> subscriptions;
  std::vector<std::string> toolbar_capability_ids;
  std::vector<std::string> favorite_colors;
};

struct CacheLoadResult {
  ResultCode code = ResultCode::Ok;
  bool restored_session = false;
};

const char* login_method_name(LoginMethod method);
const char* session_state_name(SessionState state);
ResultCode validate(const AccountSnapshot& snapshot);

class AccountSession {
 public:
  AccountSession();

  CacheLoadResult load(std::optional<AccountSnapshot> cached);
  ResultCode sign_in(AccountSnapshot snapshot);
  ResultCode sign_out();
  ResultCode add_toolbar_capability(std::string capability_id);
  ResultCode remove_toolbar_capability(const std::string& capability_id);
  ResultCode add_favorite_color(std::string color_id);
  ResultCode remove_favorite_color(const std::string& color_id);

  SessionState state() const;
  bool signed_in() const;
  const AccountSnapshot& snapshot() const;

 private:
  AccountSnapshot snapshot_;
  SessionState state_ = SessionState::SignedOut;
};

}  // namespace pixcake::account
