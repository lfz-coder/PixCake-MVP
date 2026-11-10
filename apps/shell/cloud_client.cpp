#include "cloud_client.hpp"

#ifdef PIXCAKE_ENABLE_CLOUD_HTTP
#include "pixcake/net/curl_http_transport.hpp"
#endif

#include <QDebug>

#include <chrono>
#include <exception>
#include <utility>

namespace pixcake::shell {
namespace {
using ui::shell::CloudState;
std::int64_t unix_now() {
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
}
CloudClient::CloudClient(account::AccountSession& account, CloudOptions options, Fetch fetch, LogSink* log)
    : account_(account), options_(std::move(options)), fetch_(std::move(fetch)), log_(log),
      observed_generation_(account.generation()) {
#ifdef PIXCAKE_ENABLE_CLOUD_HTTP
  if (!fetch_) {
    fetch_ = [configuration = options_](const cloud::CloudIdentity& identity, const std::string& space,
                                       std::chrono::milliseconds timeout) {
      net::CurlHttpTransport transport(configuration.origin, configuration.ca_file);
      cloud_transport::BackendV1Codec codec(configuration.origin);
      return cloud_transport::query_directory(transport, codec, identity, space, timeout);
    };
  }
#endif
  view_ = empty_view();
  timer_.setInterval(25);
  QObject::connect(&timer_, &QTimer::timeout, &timer_, [this] { poll(); });
  timer_.start();
}
CloudClient::~CloudClient() {
  timer_.stop();
  changed_ = {};
  if (pending_.valid()) pending_.wait();
}
ui::shell::CloudView CloudClient::empty_view() const {
  ui::shell::CloudView value;
  if (!fetch_ || options_.origin.empty()) value.state = CloudState::NotConfigured;
  else value.state = account_.signed_in() ? CloudState::Idle : CloudState::SignedOut;
  return value;
}
ui::shell::CloudView CloudClient::view() const {
  auto value = account_.generation() == observed_generation_ ? view_ : empty_view();
  if (account_.generation() == observed_generation_ && expires_at_ && expires_at_ <= unix_now()) {
    value = {}; value.state = CloudState::Denied;
  }
  value.busy = pending_.valid();
  return value;
}
void CloudClient::notify() { if (changed_) changed_(); }
void CloudClient::record(ResultCode code) {
  LogRecord record;
  record.module = "cloud"; record.event = "cloud.directory.query"; record.code = code;
  write_log(log_, record);
  qInfo("cloud.directory: %s", result_code_label(code));
}
void CloudClient::synchronize() {
  if (account_.generation() != observed_generation_) {
    observed_generation_ = account_.generation();
    view_ = empty_view();
    expires_at_ = 0;
    restore_token_.clear();
    notify();
  } else if (expires_at_ && expires_at_ <= unix_now()) {
    view_ = {}; view_.state = CloudState::Denied; expires_at_ = 0;
    notify();
  }
}
bool CloudClient::refresh(const std::string& space_id) {
  synchronize();
  if (!account_.signed_in()) { view_ = empty_view(); notify(); return false; }
  return start({account_.snapshot().account_id, account_.snapshot().session_handle}, space_id, false);
}
bool CloudClient::restore(std::string token) {
  synchronize();
  if (account_.signed_in()) return false;
  return start({{}, std::move(token)}, {}, true);
}
bool CloudClient::start(cloud::CloudIdentity identity, std::string space_id, bool restoring) {
  if (pending_.valid()) return false;
  if (!fetch_ || options_.origin.empty()) { view_ = empty_view(); notify(); return false; }
  if (options_.timeout.count() <= 0 || options_.timeout.count() > 5000) {
    view_.state = CloudState::Failed; view_.code = ResultCode::StateConflict; notify(); return false;
  }
  pending_generation_ = account_.generation();
  restoring_ = restoring;
  restore_token_ = restoring ? identity.session_handle : std::string{};
  try {
    pending_ = std::async(std::launch::async,
        [fetch = fetch_, identity = std::move(identity), space = std::move(space_id), timeout = options_.timeout] {
          try { return fetch(identity, space, timeout); }
          catch (...) { Reply reply; reply.code = ResultCode::NetworkFailed; return reply; }
        });
    view_.state = CloudState::Loading;
    notify();
    return true;
  } catch (const std::exception&) {
    restore_token_.clear();
    view_.state = CloudState::Failed; view_.code = ResultCode::NetworkFailed; notify(); return false;
  }
}
void CloudClient::poll() {
  synchronize();
  if (!pending_.valid() || pending_.wait_for(std::chrono::milliseconds{0}) != std::future_status::ready) return;
  auto reply = pending_.get();
  if (account_.generation() != pending_generation_) {
    restore_token_.clear(); record(ResultCode::StateConflict); notify(); return;
  }
  if (reply.code == ResultCode::Ok && reply.account.session_expires_at <= unix_now()) reply.code = ResultCode::PermissionDenied;
  if (reply.code == ResultCode::Ok && restoring_) {
    account::AccountSnapshot snapshot;
    snapshot.account_id = reply.account.account_id;
    snapshot.session_handle = restore_token_;
    reply.code = account_.sign_in(std::move(snapshot));
    observed_generation_ = account_.generation();
  }
  restore_token_.clear();
  view_.code = reply.code;
  if (reply.code == ResultCode::Ok) {
    expires_at_ = reply.account.session_expires_at;
    view_.state = CloudState::Ready; view_.has_snapshot = true;
    view_.spaces = std::move(reply.spaces); view_.projects = std::move(reply.projects);
    view_.selected_space = std::move(reply.selected_space);
  } else if (reply.code == ResultCode::PermissionDenied) {
    view_ = {}; view_.state = CloudState::Denied; view_.code = reply.code; expires_at_ = 0;
  } else {
    view_.state = CloudState::Failed;
  }
  record(reply.code);
  notify();
}
}  // namespace pixcake::shell
