#pragma once
#include "pixcake/account/auth_types.hpp"

namespace pixcake::ui::shell {
struct AuthView {
  bool configured = false;
  bool sms_enabled = false;
  bool development = false;
  bool busy = false;
  bool signed_in = false;
  bool code_sent = false;
  account::AuthError error = account::AuthError::Unconfigured;
  std::string phone;
  std::string purpose;
  std::string challenge_id;
  std::string development_code;
  std::int64_t resend_at = 0;
};
const std::string& auth_status_text(const AuthView& view);
}  // namespace pixcake::ui::shell
