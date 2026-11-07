// Integration test for the LiteRT runtime and the locked MediaPipe models.
// Built only with PIXCAKE_ENABLE_INFERENCE=ON on macOS (test images are decoded
// with the system ImageIO framework; production decoding is not involved).
// Reference values come from the official MediaPipe Python tasks
// (mediapipe 1.2.2) on the same file; see
// docs/superpowers/evidence/2026-11-06-segmentation-litert-integration.md.
#include "pixcake/renderd/mediapipe_inference.hpp"

#include "imageio_test_image.hpp"

#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

using namespace pixcake::renderd;
using pixcake::ResultCode;

const std::string kModels = PIXCAKE_MODEL_DIR;
const std::string kTestData = PIXCAKE_TESTDATA_DIR;

double iou(double ax0, double ay0, double ax1, double ay1, double bx0, double by0, double bx1, double by1) {
  const double ix = std::max(0.0, std::min(ax1, bx1) - std::max(ax0, bx0));
  const double iy = std::max(0.0, std::min(ay1, by1) - std::max(ay0, by0));
  const double inter = ix * iy;
  return inter / ((ax1 - ax0) * (ay1 - ay0) + (bx1 - bx0) * (by1 - by0) - inter);
}

const SemanticMask& mask_of(const SegmentationResult& result, SemanticPart part) {
  const auto it = std::find_if(result.masks.begin(), result.masks.end(),
                               [part](const auto& m) { return m.part == part; });
  assert(it != result.masks.end());
  return *it;
}

double share_above_half(const SemanticMask& mask) {
  std::size_t on = 0;
  for (std::uint32_t y = 0; y < mask.height; ++y) {
    for (std::uint32_t x = 0; x < mask.width; ++x) on += mask.alpha[y * mask.stride + x] > 127;
  }
  return static_cast<double>(on) / (static_cast<double>(mask.width) * mask.height);
}

double mean_alpha(const SemanticMask& mask, std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1) {
  double sum = 0;
  for (std::uint32_t y = y0; y < y1; ++y) {
    for (std::uint32_t x = x0; x < x1; ++x) sum += mask.alpha[y * mask.stride + x];
  }
  return sum / ((x1 - x0) * (y1 - y0));
}

double elapsed_ms(std::chrono::steady_clock::time_point since) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - since).count();
}

}  // namespace

// XNNPACK's kernels read up to XNN_EXTRA_BYTES past the input; an input whose
// last byte is the last readable byte of the address space (a guard page right
// after it) must still resize.  Seen as an intermittent SIGBUS in
// xnn_u8_ibilinear_ukernel__neon_u16 when a large mask or image buffer ended
// on a page boundary (crash reports of 2026-11-07).
void test_resize_reads_only_its_input() {
  const std::size_t page = static_cast<std::size_t>(::getpagesize());
  for (const std::uint32_t channels : {1u, 4u}) {
    const std::uint32_t width = 101;
    const std::uint32_t height = 37;
    const std::size_t bytes = static_cast<std::size_t>(width) * height * channels;
    const std::size_t span = (bytes + page - 1) / page * page;
    auto* base = static_cast<std::uint8_t*>(
        ::mmap(nullptr, span + page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    assert(base != MAP_FAILED);
    assert(::mprotect(base + span, page, PROT_NONE) == 0);  // the guard page
    std::uint8_t* source = base + span - bytes;              // ends exactly at the guard
    for (std::size_t i = 0; i < bytes; ++i) source[i] = static_cast<std::uint8_t>(i * 7);
    std::vector<std::uint8_t> out;
    std::string diagnostic;
    assert(pixcake::renderd::resize_bilinear_u8(source, width, height, channels, width * channels, 313, 211, out,
                                                diagnostic) == ResultCode::Ok);
    assert(out.size() == static_cast<std::size_t>(313) * 211 * channels);
    ::munmap(base, span + page);
  }
}

int main() {
  test_resize_reads_only_its_input();
  std::string diagnostic;
  const std::string segmenter_path = kModels + "/selfie_multiclass_256x256.tflite";
  const std::string detector_path = kModels + "/blaze_face_full_range.tflite";

  // Loading failures are reported, not crashed on.
  {
    std::unique_ptr<LiteRtModel> model;
    assert(LiteRtModel::load(kModels + "/does-not-exist.tflite", 1, model, diagnostic) == ResultCode::IoFailed);
    assert(!model && !diagnostic.empty());
    const std::string garbage = kTestData + "/not-a-model.tflite";
    std::ofstream(garbage, std::ios::binary) << "this is not a flatbuffer";
    assert(LiteRtModel::load(garbage, 1, model, diagnostic) == ResultCode::UnsupportedFormat);
    std::unique_ptr<MediaPipeSelfieSegmenter> wrong_segmenter;
    assert(MediaPipeSelfieSegmenter::create(detector_path, 1, wrong_segmenter, diagnostic) ==
           ResultCode::UnsupportedFormat);
    std::unique_ptr<BlazeFaceDetector> wrong_detector;
    assert(BlazeFaceDetector::create(segmenter_path, 1, wrong_detector, diagnostic) ==
           ResultCode::UnsupportedFormat);
    assert(LiteRtModel::load(detector_path, 1, model, diagnostic) == ResultCode::Ok);
    std::vector<std::vector<float>> outputs;
    assert(model->run(std::vector<float>(5), outputs, diagnostic) == ResultCode::StateConflict);
  }

  // XNNPACK resize: row padding in the source does not change the result.
  {
    const std::vector<std::uint8_t> packed = {0, 100, 200, 50, 150, 250};
    const std::vector<std::uint8_t> padded = {0, 100, 200, 9, 9, 50, 150, 250, 9, 9};
    std::vector<std::uint8_t> a, b;
    assert(resize_bilinear_u8(packed.data(), 3, 2, 1, 3, 6, 4, a, diagnostic) == ResultCode::Ok);
    assert(resize_bilinear_u8(padded.data(), 3, 2, 1, 5, 6, 4, b, diagnostic) == ResultCode::Ok);
    assert(a.size() == 24 && a == b);
    assert(a.front() == 0 && a.back() == 250);  // half-pixel centres keep the corners
    assert(resize_bilinear_u8(packed.data(), 3, 2, 1, 2, 6, 4, a, diagnostic) == ResultCode::StateConflict);
    assert(resize_bilinear_u8(packed.data(), 3, 2, 1, 3, 0, 4, a, diagnostic) == ResultCode::StateConflict);
  }

  const RgbaInput portrait = pixcake::test::load_rgba_with_imageio(kTestData + "/portrait.jpg");
  assert(portrait.width == 820 && portrait.height == 1024);

  // Face detection against the official result: one face, box (290, 110, 228x228), score 0.85.
  {
    std::unique_ptr<BlazeFaceDetector> detector;
    assert(BlazeFaceDetector::create(detector_path, 4, detector, diagnostic) == ResultCode::Ok);
    std::vector<FaceDetection> faces;
    const auto start = std::chrono::steady_clock::now();
    assert(detector->detect(portrait, {}, faces, diagnostic) == ResultCode::Ok);
    std::printf("face detection %.1f ms, %zu face(s)\n", elapsed_ms(start), faces.size());
    assert(faces.size() == 1);
    const auto& f = faces[0];
    std::printf("face box %.1f %.1f %.1f %.1f score %.3f\n", f.x0, f.y0, f.x1 - f.x0, f.y1 - f.y0, f.score);
    assert(iou(f.x0, f.y0, f.x1, f.y1, 290, 110, 290 + 228, 110 + 228) >= 0.85);
    assert(f.score > 0.75 && f.score < 0.95);
    // Keypoints: both eyes above the nose tip, mouth below it.
    assert(f.keypoints[0].y < f.keypoints[2].y && f.keypoints[1].y < f.keypoints[2].y);
    assert(f.keypoints[3].y > f.keypoints[2].y);

    // A blank image has no face.
    RgbaInput blank;
    blank.width = 640;
    blank.height = 480;
    blank.stride = 640 * 4;
    blank.bytes.assign(static_cast<std::size_t>(blank.stride) * blank.height, 128);
    assert(detector->detect(blank, {}, faces, diagnostic) == ResultCode::Ok);
    assert(faces.empty());
    RgbaInput broken = blank;
    broken.bytes.resize(10);
    assert(detector->detect(broken, {}, faces, diagnostic) == ResultCode::StateConflict);
  }

  // Segmentation against the official class shares on this image:
  // face-skin 0.0576, hair 0.0132, clothes 0.4148, background 0.4912.
  {
    std::unique_ptr<MediaPipeSelfieSegmenter> segmenter;
    assert(MediaPipeSelfieSegmenter::create(segmenter_path, 4, segmenter, diagnostic) == ResultCode::Ok);
    SegmentationResult result;
    const auto start = std::chrono::steady_clock::now();
    const ResultCode code = segmenter->segment(portrait, result, diagnostic);
    std::printf("segmentation %.1f ms\n", elapsed_ms(start));
    if (code != ResultCode::Ok) std::printf("%s\n", diagnostic.c_str());
    assert(code == ResultCode::Ok);
    assert(validate_segmentation_result(portrait, result, diagnostic) == ResultCode::Ok);
    assert(result.masks.size() == 5);
    const double face = share_above_half(mask_of(result, SemanticPart::Face));
    const double hair = share_above_half(mask_of(result, SemanticPart::Hair));
    const double clothes = share_above_half(mask_of(result, SemanticPart::AllClothes));
    const double person = share_above_half(mask_of(result, SemanticPart::AllPerson));
    std::printf("shares face %.4f hair %.4f clothes %.4f person %.4f\n", face, hair, clothes, person);
    assert(face > 0.0576 - 0.01 && face < 0.0576 + 0.01);
    assert(hair > 0.0132 - 0.006 && hair < 0.0132 + 0.006);
    assert(clothes > 0.4148 - 0.02 && clothes < 0.4148 + 0.02);
    assert(person > (1 - 0.4912) - 0.03 && person < (1 - 0.4912) + 0.03);
    // Inside the face box the face mask is on; in the top-left corner (flag, curtain) nothing is.
    assert(mean_alpha(mask_of(result, SemanticPart::Face), 350, 180, 460, 300) > 200);
    assert(mean_alpha(mask_of(result, SemanticPart::AllPerson), 0, 0, 60, 60) < 20);

    // Same pixels with padded rows give the same masks.
    RgbaInput padded;
    padded.source_format = portrait.source_format;
    padded.width = portrait.width;
    padded.height = portrait.height;
    padded.stride = portrait.stride + 64;
    padded.bytes.assign(static_cast<std::size_t>(padded.stride) * padded.height, 7);
    for (std::uint32_t y = 0; y < portrait.height; ++y) {
      std::copy_n(portrait.bytes.data() + y * portrait.stride, portrait.stride,
                  padded.bytes.data() + y * padded.stride);
    }
    SegmentationResult again;
    assert(segmenter->segment(padded, again, diagnostic) == ResultCode::Ok);
    assert(mask_of(again, SemanticPart::Face).alpha == mask_of(result, SemanticPart::Face).alpha);
  }
  return 0;
}
