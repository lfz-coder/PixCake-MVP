#pragma once

#include "pixcake/generate/generation_task.hpp"
#include "pixcake/ipc/process_messages.hpp"
#include "pixcake/layerdoc/layer_document.hpp"
#include "pixcake/persist/file_system.hpp"

#include <string>

namespace pixcake::generation_session {

// Only the verifier can populate this receipt. It contains references and
// metadata, never image pixels, so moving it to the document thread is cheap.
class VerifiedGenerationResult {
 public:
  const generate::GenerationTask& task() const { return task_; }
  const ipc::GenerationProgressMessage& progress() const { return progress_; }

 private:
  friend class GenerationResultApplier;
  bool valid_ = false;
  generate::GenerationTask task_;
  ipc::GenerationProgressMessage progress_;
};

// Main-process owner of the result -> layer boundary.  It verifies every
// result before mutating the document, places results idempotently, and saves
// once.  The provider response itself never enters this class.
class GenerationResultApplier {
 public:
  static ResultCode verify(const generate::GenerationTask& task, const ipc::GenerationProgressMessage& progress,
                           FileSystem& files, VerifiedGenerationResult& out);
  static ResultCode place_verified(const VerifiedGenerationResult& verified, LayerDocument& document,
                                   FileSystem& files, const std::string& layer_document_path);
  static ResultCode apply(const generate::GenerationTask& task,
                          const ipc::GenerationProgressMessage& progress,
                          LayerDocument& document, FileSystem& files,
                          const std::string& layer_document_path);

  static ipc::GenerationResultConfirmMessage confirmation(
      const generate::GenerationTask& task,
      const ipc::GenerationProgressMessage& progress,
      ResultCode code);
};

}  // namespace pixcake::generation_session
