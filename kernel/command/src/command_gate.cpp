#include "pixcake/command/command_gate.hpp"

#include "pixcake/capabilities/module_set.hpp"

#include <algorithm>

#include <unordered_map>

namespace {
std::mutex g_registry_mu;
std::unordered_map<std::string, std::uint64_t> g_document_revisions;
}

namespace pixcake {

CommandGate::CommandGate(EditDocument& document, const FastExportLatch& latch, FileSystem& files,
                         std::string path, std::vector<VersionLimit> limits, LogSink* log,
                         const std::vector<PipelineStep>& pipeline)
    : document_(document),
      latch_(latch),
      files_(files),
      path_(std::move(path)),
      limits_(std::move(limits)),
      log_(log) {
  if (!validate_phase_one_pipeline(pipeline, &pipeline_error_) && pipeline_error_.empty()) {
    pipeline_error_ = "能力管线无效";
  }
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
  if (session_ != nullptr && !session_->signed_in()) {
    return finish(ResultCode::StateConflict);
  }
  // C-35：管线有环或重复时拒绝开始编辑。不碰文档、不写盘，留一条能筛出来的记录（O-04）。
  if (!pipeline_error_.empty()) {
    LogRecord record;
    record.module = "command";
    record.event = "command.pipeline.invalid";
    record.code = ResultCode::StateConflict;
    record.message = pipeline_error_;
    record.path = path_;
    record.document_id = path_;
    record.capability_id = command.capability_id;
    write_log(log_, record);
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
      {
        std::lock_guard<std::mutex> registry_lock(g_registry_mu);
        g_document_revisions[path_] = document_.revision();
      }
      if (listener_ != nullptr) listener_->committed(document_.image_id(), document_.revision());
    }
    return result;
  }
  return finish(ResultCode::StateConflict);
}

CommandResult CommandGate::run_document_command(const char* name, std::uint64_t revision,
                                                const std::function<ResultCode()>& change) {
  const auto finish = [&](ResultCode code) {
    CommandResult result;
    result.code = code;
    result.revision = document_.revision();
    LogRecord record;
    record.module = "command";
    record.event = std::string("command.") + name;
    record.code = code;
    record.message = result_code_label(code);
    record.path = path_;
    record.revision = result.revision;
    write_log(log_, record);
    return result;
  };
  if (session_ != nullptr && !session_->signed_in()) return finish(ResultCode::StateConflict);
  if (!pipeline_error_.empty()) return finish(ResultCode::StateConflict);
  if (latch_.enabled()) return finish(ResultCode::FastExportBusy);
  WriteScope scope(files_, path_, &latch_);
  if (!scope) return finish(scope.code());
  {
    std::lock_guard<std::mutex> registry_lock(g_registry_mu);
    const auto found = g_document_revisions.find(path_);
    if (found != g_document_revisions.end() && found->second != document_.revision()) {
      return finish(ResultCode::StateConflict);
    }
  }
  if (revision != document_.revision()) return finish(ResultCode::StateConflict);
  const std::uint64_t before = document_.revision();
  const ResultCode code = change();
  const CommandResult result = finish(code);
  if (code == ResultCode::Ok && document_.revision() != before) {
    {
      std::lock_guard<std::mutex> registry_lock(g_registry_mu);
      g_document_revisions[path_] = document_.revision();
    }
    if (listener_ != nullptr) listener_->committed(document_.image_id(), document_.revision());
  }
  return result;
}

CommandResult CommandGate::apply_preset(PresetLibrary& library, const Preset& preset, std::uint64_t revision) {
  return run_document_command("preset.apply", revision, [&] {
    return library.apply(document_, preset, revision, files_, path_, latch_);
  });
}

CommandResult CommandGate::select_history(HistoryLog& history, std::size_t index, std::uint64_t revision) {
  return run_document_command("history.select", revision, [&] {
    EditDocument before = document_;
    ResultCode code = history.select(document_.image_id(), document_, index, false);
    if (code == ResultCode::Ok) code = document_.write_snapshot(files_, path_, &latch_);
    if (code != ResultCode::Ok) document_ = std::move(before);
    return code;
  });
}

CommandResult CommandGate::paste(const EffectClipboard& clipboard, std::uint64_t revision) {
  return run_document_command("paste", revision, [&] {
    return paste_committed(document_, clipboard, revision, files_, path_, &latch_);
  });
}

CommandResult CommandGate::commit_parameters(const std::string& capability_id,
                                             const std::vector<std::pair<std::string, std::string>>& values,
                                             std::uint64_t revision) {
  return run_document_command("commit.batch", revision, [&] {
    std::vector<std::string> ids;
    for (const auto& [id, value] : values) {
      const ResultCode previewed = document_.set_preview(capability_id, id, value);
      if (previewed != ResultCode::Ok) return previewed;
      ids.push_back(id);
    }
    return document_.commit_parameters(revision, capability_id, ids, limits_, files_, path_);
  });
}

CommandResult CommandGate::remove_local_mask(int instance, std::uint64_t revision) {
  return run_document_command("local.remove_mask", revision, [&] {
    if (instance < 0 || instance > kMaxLocalInstances) return ResultCode::StateConflict;
    return document_.commit_parameter_prefix_removal(revision, "color.local", local_instance_prefix(instance), files_,
                                                     path_);
  });
}

CommandResult CommandGate::sync_from(const EditDocument& source, const std::string& source_path,
                                     const std::vector<std::string>& checked_ids, std::uint64_t revision) {
  return run_document_command("sync", revision, [&] {
    SyncTarget target;
    target.id = document_.image_id();
    target.document = &document_;
    target.expected_revision = revision;
    target.files = &files_;
    target.path = path_;
    target.latch = &latch_;
    const auto results = apply_checked(source, source_path, checked_ids, {target});
    return results.empty() ? ResultCode::StateConflict : results.front().code;
  });
}

CommandResult CommandGate::apply_layers(const LayerDocument& layers, std::uint64_t revision) {
  return run_document_command("apply_layers", revision, [&] {
    return apply_to_original(layers, document_, revision, files_, path_);
  });
}

CommandResult CommandGate::write_match_result(const std::vector<MatchWrite>& writes, std::uint64_t revision) {
  return run_document_command("match.writeback", revision, [&] {
    const auto& targets = match_writeback_switch().targets;
    std::vector<FragmentView> replacements;
    std::vector<std::string> removals;
    for (const auto& write : writes) {
      if (std::find(targets.begin(), targets.end(), write.capability_id) == targets.end()) {
        return ResultCode::StateConflict;
      }
      const CapabilityModule* module = find_capability_module(write.capability_id);
      if (module == nullptr) return ResultCode::StateConflict;
      std::optional<FragmentBytes> existing;
      if (const auto found = document_.fragment(write.capability_id)) {
        existing = FragmentBytes{found->version, found->inner};
        removals.push_back(write.capability_id);
      }
      const FragmentResult written = module->write_back(existing, write.values, WriteMode::MatchWriteback);
      if (written.code != ResultCode::Ok) return written.code;
      replacements.push_back(FragmentView{write.capability_id, written.fragment.version, written.fragment.inner});
    }
    if (replacements.empty()) return ResultCode::StateConflict;
    return document_.commit_fragment_replacement(revision, removals, replacements, files_, path_);
  });
}

}  // namespace pixcake
