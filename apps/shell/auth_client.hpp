#pragma once
#include "cloud_client.hpp"
#include "pixcake/account/credential_store.hpp"
#include "pixcake/ui/shell/auth_display.hpp"

namespace pixcake::shell {
class AuthClient final {
 public:
  using Request = account::AuthRequest;
  using Reply = account::AuthReply;
  using Fetch = std::function<Reply(const Request&, std::chrono::milliseconds)>;
  AuthClient(account::AccountSession& account, CloudOptions options, bool allow_development,
             std::shared_ptr<account::CredentialStore> credentials = {}, Fetch fetch = {});
  ~AuthClient();
  void on_changed(std::function<void()> changed) { changed_ = std::move(changed); }
  ui::shell::AuthView view() const;
  bool configure();
  bool submit(Request request, bool remember);
  void logout();

 private:
  bool start(Request request);
  void poll();
  void notify();
  account::AccountSession& account_;
  CloudOptions options_;
  bool allow_development_ = false;
  std::shared_ptr<account::CredentialStore> credentials_;
  Fetch fetch_;
  ui::shell::AuthView view_;
  std::future<Reply> pending_;
  account::AuthOperation operation_ = account::AuthOperation::Configure;
  std::uint64_t generation_ = 0;
  std::uint64_t observed_generation_ = 0;
  std::string pending_phone_;
  std::string pending_purpose_;
  std::string refresh_token_;
  std::string logout_token_;
  std::int64_t expires_at_ = 0;
  std::int64_t next_refresh_at_ = 0;
  bool remember_ = false;
  QTimer timer_;
  std::function<void()> changed_;
};
}  // namespace pixcake::shell
