#pragma once

#include "pixcake/renderd/segmentation.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pixcake::renderd {

// A model adapter owns the raw masks and their original labels.  This type is
// deliberately independent of any third-party runtime so a future ONNX,
// CoreML, or other process-side adapter can use the same validation path.
struct RawSemanticMask {
  std::string label;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t stride = 0;
  std::vector<std::uint8_t> alpha;
};

// One explicit mapping from a project part to the model's original labels.
// A derived part is the per-pixel maximum of all listed source labels.  No
// mapping is inferred from label spelling or from a nearby semantic part.
struct SemanticLabelRule {
  SemanticPart part = SemanticPart::AllPerson;
  SupportStatus status = SupportStatus::Unsupported;
  std::vector<std::string> source_labels;
  std::string diagnostic;
};

// Builds and validates a project-facing segmentation result from raw model
// labels.  Unsupported parts are retained as explicit capabilities and never
// receive an alpha mask.  A supported/derived rule whose source label is
// absent fails instead of silently substituting another label.
ResultCode build_semantic_segmentation_result(
    const RgbaInput& input, const std::string& model_id, const std::string& model_version,
    const std::vector<SemanticLabelRule>& rules, const std::vector<RawSemanticMask>& raw_masks,
    SegmentationResult& out, std::string& diagnostic);

}  // namespace pixcake::renderd
