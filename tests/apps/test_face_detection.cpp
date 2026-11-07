#include "pixcake/renderd/face_detection.hpp"

#include <cassert>
#include <cmath>
#include <string>
#include <vector>

namespace {

using pixcake::ResultCode;
using namespace pixcake::renderd;

bool near(double a, double b, double tolerance = 1e-3) { return std::fabs(a - b) <= tolerance; }

struct Tensors {
  std::vector<float> regressors = std::vector<float>(kBlazeFaceFullRangeAnchors * kBlazeFaceCoords, 0.0f);
  std::vector<float> scores = std::vector<float>(kBlazeFaceFullRangeAnchors, -20.0f);

  // Places one raw prediction on the anchor of grid cell (x, y).
  void put(int x, int y, float dx, float dy, float w, float h, float logit) {
    const std::size_t anchor = static_cast<std::size_t>(y) * 48 + static_cast<std::size_t>(x);
    float* r = regressors.data() + anchor * kBlazeFaceCoords;
    r[0] = dx;
    r[1] = dy;
    r[2] = w;
    r[3] = h;
    for (int k = 0; k < 6; ++k) {
      r[4 + 2 * k] = static_cast<float>(k);  // keypoint k sits k model pixels right of the anchor
      r[5 + 2 * k] = 0.0f;
    }
    scores[anchor] = logit;
  }
};

Letterbox square192() {
  Letterbox box;
  std::string diagnostic;
  assert(compute_letterbox(192, 192, 192, box, diagnostic) == ResultCode::Ok);
  return box;
}

}  // namespace

int main() {
  std::string diagnostic;

  // Letterbox: keep aspect ratio, centre the content, pad the short side.
  {
    Letterbox box;
    assert(compute_letterbox(2000, 1333, 192, box, diagnostic) == ResultCode::Ok);
    assert(box.content_width == 192 && box.content_height == 128);
    assert(box.pad_x == 0 && box.pad_y == 32);
    assert(near(box.scale, 0.096));
    assert(compute_letterbox(1333, 2000, 192, box, diagnostic) == ResultCode::Ok);
    assert(box.content_width == 128 && box.content_height == 192 && box.pad_x == 32 && box.pad_y == 0);
    assert(compute_letterbox(0, 10, 192, box, diagnostic) == ResultCode::StateConflict);
    assert(compute_letterbox(10, 10, 0, box, diagnostic) == ResultCode::StateConflict);
    // A 1-pixel-high panorama still gets at least one content row.
    assert(compute_letterbox(5000, 1, 192, box, diagnostic) == ResultCode::Ok);
    assert(box.content_height == 1);
  }

  // Anchor layout of the full-range model: one anchor per cell of a 48x48 grid.
  {
    const auto& anchors = blazeface_full_range_anchors();
    assert(anchors.size() == kBlazeFaceFullRangeAnchors);
    assert(near(anchors[0].x, 0.5 / 48) && near(anchors[0].y, 0.5 / 48));
    assert(near(anchors[504].x, 24.5 / 48) && near(anchors[504].y, 10.5 / 48));
    assert(near(anchors.back().x, 47.5 / 48) && near(anchors.back().y, 47.5 / 48));
  }

  // One confident prediction decodes to the expected pixel box and keypoints.
  {
    Tensors t;
    t.put(24, 10, 0.0f, 0.0f, 48.0f, 48.0f, 2.0f);  // anchor centre (98, 42) model px
    std::vector<FaceDetection> faces;
    assert(decode_blazeface_full_range(t.regressors, t.scores, square192(), {}, faces, diagnostic) ==
           ResultCode::Ok);
    assert(faces.size() == 1);
    assert(near(faces[0].score, 1.0 / (1.0 + std::exp(-2.0))));
    assert(near(faces[0].x0, 74) && near(faces[0].y0, 18) && near(faces[0].x1, 122) && near(faces[0].y1, 66));
    assert(near(faces[0].keypoints[0].x, 98) && near(faces[0].keypoints[5].x, 103));
    assert(near(faces[0].keypoints[3].y, 42));
  }

  // Overlapping predictions are blended by score (weighted NMS); a distant one stays separate;
  // a low-score one is dropped.
  {
    Tensors t;
    t.put(24, 10, 0.0f, 0.0f, 48.0f, 48.0f, 2.0f);  // box x 74..122
    t.put(25, 10, 0.0f, 0.0f, 48.0f, 48.0f, 1.0f);  // anchor x 102 -> box x 78..126, IoU 0.846
    t.put(5, 40, 0.0f, 0.0f, 20.0f, 20.0f, 1.5f);    // far away
    t.put(40, 5, 0.0f, 0.0f, 20.0f, 20.0f, -1.0f);   // score 0.269 < 0.5
    std::vector<FaceDetection> faces;
    assert(decode_blazeface_full_range(t.regressors, t.scores, square192(), {}, faces, diagnostic) ==
           ResultCode::Ok);
    assert(faces.size() == 2);
    const double s1 = 1.0 / (1.0 + std::exp(-2.0));
    const double s2 = 1.0 / (1.0 + std::exp(-1.0));
    assert(near(faces[0].score, s1));  // the group keeps the best score
    assert(near(faces[0].x0, (s1 * 74 + s2 * 78) / (s1 + s2)));
    assert(near(faces[0].x1, (s1 * 122 + s2 * 126) / (s1 + s2)));
    assert(near(faces[1].score, 1.0 / (1.0 + std::exp(-1.5))));
    assert(faces[0].score >= faces[1].score);

    BlazeFaceDecodeOptions strict;
    strict.min_score = 0.85f;
    assert(decode_blazeface_full_range(t.regressors, t.scores, square192(), strict, faces, diagnostic) ==
           ResultCode::Ok);
    assert(faces.size() == 1);

    BlazeFaceDecodeOptions capped;
    capped.max_faces = 1;
    assert(decode_blazeface_full_range(t.regressors, t.scores, square192(), capped, faces, diagnostic) ==
           ResultCode::Ok);
    assert(faces.size() == 1 && near(faces[0].score, s1));
  }

  // Coordinates are mapped back through the letterbox to source pixels.
  {
    Letterbox box;
    assert(compute_letterbox(400, 200, 192, box, diagnostic) == ResultCode::Ok);  // scale 0.48, pad_y 48
    Tensors t;
    t.put(24, 10, 0.0f, 0.0f, 48.0f, 48.0f, 2.0f);  // model box 74..122 x 18..66
    std::vector<FaceDetection> faces;
    assert(decode_blazeface_full_range(t.regressors, t.scores, box, {}, faces, diagnostic) == ResultCode::Ok);
    assert(faces.size() == 1);
    assert(near(faces[0].x0, 74 / 0.48) && near(faces[0].x1, 122 / 0.48));
    assert(near(faces[0].y0, (18 - 48) / 0.48) && near(faces[0].y1, (66 - 48) / 0.48));
  }

  // Malformed tensors are rejected instead of being read out of bounds.
  {
    Tensors t;
    std::vector<FaceDetection> faces(3);
    t.scores.pop_back();
    assert(decode_blazeface_full_range(t.regressors, t.scores, square192(), {}, faces, diagnostic) ==
           ResultCode::UnsupportedFormat);
    assert(faces.empty());
    assert(!diagnostic.empty());
    Tensors u;
    u.regressors.resize(10);
    assert(decode_blazeface_full_range(u.regressors, u.scores, square192(), {}, faces, diagnostic) ==
           ResultCode::UnsupportedFormat);
    Letterbox wrong = square192();
    wrong.model_size = 128;
    assert(decode_blazeface_full_range(t.regressors, Tensors{}.scores, wrong, {}, faces, diagnostic) ==
           ResultCode::StateConflict);
    Tensors nan_scores;
    nan_scores.scores[7] = std::nanf("");
    assert(decode_blazeface_full_range(nan_scores.regressors, nan_scores.scores, square192(), {}, faces,
                                       diagnostic) == ResultCode::Ok);
    assert(faces.empty());
  }

  // Square crop around the face, expanded about its centre; it may extend past the image.
  {
    FaceDetection face;
    face.x0 = 74;
    face.y0 = 18;
    face.x1 = 122;
    face.y1 = 66;
    const PixelRect crop = expanded_square_crop(face, 2.0);
    assert(crop.x0 == 50 && crop.y0 == -6 && crop.x1 == 146 && crop.y1 == 90);
    face.x1 = 74 + 30;  // wider than tall: the longer side wins
    face.y1 = 18 + 60;
    const PixelRect tall = expanded_square_crop(face, 1.0);
    assert(tall.x1 - tall.x0 == 60 && tall.y1 - tall.y0 == 60);
  }

  // Square crop extraction: pixels outside the image are black; row padding is skipped.
  {
    RgbaInput image;
    image.width = 3;
    image.height = 2;
    image.stride = 16;  // 4 padding bytes per row
    image.bytes.assign(32, 0xEE);
    for (std::uint32_t y = 0; y < 2; ++y) {
      for (std::uint32_t x = 0; x < 3; ++x) {
        for (int c = 0; c < 4; ++c) image.bytes[y * 16 + x * 4 + c] = static_cast<std::uint8_t>(10 * (y * 3 + x) + c);
      }
    }
    std::vector<std::uint8_t> crop;
    assert(extract_square_crop_rgba(image, PixelRect{-1, -1, 2, 2}, crop, diagnostic) == ResultCode::Ok);
    assert(crop.size() == 3 * 3 * 4);
    auto px = [&](int x, int y) { return crop[(y * 3 + x) * 4]; };
    assert(px(0, 0) == 0 && crop[3] == 0);  // outside: black, alpha 0
    assert(px(1, 1) == 0 && crop[(1 * 3 + 1) * 4 + 3] == 3);  // source (0,0)
    assert(px(2, 1) == 10 && px(1, 2) == 30 && px(2, 2) == 40);
    assert(extract_square_crop_rgba(image, PixelRect{0, 0, 2, 3}, crop, diagnostic) == ResultCode::StateConflict);
    assert(extract_square_crop_rgba(image, PixelRect{5, 5, 5, 5}, crop, diagnostic) == ResultCode::StateConflict);
    assert(extract_square_crop_rgba(image, PixelRect{0, 0, 20000, 20000}, crop, diagnostic) ==
           ResultCode::StateConflict);  // larger than the limit

    // Paste back: element-wise max, clipped to the destination.
    std::vector<std::uint8_t> plane = {1, 2, 3, 4, 5, 6, 7, 8, 9};  // 3x3
    std::vector<std::uint8_t> dest = {0, 0, 0, 100, 0, 0};          // 3x2, stride 3
    paste_square_max(plane, PixelRect{-1, -1, 2, 2}, dest, 3, 2, 3);
    assert(dest == (std::vector<std::uint8_t>{5, 6, 0, 100, 9, 0}));
    paste_square_max(plane, PixelRect{10, 10, 13, 13}, dest, 3, 2, 3);  // fully outside: unchanged
    assert(dest == (std::vector<std::uint8_t>{5, 6, 0, 100, 9, 0}));
  }
  return 0;
}
