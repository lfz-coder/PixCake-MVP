#pragma once
#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <string>

namespace pixcake::account {
enum class AuthOperation { Configure, SendCode, SmsLogin, PasswordLogin, Register, ResetPassword, Refresh, Logout };
enum class AuthError {
  None, Unconfigured, Network, InvalidInput, CodeInvalid, CodeExpired, RateLimited,
  CredentialsInvalid, AccountExists, PasswordWeak, DevelopmentDisabled, StorageFailed, Stale
};
struct AuthRequest {
  AuthOperation operation = AuthOperation::Configure;
  std::string phone;
  std::string purpose = "login";
  std::string challenge_id;
  std::string code;
  std::string password;
  std::string token;
};
struct AuthReply {
  ResultCode code = ResultCode::Ok;
  AuthError error = AuthError::None;
  bool sms_enabled = false;
  bool development = false;
  std::string challenge_id;
  std::string development_code;
  std::int64_t code_expires_at = 0;
  std::int64_t resend_after = 0;
  std::string account_id;
  std::string access_token;
  std::string refresh_token;
  std::int64_t session_expires_at = 0;
  std::int64_t refresh_expires_at = 0;
};
}  // namespace pixcake::account
