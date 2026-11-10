#include "pixcake/ui/shell/auth_display.hpp"
#include "pixcake/ui/shell/page_copy.hpp"

namespace pixcake::ui::shell {
const std::string& auth_status_text(const AuthView& view) {
  using account::AuthError;
  if (view.busy) return page_text("auth.busy");
  switch (view.error) {
    case AuthError::None:
      return page_text(view.code_sent ? view.development ? "auth.mock-sent" : "auth.code-sent" :
                       view.development ? "auth.mock-mode" : "auth.ready");
    case AuthError::Unconfigured: return page_text("login.unconfigured");
    case AuthError::Network: return page_text("auth.network");
    case AuthError::InvalidInput: return page_text("auth.invalid-input");
    case AuthError::CodeInvalid: return page_text("auth.code-invalid");
    case AuthError::CodeExpired: return page_text("auth.code-expired");
    case AuthError::RateLimited: return page_text("auth.rate-limited");
    case AuthError::CredentialsInvalid: return page_text("auth.credentials-invalid");
    case AuthError::AccountExists: return page_text("auth.account-exists");
    case AuthError::PasswordWeak: return page_text("auth.password-policy");
    case AuthError::DevelopmentDisabled: return page_text("auth.development-disabled");
    case AuthError::StorageFailed: return page_text("auth.storage-failed");
    case AuthError::Stale: return page_text("auth.cancelled");
  }
  return page_text("auth.network");
}
}  // namespace pixcake::ui::shell
