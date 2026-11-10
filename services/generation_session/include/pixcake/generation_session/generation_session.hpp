#pragma once

#include "pixcake/command/business_commands.hpp"
#include "pixcake/generation_session/generation_flow.hpp"
#include "pixcake/generation_session/generation_history.hpp"
#include "pixcake/generation_session/generation_process.hpp"
#include "pixcake/ledger/ledger_book.hpp"
#include "pixcake/storage/sqlite_ledger_log.hpp"
#include "pixcake/support/session_state.hpp"

#include <memory>
#include <atomic>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace pixcake::generation_session {

// Configuration owned by the shell/controller.  The generated child only
// receives the paths and the fixture switch; quote and queue decisions remain
// in the main process.
struct GenerationSessionOptions {
  GenerationProcessOptions process;
  FreeQueuePolicy queue_policy;
  std::int64_t opening_points = 0;
};

class GenerationSession {
 public:
  static ResultCode open(const SessionState& session, GenerationSessionOptions options,
                         std::unique_ptr<GenerationSession>& out, std::string& diagnostic);
  ~GenerationSession();

  GenerationSession(const GenerationSession&) = delete;
  GenerationSession& operator=(const GenerationSession&) = delete;

  // Quotes and queues the task, then runs the complete submit -> verify ->
  // layer placement -> confirmation flow.  Repeating the same task id is
  // idempotent at the ledger boundary and result placement is idempotent by
  // result id.
  ResultCode submit(const generate::GenerationTask& task, const TaskQuote& quote,
                    LayerDocument& document, const std::string& layer_document_path,
                    ipc::GenerationProgressMessage& progress, int timeout_ms,
                    std::string& diagnostic);
  // Worker entry: queue and generate only; never writes a layer document.
  // The owner must revalidate the live source before calling place_result on
  // its document thread. No other session methods may run concurrently.
  ResultCode generate(const generate::GenerationTask& task, const TaskQuote& quote,
                      ipc::GenerationProgressMessage& progress, int timeout_ms, std::string& diagnostic,
                      const std::atomic<bool>* cancel_requested = nullptr, VerifiedGenerationResult* verified = nullptr);
  ResultCode place_result(const generate::GenerationTask& task, const ipc::GenerationProgressMessage& progress,
                          LayerDocument& document, const std::string& layer_document_path,
                          std::string& diagnostic);
  ResultCode place_result(const VerifiedGenerationResult& verified, LayerDocument& document,
                          const std::string& layer_document_path, std::string& diagnostic);
  ResultCode cancel(const std::string& task_id, std::string& diagnostic);

  const LedgerBook& ledger() const { return *ledger_; }
  bool connected() const { return client_.connected(); }
  std::int64_t creative_points() const { return ledger_ == nullptr ? 0 : ledger_->ledger().creative_points(); }
  std::vector<std::pair<std::string, std::string>> task_statuses() const {
    return ledger_ == nullptr ? std::vector<std::pair<std::string, std::string>>{}
                              : ledger_->ledger().task_statuses();
  }
  const std::vector<GenerationHistoryEntry>& history() const {
    static const std::vector<GenerationHistoryEntry> empty;
    return history_ == nullptr ? empty : history_->entries();
  }

 private:
  GenerationSession(const SessionState& session, GenerationSessionOptions options,
                    std::unique_ptr<SqliteLedgerLog> ledger_log,
                    std::unique_ptr<LedgerBook> ledger)
      : session_(session), options_(std::move(options)), ledger_log_(std::move(ledger_log)),
        ledger_(std::move(ledger)) {}

  ResultCode ensure_process(std::string& diagnostic, const std::atomic<bool>* cancel_requested = nullptr);
  ResultCode reload_ledger(std::string& diagnostic);
  ResultCode recover_process_failure(std::string& diagnostic);
  void stop_process();

  const SessionState& session_;
  GenerationSessionOptions options_;
  PosixFileSystem files_;
  std::unique_ptr<SqliteLedgerLog> ledger_log_;
  std::unique_ptr<LedgerBook> ledger_;
  std::unique_ptr<GenerationHistoryStore> history_;
  std::unique_ptr<GenerationProcess> process_;
  generated::GenerationClient client_;
};

}  // namespace pixcake::generation_session
