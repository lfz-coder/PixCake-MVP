#include "pixcake/exportd/export_masks.hpp"

#ifdef PIXCAKE_HAS_INFERENCE
#include "pixcake/renderd/litert_model.hpp"
#include "pixcake/renderd/portrait_inference.hpp"
#endif

#include <algorithm>
#include <utility>

namespace pixcake::exportd {
namespace {

// What renderd accepts for a preview source (preview_server.cpp).
renderd::DecodeLimits preview_decode_limits() { return {30000, 30000, 300'000'000, 4000ull * 4000ull * 4ull}; }

}  // namespace

SemanticExportMasks::SemanticExportMasks(std::shared_ptr<renderd::SemanticSegmenter> segmenter,
                                         std::shared_ptr<renderd::ImageDecoder> decoder, MaskResize resize)
    : segmenter_(std::move(segmenter)), decoder_(std::move(decoder)), resize_(std::move(resize)) {}

ResultCode SemanticExportMasks::provide(const std::string& source_path, const render::RenderDescription& description,
                                        std::uint32_t width, std::uint32_t height, ExportMasks& out,
                                        std::string& diagnostic) {
  out = {};
  if (renderd::SemanticMaskProvider::referenced_semantic_ids(description).empty()) return ResultCode::Ok;
  if (!segmenter_) {
    diagnostic = "语义分割未构建，带局部调整的照片还不能导出";
    return ResultCode::UnsupportedFormat;
  }
  if (!decoder_ || !resize_) {
    diagnostic = "导出蒙版需要的解码或缩放没有接入";
    return ResultCode::UnsupportedFormat;
  }

  // The preview source, decoded exactly as renderd decodes it.
  renderd::DecodeRequest request;
  request.source_path = source_path;
  request.limits = preview_decode_limits();
  request.max_edge = kPreviewEdge;
  renderd::RgbaInput preview;
  std::string why;
  ResultCode code = decoder_->decode(request, preview, why);
  if (code != ResultCode::Ok) {
    diagnostic = "原图按预览尺寸解码失败：" + why;
    return code;
  }
  // Same orientation and aspect as the full-resolution decode (both apply EXIF
  // orientation): otherwise the masks would land on the wrong pixels.
  const auto cross = [](std::uint64_t a, std::uint64_t b) { return a * b; };
  const std::uint64_t lhs = cross(preview.width, height);
  const std::uint64_t rhs = cross(preview.height, width);
  if (preview.width > width || preview.height > height ||
      (lhs > rhs ? lhs - rhs : rhs - lhs) > std::max<std::uint64_t>(width, height)) {
    diagnostic = "预览尺寸的原图与全分辨率原图方向或比例不一致";
    return ResultCode::StateConflict;
  }

  // A provider per export: its cache holds every part of one image, and an
  // export never segments the same image twice in a row.
  renderd::SemanticMaskProvider provider(segmenter_);
  renderd::SemanticMaskBatch batch;
  provider.provide(description.image_id, preview, description, batch);
  if (batch.segmentation_failed) {
    diagnostic = batch.diagnostics.empty() ? "语义分割失败" : batch.diagnostics.front().reason;
    return ResultCode::UnsupportedFormat;
  }
  out.skipped = std::move(batch.diagnostics);
  for (auto& mask : batch.masks) {
    if (mask.width != width || mask.height != height) {
      std::vector<std::uint8_t> scaled;
      code = resize_(mask.alpha.data(), mask.width, mask.height, width, height, scaled, why);
      if (code != ResultCode::Ok) {
        diagnostic = "蒙版放大到原图尺寸失败：" + why;
        return code;
      }
      mask.alpha = std::move(scaled);
      mask.width = width;
      mask.height = height;
    }
    out.masks.push_back(std::move(mask));
  }
  diagnostic.clear();
  return ResultCode::Ok;
}

std::shared_ptr<ExportMaskSource> make_system_export_masks(const std::string& model_directory, std::string& status) {
  std::shared_ptr<renderd::ImageDecoder> decoder(renderd::make_system_image_decoder());
#ifdef PIXCAKE_HAS_INFERENCE
  std::shared_ptr<renderd::SemanticSegmenter> segmenter;
  std::string diagnostic;
  const ResultCode code = renderd::create_portrait_segmenter(model_directory, 4, segmenter, diagnostic);
  status = code == ResultCode::Ok ? "semantic segmentation available; models " + model_directory
                                  : std::string("semantic segmentation unavailable (") + result_code_label(code) +
                                        "): " + diagnostic;
  MaskResize resize = [](const std::uint8_t* source, std::uint32_t width, std::uint32_t height,
                         std::uint32_t target_width, std::uint32_t target_height, std::vector<std::uint8_t>& out,
                         std::string& why) {
    return renderd::resize_bilinear_u8(source, width, height, 1, width, target_width, target_height, out, why);
  };
  return std::make_shared<SemanticExportMasks>(code == ResultCode::Ok ? segmenter : nullptr, decoder, resize);
#else
  (void)model_directory;
  status = "semantic segmentation unavailable: inference not built (PIXCAKE_ENABLE_INFERENCE=OFF)";
  return std::make_shared<SemanticExportMasks>(nullptr, decoder, MaskResize{});
#endif
}

}  // namespace pixcake::exportd
