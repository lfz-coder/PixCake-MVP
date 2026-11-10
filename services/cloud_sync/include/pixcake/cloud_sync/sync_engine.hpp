#pragma once

#include "pixcake/net/http_transport.hpp"
#include "pixcake/project_session/project_snapshot.hpp"
#include <atomic>
#include <functional>
#include <memory>

namespace pixcake::cloud_sync {
enum class Phase { Idle, Preparing, Uploading, Downloading, Restoring, Paused, Conflict, Complete, Failed };
struct Progress { Phase phase = Phase::Idle; std::uint64_t done = 0; std::uint64_t total = 0; };
struct TransferResult {
  ResultCode code = ResultCode::Ok;
  bool conflict = false;
  bool downloading = false;
  bool discarded = false;
  std::int64_t local_id = 0;
  std::string project_id;
  std::string space_id;
  std::int64_t revision = 0;
  std::string owned_root;
  std::string receipt;
  project_session::ProjectSnapshot snapshot;
};
// Serialized worker service. Fixed origin, account-owned data_directory; token
// is only a run argument. Every resumable byte/operation is journaled separately
// from credentials. Cancellation is checked between bounded requests/files.
class SyncEngine {
 public:
  SyncEngine(std::string data_directory, std::string origin, net::HttpTransport& transport,
             const std::atomic_bool& cancelled, std::function<void(Progress)> progress = {});
  ~SyncEngine();
  bool pending() const;
  ResultCode prepare_upload(const project_session::ProjectSnapshot& snapshot, std::int64_t local_id,
                            const std::string& space_id);
  ResultCode prepare_download(const std::string& project_id, const std::string& space_id, std::int64_t revision);
  ResultCode keep_both(const std::string& token);
  ResultCode discard(const std::string& token);
  TransferResult run(const std::string& token);
  // Call after the local command transaction succeeds. Downloaded originals
  // remain owned by the local project; only upload staging chunks are removed.
  ResultCode complete(std::int64_t local_id);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace pixcake::cloud_sync
