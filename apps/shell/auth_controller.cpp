#include "shell_controller.hpp"

namespace pixcake::shell {
void ShellController::attach_auth(account::AccountSession& account, CloudOptions options, bool development,
                                  std::shared_ptr<account::CredentialStore> credentials, std::function<void()> authenticated) {
  auth_client_ = std::make_unique<AuthClient>(account, std::move(options), development, std::move(credentials));
  auth_client_->on_changed([this, &account, authenticated = std::move(authenticated)] {
    if (account.signed_in() && display_.page() == Page::Login) {
      if (authenticated) authenticated();
      display_.signed_in(account);
      if (changed_) changed_();
    } else if (!account.signed_in() && display_.page() != Page::Login) {
      if (cloud_transfer_) cloud_transfer_->pause();
      close_project();
      exports_.reset(); projects_.reset();
      display_.signed_out();
      if (changed_) changed_();
    }
  });
  auth_client_->configure();
}
ui::shell::AuthView ShellController::auth_view() const {
  return auth_client_ ? auth_client_->view() : ui::shell::AuthView{};
}
void ShellController::request_auth(account::AuthRequest request, bool remember) {
  if (auth_client_) auth_client_->submit(std::move(request), remember);
}
void ShellController::request_auth_configure() { if (auth_client_) auth_client_->configure(); }
void ShellController::request_logout() {
  if (cloud_transfer_) cloud_transfer_->pause();
  if (auth_client_) auth_client_->logout();
}
}  // namespace pixcake::shell
