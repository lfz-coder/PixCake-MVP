#include "auth_client.hpp"
#ifdef PIXCAKE_ENABLE_CLOUD_HTTP
#include "pixcake/net/curl_http_transport.hpp"
#endif

namespace pixcake::shell {
namespace {
using account::AuthError;
using account::AuthOperation;
std::int64_t now() {
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
}
AuthClient::AuthClient(account::AccountSession& account, CloudOptions options, bool allow_development,
                       std::shared_ptr<account::CredentialStore> credentials, Fetch fetch)
    : account_(account), options_(std::move(options)), allow_development_(allow_development),
      credentials_(std::move(credentials)), fetch_(std::move(fetch)), observed_generation_(account.generation()) {
#ifdef PIXCAKE_ENABLE_CLOUD_HTTP
  if (!fetch_) fetch_ = [configuration = options_](const Request& input, std::chrono::milliseconds timeout) {
    net::CurlHttpTransport transport(configuration.origin, configuration.ca_file);
    cloud_transport::BackendV1Codec codec(configuration.origin);
    net::HttpRequest wire;
    Reply reply;
    reply.code = codec.encode_auth(input, wire);
    if (reply.code != ResultCode::Ok) { reply.error = AuthError::InvalidInput; return reply; }
    return codec.decode_auth(input.operation, transport.send(wire, timeout));
  };
#endif
  timer_.setInterval(25);
  QObject::connect(&timer_, &QTimer::timeout, &timer_, [this] { poll(); });
  timer_.start();
}
AuthClient::~AuthClient() { timer_.stop(); changed_ = {}; if (pending_.valid()) pending_.wait(); }
void AuthClient::notify() { if (changed_) changed_(); }
ui::shell::AuthView AuthClient::view() const {
  auto view = view_; view.busy = pending_.valid(); view.signed_in = account_.signed_in();
  if (observed_generation_ != account_.generation()) {
    view.development_code.clear(); view.challenge_id.clear(); view.code_sent = false;
  }
  return view;
}
bool AuthClient::configure() { Request request; return start(std::move(request)); }
bool AuthClient::start(Request request) {
  if (pending_.valid()) return false;
  if (!fetch_ || options_.origin.empty()) { view_.error = AuthError::Unconfigured; notify(); return false; }
  if (options_.timeout.count() <= 0 || options_.timeout.count() > 5000) {
    view_.error = AuthError::InvalidInput; notify(); return false;
  }
  generation_ = account_.generation(); operation_ = request.operation;
  pending_phone_ = request.phone; pending_purpose_ = request.purpose;
  view_.error = AuthError::None;
  try {
    pending_ = std::async(std::launch::async, [fetch = fetch_, request = std::move(request), timeout = options_.timeout] {
      try { return fetch(request, timeout); }
      catch (...) { Reply reply; reply.code = ResultCode::NetworkFailed; reply.error = AuthError::Network; return reply; }
    });
  } catch (...) { view_.error = AuthError::Network; notify(); return false; }
  notify(); return true;
}
bool AuthClient::submit(Request request, bool remember) {
  if (!view_.configured || account_.signed_in() || pending_.valid()) return false;
  if (request.operation == AuthOperation::Configure || request.operation == AuthOperation::Refresh ||
      request.operation == AuthOperation::Logout) return false;
  if (request.operation == AuthOperation::SendCode) {
    if (!view_.sms_enabled) { view_.error = AuthError::Unconfigured; notify(); return false; }
    view_.code_sent = false; view_.development_code.clear(); view_.challenge_id.clear();
  }
  remember_ = remember;
  return start(std::move(request));
}
void AuthClient::logout() {
  logout_token_ = refresh_token_;
  account_.sign_out();
  observed_generation_ = account_.generation();
  refresh_token_.clear(); expires_at_ = 0; next_refresh_at_ = 0;
  view_.development_code.clear(); view_.challenge_id.clear(); view_.code_sent = false;
  view_.error = AuthError::None;
  const bool cache_failed = credentials_ && credentials_->erase(options_.origin) != ResultCode::Ok;
  if (!logout_token_.empty() && !pending_.valid()) {
    Request request; request.operation = AuthOperation::Logout; request.token = std::move(logout_token_); start(std::move(request));
  }
  if (cache_failed) view_.error = AuthError::StorageFailed;
  notify();
}
void AuthClient::poll() {
  if (observed_generation_ != account_.generation()) {
    observed_generation_ = account_.generation();
    refresh_token_.clear(); expires_at_ = 0; next_refresh_at_ = 0;
    view_.development_code.clear(); view_.challenge_id.clear(); view_.code_sent = false;
  }
  if (!pending_.valid()) {
    if (account_.signed_in() && expires_at_ && now() >= expires_at_) {
      account_.sign_out(); observed_generation_ = account_.generation();
      view_.error = AuthError::CredentialsInvalid; expires_at_ = 0; notify();
      if (!refresh_token_.empty() && now() >= next_refresh_at_) {
        next_refresh_at_ = now() + 60;
        Request request; request.operation = AuthOperation::Refresh; request.token = refresh_token_; start(std::move(request));
      }
      return;
    }
    if (!refresh_token_.empty() && expires_at_ && now() + 60 >= expires_at_ && now() >= next_refresh_at_) {
      next_refresh_at_ = now() + 60;
      Request request; request.operation = AuthOperation::Refresh; request.token = refresh_token_; start(std::move(request));
    }
    return;
  }
  if (pending_.wait_for(std::chrono::milliseconds{0}) != std::future_status::ready) return;
  auto reply = pending_.get();
  if (generation_ != account_.generation()) {
    view_.error = AuthError::Stale;
    // A cancelled login may have minted a server session. Revoke that result
    // without exposing it or allowing it to restore the old account.
    if (!reply.refresh_token.empty()) logout_token_ = std::move(reply.refresh_token);
    if (!logout_token_.empty()) {
      Request cleanup; cleanup.operation = AuthOperation::Logout; cleanup.token = std::move(logout_token_); start(std::move(cleanup));
    }
    notify(); return;
  }
  view_.error = reply.error;
  if (reply.code != ResultCode::Ok) {
    if (operation_ == AuthOperation::Refresh && reply.code == ResultCode::PermissionDenied) {
      account_.sign_out(); refresh_token_.clear(); expires_at_ = 0;
      observed_generation_ = account_.generation();
      if (credentials_ && credentials_->erase(options_.origin) != ResultCode::Ok) view_.error = AuthError::StorageFailed;
    }
    notify(); return;
  }
  if (operation_ == AuthOperation::Configure) {
    if (reply.development && !allow_development_) { view_.error = AuthError::DevelopmentDisabled; notify(); return; }
    view_.configured = true; view_.sms_enabled = reply.sms_enabled; view_.development = reply.development;
    if (credentials_ && !account_.signed_in()) {
      std::string token;
      if (credentials_->read(options_.origin, token) != ResultCode::Ok) view_.error = AuthError::StorageFailed;
      else if (!token.empty()) {
        remember_ = true;
        Request request; request.operation = AuthOperation::Refresh; request.token = std::move(token); start(std::move(request));
      }
    }
  } else if (operation_ == AuthOperation::SendCode) {
    if ((!view_.development || !allow_development_) && !reply.development_code.empty()) {
      view_.error = AuthError::DevelopmentDisabled; notify(); return;
    }
    view_.phone = pending_phone_; view_.purpose = pending_purpose_; view_.challenge_id = std::move(reply.challenge_id);
    view_.development_code = std::move(reply.development_code); view_.code_sent = true; view_.resend_at = now() + reply.resend_after;
  } else if (operation_ != AuthOperation::Logout) {
    if (reply.session_expires_at <= now() || reply.refresh_expires_at < reply.session_expires_at ||
        (operation_ == AuthOperation::Refresh && account_.signed_in() && reply.account_id != account_.snapshot().account_id)) {
      view_.error = AuthError::CredentialsInvalid; notify(); return;
    }
    account::AccountSnapshot snapshot;
    snapshot.account_id = reply.account_id; snapshot.session_handle = reply.access_token;
    if (account_.sign_in(std::move(snapshot)) != ResultCode::Ok) { view_.error = AuthError::CredentialsInvalid; notify(); return; }
    observed_generation_ = account_.generation();
    refresh_token_ = std::move(reply.refresh_token); expires_at_ = reply.session_expires_at;
    next_refresh_at_ = 0;
    view_.development_code.clear(); view_.challenge_id.clear(); view_.code_sent = false;
    if (credentials_) {
      const auto stored = remember_ ? credentials_->write(options_.origin, refresh_token_) : credentials_->erase(options_.origin);
      if (stored != ResultCode::Ok) view_.error = AuthError::StorageFailed;
    }
  }
  notify();
}
}  // namespace pixcake::shell
