#include "pixcake/generation_session/generation_session.hpp"

#include <sys/stat.h>

#include <cerrno>
#include <utility>

namespace pixcake::generation_session {
namespace {

bool ensure_directory(const std::string& path) {
  struct stat info {};
  if (::stat(path.c_str(), &info) == 0) return S_ISDIR(info.st_mode);
  return ::mkdir(path.c_str(), 0700) == 0 || errno == EEXIST;
}

}  // namespace

ResultCode GenerationSession::open(const SessionState& session, GenerationSessionOptions options,
                                   std::unique_ptr<GenerationSession>& out, std::string& diagnostic) {
  out.reset();
  if (!session.signed_in()) {
    diagnostic = "not signed in";
    return ResultCode::StateConflict;
  }
  if (options.process.data_directory.empty() || !ensure_directory(options.process.data_directory)) {
    diagnostic = "generation data directory unavailable: " + options.process.data_directory;
    return ResultCode::IoFailed;
  }

  std::unique_ptr<SqliteLedgerLog> log;
  const std::string ledger_path = options.process.data_directory + "/ledger.db";
  ResultCode code = SqliteLedgerLog::open(ledger_path, log);
  if (code != ResultCode::Ok) {
    diagnostic = "generation ledger unavailable: " + ledger_path;
    return code;
  }
  std::unique_ptr<LedgerBook> ledger;
  code = LedgerBook::open(options.queue_policy, *log, ledger);
  if (code != ResultCode::Ok) {
    diagnostic = "generation ledger state unavailable: " + ledger_path;
    return code;
  }
  std::size_t events = 0;
  code = log->event_count(events);
  if (code == ResultCode::Ok && events == 0 && options.opening_points > 0) {
    code = ledger->credit_points(options.opening_points);
  }
  if (code != ResultCode::Ok) {
    diagnostic = "generation ledger could not be initialized: " + ledger_path;
    return code;
  }
  std::unique_ptr<GenerationHistoryStore> history;
  code = GenerationHistoryStore::open(options.process.data_directory + "/generation-history.pxgh", history, diagnostic);
  if (code != ResultCode::Ok) return code;
  // A previous generated process may have died while a task was running.  Its
  // persisted "生成中" state cannot be resumed safely without provider-side
  // idempotency, so fold it into a retryable failure before exposing the
  // session to the shell.
  std::vector<std::string> crashed;
  if (const ResultCode recovered = ledger->crash_generating(crashed); recovered != ResultCode::Ok) {
    diagnostic = "stale generation tasks could not be recovered";
    return recovered;
  }

  out.reset(new GenerationSession(session, std::move(options), std::move(log), std::move(ledger)));
  out->history_ = std::move(history);
  for (const auto& entry : out->history_->entries()) {
    const std::string ledger_status = out->ledger_->ledger().task_status(entry.task_id);
    if (!ledger_status.empty() && ledger_status != entry.status) {
      (void)out->history_->update_status(entry.task_id, ledger_status);
    }
  }
  diagnostic.clear();
  return ResultCode::Ok;
}

GenerationSession::~GenerationSession() { stop_process(); }

ResultCode GenerationSession::ensure_process(std::string& diagnostic, const std::atomic<bool>* cancel_requested) {
  if (process_ && client_.connected()) {
    if (process_->wait(0) < 0) return ResultCode::Ok;
    stop_process();
    std::string reload_diagnostic;
    if (const ResultCode reloaded = reload_ledger(reload_diagnostic); reloaded != ResultCode::Ok) {
      diagnostic = reload_diagnostic;
      return reloaded;
    }
    return recover_process_failure(diagnostic);
  }
  return GenerationProcess::start(options_.process, process_, client_, diagnostic, cancel_requested);
}

void GenerationSession::stop_process() {
  // The channel must close before the child owner is destroyed; otherwise the
  // child would wait for another request until the destructor timeout.
  client_ = generated::GenerationClient{};
  process_.reset();
}

ResultCode GenerationSession::reload_ledger(std::string& diagnostic) {
  ledger_.reset();
  ledger_log_.reset();
  ResultCode code = SqliteLedgerLog::open(options_.process.data_directory + "/ledger.db", ledger_log_);
  if (code == ResultCode::Ok) code = LedgerBook::open(options_.queue_policy, *ledger_log_, ledger_);
  if (code != ResultCode::Ok) {
    diagnostic = "generation ledger could not be reloaded";
    return code;
  }
  diagnostic.clear();
  return ResultCode::Ok;
}

ResultCode GenerationSession::recover_process_failure(std::string& diagnostic) {
  if (!ledger_) {
    diagnostic = "generation ledger is unavailable during process recovery";
    return ResultCode::StateConflict;
  }
  std::vector<std::string> crashed;
  const ResultCode code = ledger_->crash_generating(crashed);
  if (code != ResultCode::Ok) {
    diagnostic = "generation process failure could not be recorded";
    return code;
  }
  diagnostic.clear();
  return ResultCode::Ok;
}

ResultCode GenerationSession::submit(const generate::GenerationTask& task, const TaskQuote& quote,
                                     LayerDocument& document, const std::string& layer_document_path,
                                     ipc::GenerationProgressMessage& progress, int timeout_ms,
                                     std::string& diagnostic) {
  const ResultCode generated = generate(task, quote, progress, timeout_ms, diagnostic);
  if (generated != ResultCode::Ok) return generated;
  return place_result(task, progress, document, layer_document_path, diagnostic);
}

ResultCode GenerationSession::place_result(const generate::GenerationTask& task,
                                           const ipc::GenerationProgressMessage& progress,
                                           LayerDocument& document, const std::string& layer_document_path,
                                           std::string& diagnostic) {
  const ResultCode placed = GenerationResultApplier::apply(task, progress, document, files_, layer_document_path);
  const ResultCode confirmed = client_.confirm(GenerationResultApplier::confirmation(task, progress, placed), diagnostic);
  if (placed != ResultCode::Ok) {
    diagnostic = "generated result was rejected before layer placement";
    return placed;
  }
  return confirmed;
}

ResultCode GenerationSession::generate(const generate::GenerationTask& task, const TaskQuote& quote,
                                       ipc::GenerationProgressMessage& progress, int timeout_ms,
                                       std::string& diagnostic, const std::atomic<bool>* cancel_requested,
                                       VerifiedGenerationResult* verified) {
  progress = {};
  if (verified != nullptr) *verified = {};
  if (!session_.signed_in() || !ledger_ || quote.task_id != task.task_id || quote.tool_id != task.tool.tool_id) {
    diagnostic = "generation task or quote does not match the signed-in session";
    return ResultCode::StateConflict;
  }
  if (const ResultCode valid = generate::validate_task(task); valid != ResultCode::Ok) {
    diagnostic = "generation task is invalid";
    return valid;
  }
  if (cancel_requested != nullptr && cancel_requested->load(std::memory_order_acquire)) {
    diagnostic = "generation was cancelled before queueing";
    return ResultCode::Cancelled;
  }

  // The current generated child loads a ledger snapshot at startup. Finish
  // its previous session before queueing another task so the next child sees
  // the new quote and cannot use a stale event count.
  stop_process();

  BusinessCommands commands(session_);
  ResultCode code = commands.quote_generation(*ledger_, quote);
  if (code == ResultCode::Ok) code = commands.enqueue_generation(*ledger_, task.task_id);
  if (code != ResultCode::Ok) {
    diagnostic = "generation task could not be queued";
    return code;
  }
  if (history_ != nullptr && history_->record_task(task, "排队中") != ResultCode::Ok) {
    diagnostic = "generation history could not be saved";
    (void)ledger_->fail(task.task_id);
    return ResultCode::IoFailed;
  }
  code = ensure_process(diagnostic, cancel_requested);
  if (code != ResultCode::Ok) {
    // The task was already queued before the child was started.  Keep the
    // ledger terminal and retryable if the process cannot be reached.
    if (ledger_->ledger().task_status(task.task_id) == "排队中") {
      if (code == ResultCode::Cancelled) (void)ledger_->cancel(task.task_id);
      else (void)ledger_->fail(task.task_id);
    }
    if (history_ != nullptr) (void)history_->update_status(task.task_id, ledger_->ledger().task_status(task.task_id));
    return code;
  }

  code = client_.submit(task, progress, timeout_ms, diagnostic, cancel_requested);
  if (code == ResultCode::Ok) {
    if (progress.task_id != task.task_id) code = ResultCode::StateConflict;
    else code = progress.code;
  }
  if (code == ResultCode::Ok && verified != nullptr) {
    code = GenerationResultApplier::verify(task, progress, files_, *verified);
    if (code != ResultCode::Ok) diagnostic = "generated result failed verification";
  }
  if (code != ResultCode::Ok) {
    // The generated child may already have published a successful progress
    // message before the main process discovers a local verification failure.
    // Confirm that rejection while the channel is still alive so the child
    // can reclaim those result files instead of leaving orphaned pixels.
    if (progress.state == generate::TaskState::Succeeded && !progress.results.empty() && client_.connected()) {
      std::string reject_diagnostic;
      (void)client_.confirm(GenerationResultApplier::confirmation(task, progress, code), reject_diagnostic);
    }
    std::string cancel_diagnostic;
    (void)client_.cancel(task.task_id, cancel_diagnostic);
    // Close/reap before reopening the ledger. A late child response must not
    // be mistaken for the next task or append concurrently with recovery.
    stop_process();
  }
  // The child writes the same append-only ledger.  Refresh before the next
  // quote so a second task does not compare against a stale event count.
  std::string reload_diagnostic;
  const ResultCode reloaded = reload_ledger(reload_diagnostic);
  if (code == ResultCode::Ok && reloaded != ResultCode::Ok) {
    diagnostic = reload_diagnostic;
    return reloaded;
  }
  if (code != ResultCode::Ok && reloaded != ResultCode::Ok && diagnostic.empty()) diagnostic = reload_diagnostic;
  if (code != ResultCode::Ok && reloaded == ResultCode::Ok) {
    const auto status = ledger_->ledger().task_status(task.task_id);
    if (status == "排队中" || status == "生成中") {
      const ResultCode recovered = code == ResultCode::Cancelled ? ledger_->cancel(task.task_id) : ledger_->fail(task.task_id);
      if (recovered != ResultCode::Ok) {
        diagnostic = "generation terminal state could not be persisted";
        return recovered;
      }
    }
  }
  if (code != ResultCode::Ok) progress.results.clear();
  if (history_ != nullptr) {
    std::vector<GenerationHistoryResult> results;
    results.reserve(progress.results.size());
    for (const auto& result : progress.results) {
      results.push_back({result.ordinal, result.result_id, result.pixel_file, result.checksum,
                         result.width, result.height, result.continuation_parameters});
    }
    const std::string status = ledger_ == nullptr ? "失败" : ledger_->ledger().task_status(task.task_id);
    (void)history_->update_status(task.task_id, status.empty() ? "失败" : status, results);
  }
  return code;
}

ResultCode GenerationSession::place_result(const VerifiedGenerationResult& verified, LayerDocument& document,
                                           const std::string& layer_document_path, std::string& diagnostic) {
  const ResultCode placed = GenerationResultApplier::place_verified(verified, document, files_, layer_document_path);
  const auto confirmation = GenerationResultApplier::confirmation(verified.task(), verified.progress(), placed);
  const ResultCode confirmed = client_.confirm(confirmation, diagnostic);
  if (placed != ResultCode::Ok) {
    diagnostic = "generated result was rejected before layer placement";
    return placed;
  }
  return confirmed;
}

ResultCode GenerationSession::cancel(const std::string& task_id, std::string& diagnostic) {
  if (task_id.empty() || !client_.connected()) {
    diagnostic = "generation process is not connected";
    return ResultCode::StateConflict;
  }
  const ResultCode code = client_.cancel(task_id, diagnostic);
  std::string reload_diagnostic;
  const ResultCode reloaded = reload_ledger(reload_diagnostic);
  if (code == ResultCode::Ok && reloaded != ResultCode::Ok) {
    diagnostic = reload_diagnostic;
    return reloaded;
  }
  if (code == ResultCode::Ok && history_ != nullptr && ledger_ != nullptr) {
    const std::string status = ledger_->ledger().task_status(task_id);
    if (!status.empty()) (void)history_->update_status(task_id, status);
  }
  return code;
}

}  // namespace pixcake::generation_session
