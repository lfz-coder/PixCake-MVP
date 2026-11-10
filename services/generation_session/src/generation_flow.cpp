#include "pixcake/generation_session/generation_flow.hpp"

namespace pixcake::generation_session {

ResultCode GenerationFlow::run(const generate::GenerationTask& task,
                               ipc::GenerationProgressMessage& progress,
                               int timeout_ms, std::string& diagnostic) {
  const ResultCode submitted = client_.submit(task, progress, timeout_ms, diagnostic);
  if (submitted != ResultCode::Ok) return submitted;
  if (progress.code != ResultCode::Ok) return progress.code;

  const ResultCode placed = GenerationResultApplier::apply(task, progress, document_, files_, layer_document_path_);
  const auto confirmation = GenerationResultApplier::confirmation(task, progress, placed);
  const ResultCode confirmed = client_.confirm(confirmation, diagnostic);
  if (confirmed != ResultCode::Ok) return confirmed;
  if (placed != ResultCode::Ok) {
    diagnostic = "generated result was rejected before layer placement";
    return placed;
  }
  diagnostic.clear();
  return ResultCode::Ok;
}

}  // namespace pixcake::generation_session
