#include "pixcake/command/command_gate.hpp"

namespace pixcake {

CommandGate::CommandGate(EditDocument& document, const FastExportLatch& latch, FileSystem& files,
                         std::string path, std::vector<VersionLimit> limits, LogSink* log)
    : document_(document),
      latch_(latch),
      files_(files),
      path_(std::move(path)),
      limits_(std::move(limits)),
      log_(log) {}

CommandResult CommandGate::submit(const UserCommand& command) {
  std::lock_guard<std::mutex> lock(mu_);
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
    return finish(document_.commit_preview(*command.revision, command.capability_id, command.parameter_id,
                                           limits_, files_, path_));
  }
  return finish(ResultCode::StateConflict);
}

}  // namespace pixcake
