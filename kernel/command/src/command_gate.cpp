#include "pixcake/command/command_gate.hpp"

#include <unordered_map>

namespace {
std::mutex g_registry_mu;
std::unordered_map<std::string, std::uint64_t> g_document_revisions;
}

namespace pixcake {

CommandGate::CommandGate(EditDocument& document, const FastExportLatch& latch, FileSystem& files,
                         std::string path, std::vector<VersionLimit> limits, LogSink* log)
    : document_(document),
      latch_(latch),
      files_(files),
      path_(std::move(path)),
      limits_(std::move(limits)),
      log_(log) {
  register_fast_export_latch(path_, &latch_);
  std::lock_guard<std::mutex> lock(g_registry_mu);
  if (g_document_revisions.find(path_) == g_document_revisions.end()) {
    g_document_revisions[path_] = document_.revision();
  } else if (document_.revision() == 0) {
    // A fresh in-memory document may reuse a test or project path after the
    // previous owner was destroyed.  Start a new generation in that case.
    g_document_revisions[path_] = 0;
  }
}

CommandGate::~CommandGate() { unregister_fast_export_latch(path_, &latch_); }

CommandResult CommandGate::submit(const UserCommand& command) {
  const auto finish = [&](ResultCode code) {
    CommandResult result;
    result.code = code;
    result.revision = document_.revision();
    if (log_ != nullptr) {
      LogRecord record;
      record.code = code;
      record.message = result_code_label(code);
      record.path = path_;
      record.revision = result.revision;
      record.capability_id = command.capability_id;
      log_->write(record);
    }
    return result;
  };

  if (!command.name || command.name->empty() || !command.target_id || command.target_id->empty() ||
      !command.revision) {
    return finish(ResultCode::StateConflict);
  }
  const bool changes_document = *command.name == "preview" || *command.name == "commit";
  if (changes_document && latch_.enabled()) {
    return finish(ResultCode::FastExportBusy);
  }
  WriteScope scope(files_, path_, &latch_);
  if (!scope) {
    return finish(scope.code());
  }
  {
    std::lock_guard<std::mutex> registry_lock(g_registry_mu);
    const auto found = g_document_revisions.find(path_);
    if (found != g_document_revisions.end() && found->second != document_.revision()) {
      CommandResult result;
      result.code = ResultCode::StateConflict;
      result.revision = document_.revision();
      return result;
    }
  }
  if (*command.target_id != document_.image_id()) {
    return finish(ResultCode::StateConflict);
  }
  if (*command.revision != document_.revision()) {
    return finish(ResultCode::StateConflict);
  }
  if (*command.name == "preview") {
    return finish(document_.set_preview(command.capability_id, command.parameter_id, command.display_value));
  }
  if (*command.name == "commit") {
    const auto result = finish(document_.commit_preview(*command.revision, command.capability_id,
                                                        command.parameter_id, limits_, files_, path_));
    if (result.code == ResultCode::Ok) {
      std::lock_guard<std::mutex> registry_lock(g_registry_mu);
      g_document_revisions[path_] = document_.revision();
    }
    return result;
  }
  return finish(ResultCode::StateConflict);
}

}  // namespace pixcake
