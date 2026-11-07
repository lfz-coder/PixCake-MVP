#include "pixcake/renderd/mediapipe_inference.hpp"

#include "pixcake/renderd/segmentation_models.hpp"
#include "pixcake/renderd/semantic_label_adapter.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pixcake::renderd {
namespace {

bool valid_rgba(const RgbaInput& input) {
  const std::uint64_t row = static_cast<std::uint64_t>(input.width) * 4ull;
  return input.width != 0 && input.height != 0 && row <= std::numeric_limits<std::uint32_t>::max() &&
         input.stride >= row &&
         static_cast<std::uint64_t>(input.stride) * input.height <= input.bytes.size();
}

bool shape_is(const std::vector<int>& shape, std::initializer_list<int> expected) {
  return shape == std::vector<int>(expected);
}

// RGBA bytes -> RGB float in [-1, 1], as the MediaPipe models expect.
void normalise_rgb(const std::vector<std::uint8_t>& rgba, std::vector<float>& out) {
  const std::size_t pixels = rgba.size() / 4;
  out.resize(pixels * 3);
  for (std::size_t i = 0; i < pixels; ++i) {
    for (int c = 0; c < 3; ++c) out[i * 3 + c] = (static_cast<float>(rgba[i * 4 + c]) - 127.5f) / 127.5f;
  }
}

}  // namespace

ResultCode MediaPipeSelfieSegmenter::create(const std::string& model_path, int threads,
                                            std::unique_ptr<MediaPipeSelfieSegmenter>& out,
                                            std::string& diagnostic) {
  out.reset();
  std::unique_ptr<LiteRtModel> model;
  const ResultCode code = LiteRtModel::load(model_path, threads, model, diagnostic);
  if (code != ResultCode::Ok) return code;
  const int n = static_cast<int>(kMediaPipeSelfieMulticlassInput);
  const int labels = static_cast<int>(kMediaPipeSelfieMulticlassLabels.size());
  if (!shape_is(model->input_shape(), {1, n, n, 3}) || model->output_count() != 1 ||
      !shape_is(model->output_shape(0), {1, n, n, labels})) {
    diagnostic = "model is not MediaPipe SelfieMulticlass 256x256: " + model_path;
    return ResultCode::UnsupportedFormat;
  }
  out.reset(new MediaPipeSelfieSegmenter(std::move(model)));
  diagnostic.clear();
  return ResultCode::Ok;
}

ResultCode MediaPipeSelfieSegmenter::segment(const RgbaInput& input, SegmentationResult& out,
                                             std::string& diagnostic) {
  out = {};
  std::vector<RawSemanticMask> raw;
  const ResultCode code = raw_masks(input, raw, diagnostic);
  if (code != ResultCode::Ok) return code;
  return build_semantic_segmentation_result(input, kMediaPipeSelfieMulticlassModelId,
                                            kMediaPipeSelfieMulticlassModelVersion,
                                            mediapipe_selfie_multiclass_rules(), raw, out, diagnostic);
}

ResultCode MediaPipeSelfieSegmenter::raw_masks(const RgbaInput& input, std::vector<RawSemanticMask>& raw,
                                               std::string& diagnostic) {
  raw.clear();
  if (!valid_rgba(input)) {
    diagnostic = "segmenter received invalid RGBA input";
    return ResultCode::StateConflict;
  }
  const std::uint32_t n = kMediaPipeSelfieMulticlassInput;
  std::vector<std::uint8_t> small;
  ResultCode code = resize_bilinear_u8(input.bytes.data(), input.width, input.height, 4, input.stride, n, n, small,
                                       diagnostic);
  if (code != ResultCode::Ok) return code;
  std::vector<float> tensor;
  normalise_rgb(small, tensor);
  std::vector<std::vector<float>> outputs;
  code = model_->run(tensor, outputs, diagnostic);
  if (code != ResultCode::Ok) return code;

  // Softmax per pixel, then one 8-bit probability plane per label.
  const std::size_t labels = kMediaPipeSelfieMulticlassLabels.size();
  const std::size_t pixels = static_cast<std::size_t>(n) * n;
  std::vector<std::vector<std::uint8_t>> planes(labels, std::vector<std::uint8_t>(pixels));
  const std::vector<float>& logits = outputs.front();
  for (std::size_t p = 0; p < pixels; ++p) {
    const float* l = logits.data() + p * labels;
    const float peak = *std::max_element(l, l + labels);
    float sum = 0;
    float e[8];
    for (std::size_t c = 0; c < labels; ++c) sum += (e[c] = std::exp(l[c] - peak));
    for (std::size_t c = 0; c < labels; ++c) {
      planes[c][p] = static_cast<std::uint8_t>(std::lround(std::clamp(e[c] / sum, 0.0f, 1.0f) * 255.0f));
    }
  }

  // Background is not a source label of any project part; skip its upsampling.
  for (std::size_t c = 1; c < labels; ++c) {
    RawSemanticMask mask;
    mask.label = kMediaPipeSelfieMulticlassLabels[c];
    mask.width = input.width;
    mask.height = input.height;
    mask.stride = input.width;
    code = resize_bilinear_u8(planes[c].data(), n, n, 1, n, input.width, input.height, mask.alpha, diagnostic);
    if (code != ResultCode::Ok) {
      raw.clear();
      return code;
    }
    raw.push_back(std::move(mask));
  }
  diagnostic.clear();
  return ResultCode::Ok;
}

ResultCode BlazeFaceDetector::create(const std::string& model_path, int threads,
                                     std::unique_ptr<BlazeFaceDetector>& out, std::string& diagnostic) {
  out.reset();
  std::unique_ptr<LiteRtModel> model;
  const ResultCode code = LiteRtModel::load(model_path, threads, model, diagnostic);
  if (code != ResultCode::Ok) return code;
  const int n = static_cast<int>(kBlazeFaceFullRangeInput);
  const int anchors = static_cast<int>(kBlazeFaceFullRangeAnchors);
  if (!shape_is(model->input_shape(), {1, n, n, 3}) || model->output_count() != 2) {
    diagnostic = "model is not MediaPipe BlazeFace full range: " + model_path;
    return ResultCode::UnsupportedFormat;
  }
  // Identify the two outputs by shape rather than by position.
  std::size_t regressors = 2, scores = 2;
  for (std::size_t i = 0; i < 2; ++i) {
    if (shape_is(model->output_shape(i), {1, anchors, static_cast<int>(kBlazeFaceCoords)})) regressors = i;
    if (shape_is(model->output_shape(i), {1, anchors, 1})) scores = i;
  }
  if (regressors == 2 || scores == 2) {
    diagnostic = "model outputs do not match BlazeFace full range: " + model_path;
    return ResultCode::UnsupportedFormat;
  }
  out.reset(new BlazeFaceDetector(std::move(model), regressors, scores));
  diagnostic.clear();
  return ResultCode::Ok;
}

ResultCode BlazeFaceDetector::detect(const RgbaInput& input, const BlazeFaceDecodeOptions& options,
                                     std::vector<FaceDetection>& out, std::string& diagnostic) {
  out.clear();
  if (!valid_rgba(input)) {
    diagnostic = "face detector received invalid RGBA input";
    return ResultCode::StateConflict;
  }
  Letterbox box;
  ResultCode code = compute_letterbox(input.width, input.height, kBlazeFaceFullRangeInput, box, diagnostic);
  if (code != ResultCode::Ok) return code;
  std::vector<std::uint8_t> content;
  code = resize_bilinear_u8(input.bytes.data(), input.width, input.height, 4, input.stride, box.content_width,
                            box.content_height, content, diagnostic);
  if (code != ResultCode::Ok) return code;

  // Zero padding (black, i.e. -1 after normalisation) around the content.
  const std::uint32_t n = kBlazeFaceFullRangeInput;
  std::vector<std::uint8_t> canvas(static_cast<std::size_t>(n) * n * 4, 0);
  for (std::uint32_t y = 0; y < box.content_height; ++y) {
    std::copy_n(content.data() + static_cast<std::size_t>(y) * box.content_width * 4,
                static_cast<std::size_t>(box.content_width) * 4,
                canvas.data() + (static_cast<std::size_t>(y + box.pad_y) * n + box.pad_x) * 4);
  }
  std::vector<float> tensor;
  normalise_rgb(canvas, tensor);
  std::vector<std::vector<float>> outputs;
  code = model_->run(tensor, outputs, diagnostic);
  if (code != ResultCode::Ok) return code;
  return decode_blazeface_full_range(outputs[regressor_output_], outputs[score_output_], box, options, out,
                                     diagnostic);
}

}  // namespace pixcake::renderd
