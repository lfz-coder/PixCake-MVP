#pragma once
#include "cloud_client.hpp"
#include "pixcake/ui/shell/cloud_transfer_view.hpp"
#include <atomic>

namespace pixcake::shell {
class CloudTransferClient {
 public:
  using TransportFactory = std::function<std::unique_ptr<net::HttpTransport>()>;
  CloudTransferClient(account::AccountSession& account, CloudOptions options, LogSink* log = nullptr,
                      TransportFactory transport_factory = {});
  ~CloudTransferClient();
  void set_directory(std::string directory);
  ui::shell::CloudTransferView view() const;
  void on_changed(std::function<void()> callback) { changed_ = std::move(callback); }
  bool upload(std::int64_t local_id, std::string space_id);
  bool download(std::string project_id, std::string space_id, std::int64_t revision);
  bool retry(bool keep_both = false);
  void pause();
  bool discard();
 private:
  struct Operation {
    enum Kind { Upload, Download, Resume, Discard } kind = Resume;
    bool keep_both = false;
    std::int64_t local_id = 0;
    std::string project_id;
    std::string space_id;
    std::int64_t revision = 0;
  };
  struct Job {
    std::atomic_bool cancelled{false};
    std::atomic<cloud_sync::Phase> phase{cloud_sync::Phase::Idle};
    std::atomic<std::uint64_t> done{0}, total{0};
  };
  struct Outcome { ResultCode code = ResultCode::Ok; bool conflict = false; bool pending = false; bool discarded = false; };
  bool start(Operation operation);
  void poll();
  void changed();
  account::AccountSession& account_;
  CloudOptions options_;
  LogSink* log_;
  TransportFactory transport_factory_;
  std::string directory_;
  std::string running_directory_;
  std::uint64_t generation_ = 0;
  ui::shell::CloudTransferView view_;
  std::future<Outcome> pending_;
  std::shared_ptr<Job> job_;
  QTimer timer_;
  std::chrono::steady_clock::time_point retry_at_{};
  bool automatically_retry_ = false;
  unsigned automatic_retries_ = 0;
  std::function<void()> changed_;
};
}
