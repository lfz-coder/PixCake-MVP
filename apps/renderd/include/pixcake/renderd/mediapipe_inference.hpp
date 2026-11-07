#pragma once

#include "pixcake/renderd/face_detection.hpp"
#include "pixcake/renderd/litert_model.hpp"
#include "pixcake/renderd/segmentation.hpp"
#include "pixcake/renderd/semantic_label_adapter.hpp"

#include <memory>
#include <string>
#include <vector>

namespace pixcake::renderd {

// MediaPipe SelfieMulticlass on LiteRT.  Produces per-label probability masks
// at the input resolution and maps them to project parts through
// mediapipe_selfie_multiclass_rules().  Pre/post-processing matches the
// official task: stretch to 256x256, RGB normalised to [-1, 1], softmax over
// the 6 channels (see the parity evidence).
class MediaPipeSelfieSegmenter final : public SemanticSegmenter {
 public:
  static ResultCode create(const std::string& model_path, int threads,
                           std::unique_ptr<MediaPipeSelfieSegmenter>& out, std::string& diagnostic);

  ResultCode segment(const RgbaInput& input, SegmentationResult& out, std::string& diagnostic) override;

  // Per-label probability masks at the input resolution (all labels except
  // background), before mapping to project parts.
  ResultCode raw_masks(const RgbaInput& input, std::vector<RawSemanticMask>& out, std::string& diagnostic);

 private:
  explicit MediaPipeSelfieSegmenter(std::unique_ptr<LiteRtModel> model) : model_(std::move(model)) {}
  std::unique_ptr<LiteRtModel> model_;
};

// MediaPipe BlazeFace (full range) on LiteRT: letterbox to 192x192, run, decode.
class BlazeFaceDetector {
 public:
  static ResultCode create(const std::string& model_path, int threads, std::unique_ptr<BlazeFaceDetector>& out,
                           std::string& diagnostic);

  ResultCode detect(const RgbaInput& input, const BlazeFaceDecodeOptions& options,
                    std::vector<FaceDetection>& out, std::string& diagnostic);

 private:
  BlazeFaceDetector(std::unique_ptr<LiteRtModel> model, std::size_t regressor_output, std::size_t score_output)
      : model_(std::move(model)), regressor_output_(regressor_output), score_output_(score_output) {}
  std::unique_ptr<LiteRtModel> model_;
  std::size_t regressor_output_ = 0;
  std::size_t score_output_ = 1;
};

}  // namespace pixcake::renderd
