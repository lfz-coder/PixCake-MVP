#pragma once

#include "pixcake/renderd/image_io.hpp"
#include "pixcake/support/result_code.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pixcake::renderd {

// Post-processing for MediaPipe BlazeFace (full range).  The model itself runs
// in the LiteRT adapter; this part is runtime-independent so it can be tested
// without a model.  The decoding follows the official MediaPipe task graph
// (SSD anchors, sigmoid scores, weighted non-maximum suppression); see
// docs/superpowers/evidence/2026-11-06-segmentation-face-detection.md for the
// parity check against the official implementation.

inline constexpr std::uint32_t kBlazeFaceFullRangeInput = 192;
inline constexpr std::size_t kBlazeFaceFullRangeAnchors = 2304;  // 48 x 48 grid, stride 4
inline constexpr std::size_t kBlazeFaceCoords = 16;              // box (4) + 6 keypoints (12)

struct FacePoint {
  double x = 0;
  double y = 0;
};

// Coordinates are source-image pixels.  Boxes are not clamped to the image:
// a face at the border legitimately extends past it.
struct FaceDetection {
  double score = 0;
  double x0 = 0;
  double y0 = 0;
  double x1 = 0;
  double y1 = 0;
  // right eye, left eye, nose tip, mouth centre, right ear tragion, left ear tragion
  std::array<FacePoint, 6> keypoints{};
};

// Aspect-preserving fit of the source image into a square model input, with
// the content centred and the remainder zero-padded.
struct Letterbox {
  std::uint32_t source_width = 0;
  std::uint32_t source_height = 0;
  std::uint32_t model_size = 0;
  std::uint32_t content_width = 0;
  std::uint32_t content_height = 0;
  std::uint32_t pad_x = 0;
  std::uint32_t pad_y = 0;
  double scale = 0;  // model pixels per source pixel
};

ResultCode compute_letterbox(std::uint32_t source_width, std::uint32_t source_height, std::uint32_t model_size,
                             Letterbox& out, std::string& diagnostic);

struct AnchorCenter {
  double x = 0;  // normalised [0, 1]
  double y = 0;
};

const std::vector<AnchorCenter>& blazeface_full_range_anchors();

struct BlazeFaceDecodeOptions {
  float min_score = 0.5f;            // MediaPipe default min_detection_confidence
  float min_suppression_iou = 0.3f;  // MediaPipe default min_suppression_threshold
  std::size_t max_faces = 0;         // 0 = no limit
};

// regressors: [2304 x 16] float, scores: [2304] raw logits, both in model order.
// Results are sorted by score, highest first.
ResultCode decode_blazeface_full_range(const std::vector<float>& regressors, const std::vector<float>& scores,
                                       const Letterbox& letterbox, const BlazeFaceDecodeOptions& options,
                                       std::vector<FaceDetection>& out, std::string& diagnostic);

struct PixelRect {
  std::int64_t x0 = 0;
  std::int64_t y0 = 0;
  std::int64_t x1 = 0;  // exclusive
  std::int64_t y1 = 0;  // exclusive
};

// Square region centred on the face whose side is the longer box side times
// `expand`.  Used to feed face-parsing models; it may extend past the image.
PixelRect expanded_square_crop(const FaceDetection& face, double expand);

// Largest face crop side accepted (pixels).  A larger crop means a face bigger
// than the photo itself; it is rejected rather than allocated.
inline constexpr std::int64_t kMaxFaceCropSide = 16384;

// Copies a square region of `image` into a packed RGBA buffer of side x side
// pixels; pixels outside the image are transparent black.  The rect must be
// square, non-empty, no larger than kMaxFaceCropSide, and overlap the image.
ResultCode extract_square_crop_rgba(const RgbaInput& image, const PixelRect& rect, std::vector<std::uint8_t>& out,
                                    std::string& diagnostic);

// Writes max(destination, plane) for the part of a square plane (placed at
// `rect`, side = rect width) that lies inside the destination.
void paste_square_max(const std::vector<std::uint8_t>& plane, const PixelRect& rect,
                      std::vector<std::uint8_t>& destination, std::uint32_t destination_width,
                      std::uint32_t destination_height, std::uint32_t destination_stride);

}  // namespace pixcake::renderd
