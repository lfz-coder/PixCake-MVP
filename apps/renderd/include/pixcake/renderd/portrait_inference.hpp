#pragma once

#include "pixcake/renderd/face_detection.hpp"
#include "pixcake/renderd/mediapipe_inference.hpp"
#include "pixcake/renderd/onnx_model.hpp"
#include "pixcake/renderd/segmentation.hpp"
#include "pixcake/renderd/semantic_label_adapter.hpp"

#include <memory>
#include <string>
#include <vector>

namespace pixcake::renderd {

// BiSeNet face parsing (ONNX Runtime) on square face crops.  Each crop is the
// detected face box expanded by kBiSeNetCropExpand, resized to 512x512,
// ImageNet-normalised; per-class softmax probabilities are resized back and
// merged into full-image masks with max().
class BiSeNetFaceParser {
 public:
  static ResultCode create(const std::string& model_path, int threads, std::unique_ptr<BiSeNetFaceParser>& out,
                           std::string& diagnostic);

  // Produces one mask per label in bisenet_source_labels(), at the image
  // resolution.  With no faces every mask is present and all zero: the parts
  // are supported, the photo simply has none.
  ResultCode parse(const RgbaInput& input, const std::vector<FaceDetection>& faces,
                   std::vector<RawSemanticMask>& out, std::string& diagnostic);

 private:
  explicit BiSeNetFaceParser(std::unique_ptr<OnnxModel> model) : model_(std::move(model)) {}
  std::unique_ptr<OnnxModel> model_;
};

struct PortraitModelPaths {
  std::string selfie_multiclass;  // MediaPipe SelfieMulticlass 256x256 (.tflite)
  std::string face_detector;      // MediaPipe BlazeFace full range (.tflite)
  std::string face_parser;        // BiSeNet ResNet18 (.onnx)
};

// The project's portrait segmenter: MediaPipe for the whole person, BlazeFace
// to find faces, BiSeNet inside each face.  Parts and sources: portrait_rules().
class PortraitSegmenter final : public SemanticSegmenter {
 public:
  static ResultCode create(const PortraitModelPaths& paths, int threads, std::unique_ptr<PortraitSegmenter>& out,
                           std::string& diagnostic);

  ResultCode segment(const RgbaInput& input, SegmentationResult& out, std::string& diagnostic) override;

  // Faces found by the last successful segment() call.
  const std::vector<FaceDetection>& last_faces() const { return last_faces_; }

 private:
  PortraitSegmenter(std::unique_ptr<MediaPipeSelfieSegmenter> person, std::unique_ptr<BlazeFaceDetector> detector,
                    std::unique_ptr<BiSeNetFaceParser> parser)
      : person_(std::move(person)), detector_(std::move(detector)), parser_(std::move(parser)) {}
  std::unique_ptr<MediaPipeSelfieSegmenter> person_;
  std::unique_ptr<BlazeFaceDetector> detector_;
  std::unique_ptr<BiSeNetFaceParser> parser_;
  std::vector<FaceDetection> last_faces_;
};

// Model file names of the locked models (third_party/models.lock.cmake),
// resolved inside `model_directory`.
PortraitModelPaths portrait_model_paths(const std::string& model_directory);

// Where the models are: Contents/Resources/models inside an app bundle (next
// to Contents/MacOS/<this executable>), else `fallback` (the directory a
// development build downloaded and verified them into).  renderd and exportd.
std::string model_directory_near_executable(const std::string& fallback);

// The segmenter the renderd process hands to RenderService.
ResultCode create_portrait_segmenter(const std::string& model_directory, int threads,
                                     std::shared_ptr<SemanticSegmenter>& out, std::string& diagnostic);

}  // namespace pixcake::renderd
