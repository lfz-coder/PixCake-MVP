#pragma once

#include "pixcake/capability/capability_registry.hpp"
#include "pixcake/editdoc/edit_document.hpp"
#include "pixcake/persist/file_system.hpp"
#include "pixcake/persist/write_lock.hpp"
#include "pixcake/support/fast_export_latch.hpp"
#include "pixcake/support/log_sink.hpp"
#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace pixcake {

struct UserCommand {
  std::optional<std::string> name;
  std::optional<std::string> target_id;
  std::optional<std::uint64_t> revision;
  std::string capability_id;
  std::string parameter_id;
  std::string display_value;
};

struct CommandResult {
  ResultCode code = ResultCode::Ok;
  std::uint64_t revision = 0;
};

class CommandGate {
 public:
  // pipeline 是开始编辑前要检查的能力管线，默认是阶段一管线。管线有环或重复时，所有命令都返回
  // StateConflict，并写一条 event 为 "command.pipeline.invalid" 的日志（契约 C-35、运维 O-04）。
  CommandGate(EditDocument& document, const FastExportLatch& latch, FileSystem& files, std::string path,
              std::vector<VersionLimit> limits, LogSink* log,
              const std::vector<PipelineStep>& pipeline = phase_one_pipeline());
  ~CommandGate();

  CommandResult submit(const UserCommand& command);

 private:
  EditDocument& document_;
  const FastExportLatch& latch_;
  FileSystem& files_;
  std::string path_;
  std::vector<VersionLimit> limits_;
  LogSink* log_ = nullptr;
  std::string pipeline_error_;
};

}  // namespace pixcake
