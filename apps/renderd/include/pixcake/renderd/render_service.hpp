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
  // True when the shaders drew the frame; false for the reference renderer.
  bool drawn_on_gpu = false;
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
  // operations draw on the GPU through an offscreen native context when one
  // can be created (architecture 9.3: the preview context lives in this
  // process), else -- and for work the shaders do not cover -- through the
  // reference renderer.  The two agree within one 8-bit step.
  PreviewResult render_latest();
  // Renders one frame now, outside the latest-request slot (thumbnails).  The
  // caller has checked identity; input larger than max_edge is refused.
  PreviewResult render_now(std::uint64_t sequence, const std::string& image_id,
                           const render::RenderDescription& description, const RgbaInput& input,
                           std::uint32_t max_edge);
  bool has_pending() const;

 private:
  static ResultCode validate_input(const RgbaInput&, std::string& diagnostic);
  static bool has_enabled_operations(const render::RenderDescription&);
  PreviewResult render_operations(std::uint64_t sequence, const std::string& image_id,
                                  const render::RenderDescription& description, const RgbaInput& input,
                                  std::uint32_t max_edge, PreviewResult result);

  // The offscreen GPU context, created on first use; false once it could not
  // be created or a GPU pass failed (the reference draws from then on).
  bool gpu_ready();

  mutable std::mutex mutex_;
  std::optional<PreviewWork> pending_;
  SemanticMaskProvider masks_;  // render thread only
  gl::Context gpu_context_;     // render thread only
  gpu::RenderExecutor gpu_executor_;  // keeps the context's programs and textures
  bool gpu_tried_ = false;
  bool gpu_ok_ = false;
};

}  // namespace pixcake::renderd
