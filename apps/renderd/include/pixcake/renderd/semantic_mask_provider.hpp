#pragma once

#include "pixcake/gpu/render_executor.hpp"
#include "pixcake/render/render_description.hpp"
#include "pixcake/renderd/image_io.hpp"
#include "pixcake/renderd/segmentation.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pixcake::renderd {

struct SemanticMaskBatch {
  std::vector<gpu::RenderMask> masks;
  std::vector<gpu::ExecutionDiagnostic> diagnostics;
  // No segmenter is built, or segmentation itself failed: every referenced
  // part is missing for that reason (as opposed to a part the model does not
  // output).  Preview skips the operations either way; export refuses.
  bool segmentation_failed = false;
};

// Supplies the "semantic:<part>" masks a render description refers to, for the
// image being rendered (the preview frame here; exportd will use the same
// rules at full resolution).  Requirement 10.4: a part the model does not
// output stays unavailable and is never replaced by another part.
//
// The last segmentation is cached per image content, so slider drags on the
// same preview frame segment it once.  Not thread-safe: owned by the single
// render thread of RenderService.
class SemanticMaskProvider {
 public:
  // `segmenter` may be null when inference is not built; every semantic mask
  // is then reported as unavailable.
  explicit SemanticMaskProvider(std::shared_ptr<SemanticSegmenter> segmenter = nullptr);

  // Semantic mask ids referenced by enabled operations, de-duplicated, in order.
  static std::vector<std::string> referenced_semantic_ids(const render::RenderDescription& description);

  // Never fails the frame: segmentation errors and unavailable parts become
  // non-blocking diagnostics, and the renderer skips the affected operations.
  void provide(const std::string& image_id, const RgbaInput& input, const render::RenderDescription& description,
               SemanticMaskBatch& out);

  std::size_t segmentation_runs() const { return runs_; }

 private:
  struct Cached {
    std::string image_id;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t content_hash = 0;
    SegmentationResult result;
  };

  std::shared_ptr<SemanticSegmenter> segmenter_;
  std::optional<Cached> cached_;
  std::size_t runs_ = 0;
};

}  // namespace pixcake::renderd
