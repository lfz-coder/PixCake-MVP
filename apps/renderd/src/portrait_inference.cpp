#include "pixcake/renderd/portrait_inference.hpp"

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include <filesystem>
#include <system_error>

#include "pixcake/renderd/litert_model.hpp"
#include "pixcake/renderd/segmentation_models.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pixcake::renderd {
namespace {

constexpr float kImageNetMean[3] = {0.485f, 0.456f, 0.406f};
constexpr float kImageNetStd[3] = {0.229f, 0.224f, 0.225f};

std::size_t label_index(const std::string& label) {
  const auto it = std::find_if(kBiSeNetLabels.begin(), kBiSeNetLabels.end(),
                               [&](const char* l) { return label == l; });
  return static_cast<std::size_t>(it - kBiSeNetLabels.begin());
}

}  // namespace

ResultCode BiSeNetFaceParser::create(const std::string& model_path, int threads,
                                     std::unique_ptr<BiSeNetFaceParser>& out, std::string& diagnostic) {
  out.reset();
  std::unique_ptr<OnnxModel> model;
  const ResultCode code = OnnxModel::load(model_path, threads, model, diagnostic);
  if (code != ResultCode::Ok) return code;
  const auto& shape = model->input_shape();
  const auto n = static_cast<std::int64_t>(kBiSeNetInput);
  if (shape.size() != 4 || shape[1] != 3 || shape[2] != n || shape[3] != n) {
    diagnostic = "model is not BiSeNet face parsing with a 3x512x512 input: " + model_path;
    return ResultCode::UnsupportedFormat;
  }
  out.reset(new BiSeNetFaceParser(std::move(model)));
  diagnostic.clear();
  return ResultCode::Ok;
}

ResultCode BiSeNetFaceParser::parse(const RgbaInput& input, const std::vector<FaceDetection>& faces,
                                    std::vector<RawSemanticMask>& out, std::string& diagnostic) {
  out.clear();
  if (input.width == 0 || input.height == 0 || input.stride < static_cast<std::uint64_t>(input.width) * 4 ||
      static_cast<std::uint64_t>(input.stride) * input.height > input.bytes.size()) {
    diagnostic = "face parser received invalid RGBA input";
    return ResultCode::StateConflict;
  }
  const auto wanted = bisenet_source_labels();
  std::vector<std::size_t> channels;
  for (const auto& label : wanted) {
    channels.push_back(label_index(label));
    RawSemanticMask mask;
    mask.label = label;
    mask.width = input.width;
    mask.height = input.height;
    mask.stride = input.width;
    mask.alpha.assign(static_cast<std::size_t>(input.width) * input.height, 0);
    out.push_back(std::move(mask));
  }

  const std::uint32_t n = kBiSeNetInput;
  const std::size_t plane_size = static_cast<std::size_t>(n) * n;
  const std::size_t classes = kBiSeNetLabels.size();
  auto fail = [&](ResultCode code) {
    out.clear();
    return code;
  };
  for (const auto& face : faces) {
    const PixelRect rect = expanded_square_crop(face, kBiSeNetCropExpand);
    std::vector<std::uint8_t> crop;
    ResultCode code = extract_square_crop_rgba(input, rect, crop, diagnostic);
    if (code != ResultCode::Ok) return fail(code);
    const auto side = static_cast<std::uint32_t>(rect.x1 - rect.x0);
    std::vector<std::uint8_t> small;
    code = resize_bilinear_u8(crop.data(), side, side, 4, static_cast<std::size_t>(side) * 4, n, n, small,
                              diagnostic);
    if (code != ResultCode::Ok) return fail(code);

    // NCHW, RGB in [0,1] then ImageNet mean/std (as the upstream onnx_inference.py).
    std::vector<float> tensor(3 * plane_size);
    for (std::size_t p = 0; p < plane_size; ++p) {
      for (std::size_t c = 0; c < 3; ++c) {
        tensor[c * plane_size + p] = (small[p * 4 + c] / 255.0f - kImageNetMean[c]) / kImageNetStd[c];
      }
    }
    std::vector<float> logits;
    std::vector<std::int64_t> logits_shape;
    code = model_->run(tensor, {1, 3, n, n}, logits, logits_shape, diagnostic);
    if (code != ResultCode::Ok) return fail(code);
    if (logits_shape != std::vector<std::int64_t>{1, static_cast<std::int64_t>(classes), n, n}) {
      diagnostic = "face parser output is not 19x512x512";
      return fail(ResultCode::UnsupportedFormat);
    }

    // Softmax over the 19 classes, kept only for the labels we map.
    std::vector<std::vector<std::uint8_t>> planes(channels.size(), std::vector<std::uint8_t>(plane_size));
    for (std::size_t p = 0; p < plane_size; ++p) {
      float peak = -std::numeric_limits<float>::infinity();
      for (std::size_t c = 0; c < classes; ++c) peak = std::max(peak, logits[c * plane_size + p]);
      float sum = 0;
      for (std::size_t c = 0; c < classes; ++c) sum += std::exp(logits[c * plane_size + p] - peak);
      for (std::size_t i = 0; i < channels.size(); ++i) {
        const float prob = std::exp(logits[channels[i] * plane_size + p] - peak) / sum;
        planes[i][p] = static_cast<std::uint8_t>(std::lround(std::clamp(prob, 0.0f, 1.0f) * 255.0f));
      }
    }
    for (std::size_t i = 0; i < channels.size(); ++i) {
      std::vector<std::uint8_t> back;
      code = resize_bilinear_u8(planes[i].data(), n, n, 1, n, side, side, back, diagnostic);
      if (code != ResultCode::Ok) return fail(code);
      paste_square_max(back, rect, out[i].alpha, input.width, input.height, input.width);
    }
  }
  diagnostic.clear();
  return ResultCode::Ok;
}

ResultCode PortraitSegmenter::create(const PortraitModelPaths& paths, int threads,
                                     std::unique_ptr<PortraitSegmenter>& out, std::string& diagnostic) {
  out.reset();
  std::unique_ptr<MediaPipeSelfieSegmenter> person;
  std::unique_ptr<BlazeFaceDetector> detector;
  std::unique_ptr<BiSeNetFaceParser> parser;
  ResultCode code = MediaPipeSelfieSegmenter::create(paths.selfie_multiclass, threads, person, diagnostic);
  if (code != ResultCode::Ok) return code;
  code = BlazeFaceDetector::create(paths.face_detector, threads, detector, diagnostic);
  if (code != ResultCode::Ok) return code;
  code = BiSeNetFaceParser::create(paths.face_parser, threads, parser, diagnostic);
  if (code != ResultCode::Ok) return code;
  out.reset(new PortraitSegmenter(std::move(person), std::move(detector), std::move(parser)));
  diagnostic.clear();
  return ResultCode::Ok;
}

ResultCode PortraitSegmenter::segment(const RgbaInput& input, SegmentationResult& out, std::string& diagnostic) {
  out = {};
  last_faces_.clear();
  std::vector<RawSemanticMask> raw;
  ResultCode code = person_->raw_masks(input, raw, diagnostic);
  if (code != ResultCode::Ok) return code;
  std::vector<FaceDetection> faces;
  code = detector_->detect(input, {}, faces, diagnostic);
  if (code != ResultCode::Ok) return code;
  std::vector<RawSemanticMask> face_parts;
  code = parser_->parse(input, faces, face_parts, diagnostic);
  if (code != ResultCode::Ok) return code;
  for (auto& mask : face_parts) raw.push_back(std::move(mask));
  code = build_semantic_segmentation_result(input, kPortraitModelId, kPortraitModelVersion, portrait_rules(), raw,
                                            out, diagnostic);
  if (code == ResultCode::Ok) last_faces_ = std::move(faces);
  return code;
}

PortraitModelPaths portrait_model_paths(const std::string& model_directory) {
  return {model_directory + "/" PIXCAKE_MODEL_FILE_SELFIE_MULTICLASS,
          model_directory + "/" PIXCAKE_MODEL_FILE_BLAZEFACE_FULL_RANGE,
          model_directory + "/" PIXCAKE_MODEL_FILE_BISENET_RESNET18};
}

ResultCode create_portrait_segmenter(const std::string& model_directory, int threads,
                                     std::shared_ptr<SemanticSegmenter>& out, std::string& diagnostic) {
  out.reset();
  std::unique_ptr<PortraitSegmenter> segmenter;
  const ResultCode code = PortraitSegmenter::create(portrait_model_paths(model_directory), threads, segmenter,
                                                    diagnostic);
  if (code == ResultCode::Ok) out = std::move(segmenter);
  return code;
}

std::string model_directory_near_executable(const std::string& fallback) {
#ifdef __APPLE__
  char buffer[4096];
  std::uint32_t size = sizeof(buffer);
  if (_NSGetExecutablePath(buffer, &size) == 0) {
    std::error_code error;
    const auto executable = std::filesystem::weakly_canonical(buffer, error);
    if (!error) {
      const auto bundled = executable.parent_path().parent_path() / "Resources" / "models";
      if (std::filesystem::is_directory(bundled, error)) return bundled.string();
    }
  }
#endif
  return fallback;
}

}  // namespace pixcake::renderd
