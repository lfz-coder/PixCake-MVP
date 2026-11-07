#include "pixcake/renderd/face_detection.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace pixcake::renderd {
namespace {

constexpr std::uint32_t kGrid = 48;  // 192 / stride 4

double sigmoid_clipped(float logit) {
  // MediaPipe clips raw scores to [-100, 100] before the sigmoid.
  const double clipped = std::clamp(static_cast<double>(logit), -100.0, 100.0);
  return 1.0 / (1.0 + std::exp(-clipped));
}

double iou(const FaceDetection& a, const FaceDetection& b) {
  const double ix = std::max(0.0, std::min(a.x1, b.x1) - std::max(a.x0, b.x0));
  const double iy = std::max(0.0, std::min(a.y1, b.y1) - std::max(a.y0, b.y0));
  const double inter = ix * iy;
  const double uni = (a.x1 - a.x0) * (a.y1 - a.y0) + (b.x1 - b.x0) * (b.y1 - b.y0) - inter;
  return uni > 0 ? inter / uni : 0.0;
}

}  // namespace

ResultCode compute_letterbox(std::uint32_t source_width, std::uint32_t source_height, std::uint32_t model_size,
                             Letterbox& out, std::string& diagnostic) {
  out = {};
  if (source_width == 0 || source_height == 0 || model_size == 0) {
    diagnostic = "letterbox needs a non-empty source and model size";
    return ResultCode::StateConflict;
  }
  const double scale = static_cast<double>(model_size) / std::max(source_width, source_height);
  auto fit = [&](std::uint32_t side) {
    const auto scaled = static_cast<std::uint32_t>(std::lround(side * scale));
    return std::clamp<std::uint32_t>(scaled, 1, model_size);
  };
  out.source_width = source_width;
  out.source_height = source_height;
  out.model_size = model_size;
  out.scale = scale;
  out.content_width = fit(source_width);
  out.content_height = fit(source_height);
  out.pad_x = (model_size - out.content_width) / 2;
  out.pad_y = (model_size - out.content_height) / 2;
  diagnostic.clear();
  return ResultCode::Ok;
}

const std::vector<AnchorCenter>& blazeface_full_range_anchors() {
  static const std::vector<AnchorCenter> anchors = [] {
    std::vector<AnchorCenter> list;
    list.reserve(kBlazeFaceFullRangeAnchors);
    for (std::uint32_t y = 0; y < kGrid; ++y) {
      for (std::uint32_t x = 0; x < kGrid; ++x) {
        list.push_back({(x + 0.5) / kGrid, (y + 0.5) / kGrid});
      }
    }
    return list;
  }();
  return anchors;
}

ResultCode decode_blazeface_full_range(const std::vector<float>& regressors, const std::vector<float>& scores,
                                       const Letterbox& letterbox, const BlazeFaceDecodeOptions& options,
                                       std::vector<FaceDetection>& out, std::string& diagnostic) {
  out.clear();
  if (regressors.size() != kBlazeFaceFullRangeAnchors * kBlazeFaceCoords ||
      scores.size() != kBlazeFaceFullRangeAnchors) {
    diagnostic = "face detector output does not match the BlazeFace full-range layout";
    return ResultCode::UnsupportedFormat;
  }
  if (letterbox.model_size != kBlazeFaceFullRangeInput || letterbox.scale <= 0) {
    diagnostic = "face detector letterbox does not match the model input";
    return ResultCode::StateConflict;
  }

  const auto& anchors = blazeface_full_range_anchors();
  const double size = kBlazeFaceFullRangeInput;
  auto to_source_x = [&](double model_px) { return (model_px - letterbox.pad_x) / letterbox.scale; };
  auto to_source_y = [&](double model_px) { return (model_px - letterbox.pad_y) / letterbox.scale; };

  std::vector<FaceDetection> candidates;
  for (std::size_t i = 0; i < kBlazeFaceFullRangeAnchors; ++i) {
    if (std::isnan(scores[i])) continue;
    const double score = sigmoid_clipped(scores[i]);
    if (score < options.min_score) continue;
    const float* r = regressors.data() + i * kBlazeFaceCoords;
    // Offsets are in model pixels relative to the anchor centre (fixed anchor size).
    const double cx = r[0] + anchors[i].x * size;
    const double cy = r[1] + anchors[i].y * size;
    const double w = r[2];
    const double h = r[3];
    if (!std::isfinite(cx) || !std::isfinite(cy) || !(w > 0) || !(h > 0) || !std::isfinite(w) ||
        !std::isfinite(h)) {
      continue;
    }
    FaceDetection face;
    face.score = score;
    face.x0 = to_source_x(cx - w / 2);
    face.y0 = to_source_y(cy - h / 2);
    face.x1 = to_source_x(cx + w / 2);
    face.y1 = to_source_y(cy + h / 2);
    for (std::size_t k = 0; k < face.keypoints.size(); ++k) {
      face.keypoints[k] = {to_source_x(r[4 + 2 * k] + anchors[i].x * size),
                           to_source_y(r[5 + 2 * k] + anchors[i].y * size)};
    }
    candidates.push_back(face);
  }
  std::stable_sort(candidates.begin(), candidates.end(),
                   [](const auto& a, const auto& b) { return a.score > b.score; });

  // Weighted NMS: every candidate overlapping the current best is blended into
  // it, weighted by score; the blended detection keeps the best score.
  while (!candidates.empty()) {
    const FaceDetection top = candidates.front();
    std::vector<FaceDetection> rest;
    FaceDetection merged = top;
    double weight = 0;
    merged.x0 = merged.y0 = merged.x1 = merged.y1 = 0;
    for (auto& kp : merged.keypoints) kp = {};
    for (const auto& candidate : candidates) {
      if (iou(top, candidate) > options.min_suppression_iou) {
        weight += candidate.score;
        merged.x0 += candidate.score * candidate.x0;
        merged.y0 += candidate.score * candidate.y0;
        merged.x1 += candidate.score * candidate.x1;
        merged.y1 += candidate.score * candidate.y1;
        for (std::size_t k = 0; k < merged.keypoints.size(); ++k) {
          merged.keypoints[k].x += candidate.score * candidate.keypoints[k].x;
          merged.keypoints[k].y += candidate.score * candidate.keypoints[k].y;
        }
      } else {
        rest.push_back(candidate);
      }
    }
    // Candidates have positive width and height, so `top` overlaps itself
    // (IoU 1) and weight is at least top.score > 0.
    merged.x0 /= weight;
    merged.y0 /= weight;
    merged.x1 /= weight;
    merged.y1 /= weight;
    for (auto& kp : merged.keypoints) {
      kp.x /= weight;
      kp.y /= weight;
    }
    out.push_back(merged);
    if (options.max_faces != 0 && out.size() >= options.max_faces) break;
    candidates = std::move(rest);
  }
  diagnostic.clear();
  return ResultCode::Ok;
}

PixelRect expanded_square_crop(const FaceDetection& face, double expand) {
  const double cx = (face.x0 + face.x1) / 2;
  const double cy = (face.y0 + face.y1) / 2;
  const double side = std::max(face.x1 - face.x0, face.y1 - face.y0) * expand;
  PixelRect rect;
  rect.x0 = std::llround(cx - side / 2);
  rect.y0 = std::llround(cy - side / 2);
  const std::int64_t s = std::llround(side);
  rect.x1 = rect.x0 + s;
  rect.y1 = rect.y0 + s;
  return rect;
}

ResultCode extract_square_crop_rgba(const RgbaInput& image, const PixelRect& rect, std::vector<std::uint8_t>& out,
                                    std::string& diagnostic) {
  out.clear();
  const std::int64_t side = rect.x1 - rect.x0;
  if (side <= 0 || rect.y1 - rect.y0 != side || side > kMaxFaceCropSide) {
    diagnostic = "face crop must be a non-empty square within the size limit";
    return ResultCode::StateConflict;
  }
  if (image.width == 0 || image.height == 0 || image.stride < static_cast<std::uint64_t>(image.width) * 4 ||
      static_cast<std::uint64_t>(image.stride) * image.height > image.bytes.size()) {
    diagnostic = "face crop source image is malformed";
    return ResultCode::StateConflict;
  }
  const std::int64_t x0 = std::max<std::int64_t>(rect.x0, 0);
  const std::int64_t y0 = std::max<std::int64_t>(rect.y0, 0);
  const std::int64_t x1 = std::min<std::int64_t>(rect.x1, image.width);
  const std::int64_t y1 = std::min<std::int64_t>(rect.y1, image.height);
  if (x0 >= x1 || y0 >= y1) {
    diagnostic = "face crop lies outside the image";
    return ResultCode::StateConflict;
  }
  const auto s = static_cast<std::size_t>(side);
  out.assign(s * s * 4, 0);
  const auto row_bytes = static_cast<std::size_t>(x1 - x0) * 4;
  for (std::int64_t y = y0; y < y1; ++y) {
    const std::uint8_t* src = image.bytes.data() + static_cast<std::size_t>(y) * image.stride +
                              static_cast<std::size_t>(x0) * 4;
    std::uint8_t* dst = out.data() + (static_cast<std::size_t>(y - rect.y0) * s +
                                      static_cast<std::size_t>(x0 - rect.x0)) * 4;
    std::memcpy(dst, src, row_bytes);
  }
  diagnostic.clear();
  return ResultCode::Ok;
}

void paste_square_max(const std::vector<std::uint8_t>& plane, const PixelRect& rect,
                      std::vector<std::uint8_t>& destination, std::uint32_t destination_width,
                      std::uint32_t destination_height, std::uint32_t destination_stride) {
  const std::int64_t side = rect.x1 - rect.x0;
  if (side <= 0 || plane.size() != static_cast<std::size_t>(side) * static_cast<std::size_t>(side) ||
      static_cast<std::uint64_t>(destination_stride) * destination_height > destination.size()) {
    return;
  }
  const std::int64_t x0 = std::max<std::int64_t>(rect.x0, 0);
  const std::int64_t y0 = std::max<std::int64_t>(rect.y0, 0);
  const std::int64_t x1 = std::min<std::int64_t>(rect.x0 + side, destination_width);
  const std::int64_t y1 = std::min<std::int64_t>(rect.y0 + side, destination_height);
  for (std::int64_t y = y0; y < y1; ++y) {
    const std::uint8_t* src = plane.data() + static_cast<std::size_t>(y - rect.y0) * static_cast<std::size_t>(side);
    std::uint8_t* dst = destination.data() + static_cast<std::size_t>(y) * destination_stride;
    for (std::int64_t x = x0; x < x1; ++x) {
      dst[x] = std::max(dst[x], src[x - rect.x0]);
    }
  }
}

}  // namespace pixcake::renderd
