#include "cloud_transfer_client.hpp"
#ifdef PIXCAKE_ENABLE_CLOUD_HTTP
#include "pixcake/net/curl_http_transport.hpp"
#include "pixcake/platform/mac_source_access.hpp"
#include "pixcake/project_session/project_session.hpp"
#endif

namespace pixcake::shell {
CloudTransferClient::CloudTransferClient(account::AccountSession& account, CloudOptions options, LogSink* log,
                                        TransportFactory transport_factory)
    : account_(account), options_(std::move(options)), log_(log), transport_factory_(std::move(transport_factory)),
      generation_(account.generation()) {
#ifdef PIXCAKE_ENABLE_CLOUD_HTTP
  if (!transport_factory_) transport_factory_ = [configuration = options_] {
    return std::make_unique<net::CurlHttpTransport>(configuration.origin, configuration.ca_file);
  };
#endif
  timer_.setInterval(100);
  QObject::connect(&timer_, &QTimer::timeout, &timer_, [this] { poll(); }); timer_.start();
}
CloudTransferClient::~CloudTransferClient() {
  timer_.stop(); changed_ = {}; if (job_) job_->cancelled = true; if (pending_.valid()) pending_.wait();
}
void CloudTransferClient::changed() { if (changed_) changed_(); }
ui::shell::CloudTransferView CloudTransferClient::view() const {
  if (!account_.signed_in() || generation_ != account_.generation()) return {};
  auto result = view_; result.busy = pending_.valid();
  if (result.busy && job_) {
    result.phase = job_->cancelled.load() ? cloud_sync::Phase::Paused : job_->phase.load();
    result.done = job_->done; result.total = job_->total;
  }
  return result;
}
void CloudTransferClient::set_directory(std::string directory) {
  if (directory_ == directory) return;
  pause(); directory_ = std::move(directory); view_ = {};
#ifdef PIXCAKE_ENABLE_CLOUD_HTTP
  view_.configured = !directory_.empty() && !options_.origin.empty();
  if (view_.configured && !pending_.valid()) {
    try {
      std::atomic_bool cancel{false}; auto transport = transport_factory_();
      if (!transport) return;
      cloud_sync::SyncEngine engine(directory_, options_.origin, *transport, cancel);
      view_.pending = engine.pending();
      if (view_.pending) view_.phase = cloud_sync::Phase::Paused;
    } catch (...) { view_.code = ResultCode::IoFailed; view_.phase = cloud_sync::Phase::Failed; }
  }
#endif
  generation_ = account_.generation(); changed();
}
bool CloudTransferClient::upload(std::int64_t local, std::string space) {
  automatic_retries_ = 0;
  Operation operation; operation.kind = Operation::Upload;
  operation.local_id = local; operation.space_id = std::move(space); return start(std::move(operation));
}
bool CloudTransferClient::download(std::string project, std::string space, std::int64_t revision) {
  automatic_retries_ = 0;
  Operation operation; operation.kind = Operation::Download; operation.project_id = std::move(project);
  operation.space_id = std::move(space); operation.revision = revision; return start(std::move(operation));
}
bool CloudTransferClient::retry(bool keep_both) {
  automatic_retries_ = 0; Operation operation; operation.keep_both = keep_both; return start(operation);
}
bool CloudTransferClient::discard() { Operation operation; operation.kind = Operation::Discard; return start(operation); }
void CloudTransferClient::pause() {
  automatically_retry_ = false;
  if (job_) job_->cancelled = true;
  if (view_.pending || pending_.valid()) view_.phase = cloud_sync::Phase::Paused;
  changed();
}
bool CloudTransferClient::start(Operation operation) {
#ifdef PIXCAKE_ENABLE_CLOUD_HTTP
  if (!account_.signed_in() || !view_.configured || pending_.valid()) return false;
  if (view_.pending && (operation.kind == Operation::Upload || operation.kind == Operation::Download)) return false;
  generation_ = account_.generation();
  running_directory_ = directory_;
  job_ = std::make_shared<Job>(); job_->phase = operation.kind == Operation::Upload ?
    cloud_sync::Phase::Preparing : cloud_sync::Phase::Downloading;
  automatically_retry_ = false; view_.code = ResultCode::Ok;
  try {
    pending_ = std::async(std::launch::async, [operation = std::move(operation), job = job_,
        directory = directory_, options = options_, factory = transport_factory_, token = account_.snapshot().session_handle] {
      Outcome outcome;
      try {
        auto transport = factory();
        if (!transport) { outcome.code = ResultCode::StateConflict; return outcome; }
        cloud_sync::SyncEngine engine(directory, options.origin, *transport, job->cancelled, [job](cloud_sync::Progress progress) {
          job->phase = progress.phase; job->done = progress.done; job->total = progress.total;
        });
        struct WorkerSession final : SessionState {
          const std::atomic_bool& cancelled;
          explicit WorkerSession(const std::atomic_bool& value) : cancelled(value) {}
          bool signed_in() const override { return !cancelled.load(); }
        } session(job->cancelled);
        platform::MacSourceAccess access;
        std::unique_ptr<project_session::ProjectSession> projects;
        std::string diagnostic;
        const auto open_projects = [&] {
          return projects ? ResultCode::Ok : project_session::ProjectSession::open(directory, session, access,
              &platform::MacSourceAccess::create_bookmark, projects, diagnostic);
        };
        std::vector<std::unique_ptr<platform::SourceScope>> scopes;
        if (operation.kind == Operation::Upload) {
          project_session::ProjectSnapshot snapshot;
          outcome.code = open_projects();
          if (outcome.code == ResultCode::Ok) outcome.code = projects->describe_snapshot(operation.local_id, snapshot);
          if (outcome.code != ResultCode::Ok) return outcome;
          for (const auto& photo : snapshot.photos) {
            auto scope = std::make_unique<platform::SourceScope>(photo.source_access);
            if (scope->code() != ResultCode::Ok || scope->path() != photo.source_file) {
              outcome.code = ResultCode::PermissionDenied; return outcome;
            }
            scopes.push_back(std::move(scope));
          }
          outcome.code = engine.prepare_upload(snapshot, operation.local_id, operation.space_id);
        } else if (operation.kind == Operation::Download) {
          outcome.code = engine.prepare_download(operation.project_id, operation.space_id, operation.revision);
        } else if (operation.kind == Operation::Discard) {
          outcome.code = engine.discard(token); outcome.discarded = outcome.code == ResultCode::Ok;
        } else if (operation.keep_both) outcome.code = engine.keep_both(token);
        scopes.clear();
        if (outcome.code == ResultCode::Ok && !outcome.discarded) {
          auto result = engine.run(token); outcome.code = result.code; outcome.conflict = result.conflict;
          outcome.discarded = result.discarded;
          if (outcome.code == ResultCode::Ok && !outcome.discarded && !job->cancelled.load()) {
            outcome.code = open_projects();
            if (outcome.code == ResultCode::Ok) {
              if (result.downloading) {
                outcome.code = projects->restore_snapshot(result.snapshot, result.owned_root, result.receipt, result.local_id);
              } else outcome.code = projects->mark_uploaded(result.local_id);
            }
            if (outcome.code == ResultCode::Ok) outcome.code = engine.complete(result.local_id);
          }
        }
        outcome.pending = engine.pending();
      } catch (...) { outcome.code = ResultCode::IoFailed; outcome.pending = true; }
      return outcome;
    });
  } catch (...) { view_.code = ResultCode::IoFailed; view_.phase = cloud_sync::Phase::Failed; changed(); return false; }
  changed(); return true;
#else
  (void)operation; return false;
#endif
}
void CloudTransferClient::poll() {
  if (generation_ != account_.generation()) {
    pause(); generation_ = account_.generation();
    if (!account_.signed_in()) { directory_.clear(); view_ = {}; }
  }
  if (!pending_.valid()) {
    if (automatically_retry_ && account_.signed_in() && std::chrono::steady_clock::now() >= retry_at_) {
      ++automatic_retries_; start(Operation{});
    }
    return;
  }
  if (pending_.wait_for(std::chrono::milliseconds{0}) != std::future_status::ready) return;
  const auto outcome = pending_.get();
  if (directory_ != running_directory_) {
    auto directory = directory_; directory_.clear(); set_directory(std::move(directory));
    return;
  }
  if (job_->cancelled.load()) {
    view_.phase = cloud_sync::Phase::Paused; view_.pending = outcome.pending;
    if (!account_.signed_in()) view_ = {};
    changed(); return;
  }
  view_.code = outcome.code; view_.pending = outcome.pending;
  view_.phase = outcome.conflict ? cloud_sync::Phase::Conflict : outcome.code != ResultCode::Ok ? cloud_sync::Phase::Failed :
    outcome.discarded ? cloud_sync::Phase::Idle : cloud_sync::Phase::Complete;
  automatically_retry_ = outcome.pending && outcome.code == ResultCode::NetworkFailed && automatic_retries_ < 3;
  view_.automatic_retry = automatically_retry_;
  retry_at_ = std::chrono::steady_clock::now() + std::chrono::seconds{30};
  if (log_) { LogRecord record; record.message = "cloud.transfer"; record.code = outcome.code; log_->write(record); }
  changed();
}
}  // namespace pixcake::shell
