#pragma once

#include "pixcake/account/account_session.hpp"
#include "pixcake/cloud_transport/cloud_directory_query.hpp"
#include "pixcake/ui/shell/cloud_display.hpp"
#include "pixcake/support/log_sink.hpp"

#include <QTimer>

#include <functional>
#include <future>

namespace pixcake::shell {
struct CloudOptions {
  std::string origin;
  std::string ca_file;
  std::chrono::milliseconds timeout{5000};
};

// Qt-owner-thread only. Worker jobs own their immutable inputs and local
// service sessions. The account must outlive this client. Destruction waits
// for the single bounded request after stopping all owner-thread callbacks.
class CloudClient final {
 public:
  using Reply = cloud_transport::CloudDirectoryReply;
  using Fetch = std::function<Reply(const cloud::CloudIdentity&, const std::string&, std::chrono::milliseconds)>;
  CloudClient(account::AccountSession& account, CloudOptions options, Fetch fetch = {}, LogSink* log = nullptr);
  ~CloudClient();
  CloudClient(const CloudClient&) = delete;
  CloudClient& operator=(const CloudClient&) = delete;
  void on_changed(std::function<void()> callback) { changed_ = std::move(callback); }
  ui::shell::CloudView view() const;
  bool refresh(const std::string& space_id = {});
  // For a credential supplied by a trusted cache or explicit development
  // harness. The server, never the caller, supplies the account identity.
  bool restore(std::string token);

 private:
  ui::shell::CloudView empty_view() const;
  void synchronize();
  bool start(cloud::CloudIdentity identity, std::string space_id, bool restoring);
  void poll();
  void notify();
  void record(ResultCode code);
  account::AccountSession& account_;
  CloudOptions options_;
  Fetch fetch_;
  LogSink* log_ = nullptr; // Owner-thread use; must outlive this client.
  ui::shell::CloudView view_;
  std::uint64_t observed_generation_ = 0;
  std::uint64_t pending_generation_ = 0;
  std::int64_t expires_at_ = 0;
  bool restoring_ = false;
  std::string restore_token_;
  std::future<Reply> pending_;
  QTimer timer_;
  std::function<void()> changed_;
};
}  // namespace pixcake::shell
