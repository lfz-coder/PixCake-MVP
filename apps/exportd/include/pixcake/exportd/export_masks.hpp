#pragma once

#include "pixcake/gpu/render_executor.hpp"
#include "pixcake/render/render_description.hpp"
#include "pixcake/renderd/image_io.hpp"
#include "pixcake/renderd/semantic_mask_provider.hpp"
#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace pixcake::exportd {

// The masks one export draws with, at the original's resolution.
struct ExportMasks {
  std::vector<gpu::RenderMask> masks;
  // Parts the model does not output: skipped, as in the preview.
  std::vector<gpu::ExecutionDiagnostic> skipped;
};

class ExportMaskSource {
 public:
  virtual ~ExportMaskSource() = default;
  // Masks referenced by `description` for the original at `source_path`,
  // `width` x `height` (its decoded, oriented size).  An error when the masks
  // cannot be computed at all (no segmentation built, segmentation failed):
  // the export then fails with that reason instead of dropping the effect.
  virtual ResultCode provide(const std::string& source_path, const render::RenderDescription& description,
                             std::uint32_t width, std::uint32_t height, ExportMasks& out,
                             std::string& diagnostic) = 0;
};

// Bilinear resize of one 8-bit plane (packed output).
using MaskResize = std::function<ResultCode(const std::uint8_t* source, std::uint32_t width, std::uint32_t height,
                                            std::uint32_t target_width, std::uint32_t target_height,
                                            std::vector<std::uint8_t>& out, std::string& diagnostic)>;

// Requirement 10.4 / architecture 10.4: the export uses the same semantic
// segmentation as the preview.  The original is decoded the way renderd
// decodes the preview source (longest edge kPreviewEdge, same decoder), the
// same segmenter runs on it -- so the parts and their masks are the preview's
// -- and each referenced mask is resized to the original's resolution.
class SemanticExportMasks final : public ExportMaskSource {
 public:
  // The preview's longest edge (PreviewSessionOptions::max_edge).
  static constexpr std::uint32_t kPreviewEdge = 4000;

  // Null `segmenter`: inference is not built; masked exports then fail.
  SemanticExportMasks(std::shared_ptr<renderd::SemanticSegmenter> segmenter,
                      std::shared_ptr<renderd::ImageDecoder> decoder, MaskResize resize);
  ResultCode provide(const std::string& source_path, const render::RenderDescription& description,
                     std::uint32_t width, std::uint32_t height, ExportMasks& out, std::string& diagnostic) override;

 private:
  std::shared_ptr<renderd::SemanticSegmenter> segmenter_;
  std::shared_ptr<renderd::ImageDecoder> decoder_;
  MaskResize resize_;
};

// The mask source of the exportd process: the portrait segmenter and XNNPACK
// resize when inference is built, else one that refuses masked exports.
std::shared_ptr<ExportMaskSource> make_system_export_masks(const std::string& model_directory, std::string& status);

}  // namespace pixcake::exportd
