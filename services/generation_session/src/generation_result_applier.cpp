#include "pixcake/generation_session/generation_result_applier.hpp"

#include "pixcake/support/checksum.hpp"

#include <cstdio>
#include <limits>
#include <set>
#include <utility>

namespace pixcake::generation_session {
namespace {

std::string hex(std::uint64_t value) {
  char buffer[17];
  std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(value));
  return buffer;
}

bool valid_text(const std::string& value, bool required) {
  return (!required && value.empty()) || (!value.empty() && value.find('\0') == std::string::npos);
}

ResultCode validate_progress(const generate::GenerationTask& task,
                             const ipc::GenerationProgressMessage& progress) {
  if (generate::validate_task(task) != ResultCode::Ok || progress.task_id != task.task_id ||
      progress.state != generate::TaskState::Succeeded || progress.code != ResultCode::Ok ||
      progress.results.size() != task.tool.output_count || progress.results.empty()) {
    return ResultCode::StateConflict;
  }
  std::set<std::string> result_ids;
  for (std::size_t index = 0; index < progress.results.size(); ++index) {
    const auto& result = progress.results[index];
    if (result.ordinal != index || !valid_text(result.result_id, true) || !valid_text(result.pixel_file, true) ||
        !valid_text(result.checksum, true) || result.width == 0 || result.height == 0 ||
        result.continuation_parameters.size() > 32 * 1024 || !result_ids.insert(result.result_id).second) {
      return ResultCode::StateConflict;
    }
  }
  return ResultCode::Ok;
}

}  // namespace

ResultCode GenerationResultApplier::apply(const generate::GenerationTask& task,
                                           const ipc::GenerationProgressMessage& progress,
                                           LayerDocument& document, FileSystem& files,
                                           const std::string& layer_document_path) {
  if (layer_document_path.empty()) return ResultCode::StateConflict;
  if (document.recovered_empty() || document.source_image_id() != task.image_id ||
      document.source_revision() != task.source_revision) {
    return ResultCode::StateConflict;
  }
  VerifiedGenerationResult verified;
  const ResultCode checked = verify(task, progress, files, verified);
  if (checked != ResultCode::Ok) return checked;
  return place_verified(verified, document, files, layer_document_path);
}

ResultCode GenerationResultApplier::verify(const generate::GenerationTask& task,
                                            const ipc::GenerationProgressMessage& progress, FileSystem& files,
                                            VerifiedGenerationResult& out) {
  out = {};
  if (const ResultCode valid = validate_progress(task, progress); valid != ResultCode::Ok) return valid;

  // Verify all files and checksums before changing the layer document.  A
  // duplicate or a later malformed result therefore cannot leave a half-placed
  // multi-output task.
  for (const auto& result : progress.results) {
    std::vector<std::uint8_t> bytes;
    const IoResult read = files.read_file(result.pixel_file, bytes);
    if (!read.ok || bytes.empty()) return read.not_found ? ResultCode::SourceMissing : read.code;
    const std::uint64_t expected = static_cast<std::uint64_t>(result.width) * result.height * 4u;
    if (expected != bytes.size() || hex(checksum_fnv1a64(bytes.data(), bytes.size())) != result.checksum) {
      return ResultCode::ImageCorrupt;
    }
  }

  out.task_ = task;
  out.progress_ = progress;
  out.valid_ = true;
  return ResultCode::Ok;
}

ResultCode GenerationResultApplier::place_verified(const VerifiedGenerationResult& verified, LayerDocument& document,
                                                    FileSystem& files, const std::string& layer_document_path) {
  const auto& task = verified.task_;
  const auto& progress = verified.progress_;
  if (!verified.valid_ || layer_document_path.empty() || document.recovered_empty() ||
      document.source_image_id() != task.image_id || document.source_revision() != task.source_revision) {
    return ResultCode::StateConflict;
  }

  LayerDocument staged = document;
  const std::string shape = generate::result_shape_name(task.tool.result_shape);
  for (const auto& result : progress.results) {
    if (const ResultCode placed = staged.place_result(result.result_id, shape, task.tool.tool_id,
                                                      result.continuation_parameters, result.pixel_file,
                                                      result.checksum, result.width, result.height);
        placed != ResultCode::Ok) {
      return placed;
    }
  }
  if (const ResultCode saved = staged.save(files, layer_document_path); saved != ResultCode::Ok) return saved;
  document = std::move(staged);
  return ResultCode::Ok;
}

ipc::GenerationResultConfirmMessage GenerationResultApplier::confirmation(
    const generate::GenerationTask& task, const ipc::GenerationProgressMessage& progress, ResultCode code) {
  ipc::GenerationResultConfirmMessage result;
  result.task_id = task.task_id;
  result.code = code;
  if (!progress.results.empty()) {
    result.result_id = progress.results.front().result_id;
    result.checksum = progress.results.front().checksum;
  }
  return result;
}

}  // namespace pixcake::generation_session
