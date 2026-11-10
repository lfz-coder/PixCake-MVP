#pragma once

#include "pixcake/generated/generation_channel.hpp"
#include "pixcake/generation_session/generation_result_applier.hpp"

#include <string>
#include <utility>

namespace pixcake::generation_session {

// Main-process orchestration for one generation request: submit to generated,
// verify and place every result, then send the idempotent confirmation back.
class GenerationFlow {
 public:
  GenerationFlow(generated::GenerationClient& client, FileSystem& files,
                 LayerDocument& document, std::string layer_document_path)
      : client_(client), files_(files), document_(document), layer_document_path_(std::move(layer_document_path)) {}

  ResultCode run(const generate::GenerationTask& task,
                 ipc::GenerationProgressMessage& progress,
                 int timeout_ms, std::string& diagnostic);

 private:
  generated::GenerationClient& client_;
  FileSystem& files_;
  LayerDocument& document_;
  std::string layer_document_path_;
};

}  // namespace pixcake::generation_session
