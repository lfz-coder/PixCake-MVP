#pragma once

#include "pixcake/gpu/render_executor.hpp"
#include "pixcake/ipc/preview_protocol.hpp"
#include "pixcake/renderd/image_io.hpp"
#include "pixcake/renderd/segmentation.hpp"
#include "pixcake/renderd/semantic_mask_provider.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace pixcake::renderd {

struct PreviewWork {
  std::uint64_t client_sequence = 0;
  preview::PreviewRequest request;
  render::RenderDescription description;
  RgbaInput input;
};

struct PreviewResult {
  ResultCode code = ResultCode::Ok;
  std::uint64_t client_sequence = 0;
  ipc::PreviewFrameHeader header;
  std::vector<std::uint8_t> rgba;
  std::string diagnostic;
  // Per-operation outcome of the reference renderer (implemented, reference
  // only, skipped for a missing resource, unregistered, disabled) and the
  // reasons the shell shows for skipped work, e.g. an unavailable semantic part.
  std::vector<gpu::PassStatusRecord> statuses;
  std::vector<gpu::ExecutionDiagnostic> diagnostics;
};

// Process-side owner of preview work. It deliberately accepts already decoded
// RGBA input: file decoding and GPU resource lifetime belong to later adapters.
// At most one request is retained, so slider drags cannot create an unbounded
// queue of stale frames.
class RenderService {
 public:
  // `segmenter` supplies semantic masks ("semantic:<part>" mask ids); null when
  // inference is not built, in which case such operations are skipped with a reason.
  explicit RenderService(std::shared_ptr<SemanticSegmenter> segmenter = nullptr);

  ResultCode submit(PreviewWork work, std::string& diagnostic);
  // Cancel only the pending request that matches the client sequence and
  // document identity. A newer request or another editing instance is never
  // removed by an older cancellation message.
  // 取消消息的字段只在 kernel/ipc 定义一次（质量清单 Q-25）。
  ResultCode cancel(const ipc::PreviewCancelMessage& message, std::string& diagnostic);
  // Renders on the calling thread (one render thread per service).  Enabled
  // operations run through the deterministic reference renderer; renderd has
  // no GL context (architecture rule 5b).
  PreviewResult render_latest();
  bool has_pending() const;

 private:
  static ResultCode validate_input(const RgbaInput&, std::string& diagnostic);
  static bool has_enabled_operations(const render::RenderDescription&);
  PreviewResult render_operations(const PreviewWork& work, PreviewResult result);

  mutable std::mutex mutex_;
  std::optional<PreviewWork> pending_;
  SemanticMaskProvider masks_;  // render thread only
};

}  // namespace pixcake::renderd
