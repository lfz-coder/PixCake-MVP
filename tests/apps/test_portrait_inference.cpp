// Integration test for ONNX Runtime + BiSeNet face parsing and the combined
// portrait segmenter.  Built only with PIXCAKE_ENABLE_INFERENCE=ON on macOS.
// Reference shares come from the Python pipeline in
// docs/superpowers/evidence/scripts/2026-11-06-segmentation/bisenet_reference.py
// on the same fixture (official MediaPipe face box, same crop, softmax > 0.5).
#include "pixcake/renderd/portrait_inference.hpp"
#include "pixcake/renderd/render_service.hpp"
#include "pixcake/renderd/segmentation_models.hpp"

#include "imageio_test_image.hpp"

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

int alpha_at(const SemanticMask& mask, double x, double y) {
  return mask.alpha[static_cast<std::size_t>(y) * mask.stride + static_cast<std::size_t>(x)];
}

int min_alpha_near(const SemanticMask& mask, double cx, double cy, int radius) {
  int lowest = 255;
  for (int dy = -radius; dy <= radius; ++dy) {
    for (int dx = -radius; dx <= radius; ++dx) {
      const double x = cx + dx, y = cy + dy;
      if (x < 0 || y < 0 || x >= mask.width || y >= mask.height) continue;
      lowest = std::min(lowest, alpha_at(mask, x, y));
    }
  }
  return lowest;
}

bool near(double value, double expected, double tolerance) {
  return value >= expected - tolerance && value <= expected + tolerance;
}

}  // namespace

int main() {
  std::string diagnostic;
  const std::string parser_path = kModels + "/bisenet_resnet18.onnx";
  PortraitModelPaths paths{kModels + "/selfie_multiclass_256x256.tflite", kModels + "/blaze_face_full_range.tflite",
                           parser_path};

  // ONNX Runtime loading and input checks.
  {
    std::unique_ptr<OnnxModel> model;
    assert(OnnxModel::load(kModels + "/missing.onnx", 1, model, diagnostic) == ResultCode::IoFailed);
    const std::string garbage = kTestData + "/not-a-model.onnx";
    std::ofstream(garbage, std::ios::binary) << "not a protobuf";
    assert(OnnxModel::load(garbage, 1, model, diagnostic) == ResultCode::UnsupportedFormat);
    assert(!model && !diagnostic.empty());
    std::unique_ptr<BiSeNetFaceParser> wrong;
    assert(BiSeNetFaceParser::create(paths.selfie_multiclass, 1, wrong, diagnostic) == ResultCode::UnsupportedFormat);
    assert(OnnxModel::load(parser_path, 1, model, diagnostic) == ResultCode::Ok);
    assert((model->input_shape() == std::vector<std::int64_t>{-1, 3, 512, 512}));
    std::vector<float> output;
    std::vector<std::int64_t> output_shape;
    assert(model->run(std::vector<float>(10), {1, 3, 512, 512}, output, output_shape, diagnostic) ==
           ResultCode::StateConflict);
    assert(model->run(std::vector<float>(3 * 4 * 4), {1, 3, 4, 4}, output, output_shape, diagnostic) ==
           ResultCode::StateConflict);  // fixed 512x512 dimensions
    std::unique_ptr<PortraitSegmenter> segmenter;
    PortraitModelPaths broken = paths;
    broken.face_parser = garbage;
    assert(PortraitSegmenter::create(broken, 1, segmenter, diagnostic) == ResultCode::UnsupportedFormat);
  }

  const RgbaInput portrait = pixcake::test::load_rgba_with_imageio(kTestData + "/portrait.jpg");

  // No faces: every face-parsing mask exists and is empty.
  {
    std::unique_ptr<BiSeNetFaceParser> parser;
    assert(BiSeNetFaceParser::create(parser_path, 4, parser, diagnostic) == ResultCode::Ok);
    std::vector<RawSemanticMask> masks;
    assert(parser->parse(portrait, {}, masks, diagnostic) == ResultCode::Ok);
    assert(masks.size() == bisenet_source_labels().size());
    for ([[maybe_unused]] const auto& mask : masks) {
      assert(mask.width == portrait.width && mask.height == portrait.height);
      assert(std::all_of(mask.alpha.begin(), mask.alpha.end(), [](auto a) { return a == 0; }));
    }
    // A face so large its crop exceeds the limit is refused, not allocated.
    FaceDetection giant;
    giant.x0 = 0;
    giant.y0 = 0;
    giant.x1 = static_cast<double>(kMaxFaceCropSide);
    giant.y1 = static_cast<double>(kMaxFaceCropSide);
    assert(parser->parse(portrait, {giant}, masks, diagnostic) == ResultCode::StateConflict);
    assert(masks.empty());
  }

  // The combined portrait segmenter on the fixture.
  {
    std::unique_ptr<PortraitSegmenter> segmenter;
    assert(PortraitSegmenter::create(paths, 4, segmenter, diagnostic) == ResultCode::Ok);
    SegmentationResult result;
    const auto start = std::chrono::steady_clock::now();
    const ResultCode code = segmenter->segment(portrait, result, diagnostic);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    if (code != ResultCode::Ok) std::printf("%s\n", diagnostic.c_str());
    assert(code == ResultCode::Ok);
    std::printf("portrait segmentation %.1f ms\n", ms);
    assert(validate_segmentation_result(portrait, result, diagnostic) == ResultCode::Ok);
    assert(result.model_id == std::string(kPortraitModelId));
    assert(result.masks.size() == 10);
    assert(segmenter->last_faces().size() == 1);

    const double face_skin = share_above_half(mask_of(result, SemanticPart::FaceSkin));
    const double eyebrow = share_above_half(mask_of(result, SemanticPart::Eyebrow));
    const double lips = share_above_half(mask_of(result, SemanticPart::Lips));
    const double ear = share_above_half(mask_of(result, SemanticPart::Ear));
    const double all_skin = share_above_half(mask_of(result, SemanticPart::AllSkin));
    std::printf("shares face_skin %.5f eyebrow %.5f lips %.5f ear %.5f all_skin %.5f\n", face_skin, eyebrow, lips,
                ear, all_skin);
    // Python reference: face_skin 0.0472, eyebrow 0.00154, lips 0.00179, ear 0.00395.
    assert(near(face_skin, 0.0472, 0.004));
    assert(near(eyebrow, 0.00154, 0.0008));
    assert(near(lips, 0.00179, 0.0008));
    assert(near(ear, 0.00395, 0.0015));
    assert(all_skin > face_skin + 0.01);  // also hands and neck from MediaPipe body-skin

    const auto& face = segmenter->last_faces().front();
    const auto& skin = mask_of(result, SemanticPart::FaceSkin);
    // Nose tip is face skin (nose included).  BlazeFace eye keypoints are
    // approximate (on this fixture one sits on the lower lid, ~10 px below the
    // eye), so the eye check looks for the carved-out eye within 15 px.
    assert(alpha_at(skin, face.keypoints[2].x, face.keypoints[2].y) > 200);
    for (int eye = 0; eye < 2; ++eye) {
      assert(min_alpha_near(skin, face.keypoints[eye].x, face.keypoints[eye].y, 15) < 100);
    }
    // Nothing outside the face crop.
    assert(alpha_at(skin, 10, 10) == 0 && alpha_at(skin, portrait.width - 10, portrait.height - 10) == 0);

    // A blank image: no faces, all ten parts still present, face parts empty.
    RgbaInput blank;
    blank.width = 320;
    blank.height = 240;
    blank.stride = 320 * 4;
    blank.bytes.assign(static_cast<std::size_t>(blank.stride) * blank.height, 90);
    assert(segmenter->segment(blank, result, diagnostic) == ResultCode::Ok);
    assert(segmenter->last_faces().empty());
    assert(result.masks.size() == 10);
    const auto& empty_skin = mask_of(result, SemanticPart::FaceSkin).alpha;
    assert(std::all_of(empty_skin.begin(), empty_skin.end(), [](auto a) { return a == 0; }));
  }

  // End to end on the preview path: a local brightening limited to
  // "semantic:person.face_skin" changes face skin and nothing far from it.
  {
    std::shared_ptr<SemanticSegmenter> segmenter;
    assert(create_portrait_segmenter(kModels, 4, segmenter, diagnostic) == ResultCode::Ok);
    RenderService service(segmenter);
    auto preview = [&](std::uint64_t sequence, const std::string& mask_id) {
      PreviewWork work;
      work.client_sequence = sequence;
      work.request.image_id = "portrait";
      work.request.editing_instance_id = "edit";
      work.request.base_revision = 1;
      work.request.capability_id = "color.local";
      work.request.parameter_id = "brightness";
      work.request.display_value = "40";
      work.request.max_edge = 4000;
      work.description.image_id = "portrait";
      work.description.editing_instance_id = "edit";
      work.description.document_revision = 1;
      pixcake::render::RenderOperation operation;
      operation.capability_id = "color.local";
      operation.stage = "local";
      operation.program = "pixcake.color.local";
      operation.order = 1;
      operation.values = {{"brightness", "brightness", "40", 40.0}};
      operation.mask_ids = {mask_id};
      work.description.operations.push_back(operation);
      work.input = portrait;
      std::string message;
      assert(service.submit(std::move(work), message) == ResultCode::Ok);
      const auto start = std::chrono::steady_clock::now();
      auto frame = service.render_latest();
      std::printf("preview %s %.1f ms\n", mask_id.c_str(),
                  std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
      return frame;
    };
    auto brightness = [&](const PreviewResult& frame, std::uint32_t x, std::uint32_t y) {
      const auto* p = frame.rgba.data() + static_cast<std::size_t>(y) * frame.header.stride + x * 4;
      return p[0] + p[1] + p[2];
    };
    auto original = [&](std::uint32_t x, std::uint32_t y) {
      const auto* p = portrait.bytes.data() + static_cast<std::size_t>(y) * portrait.stride + x * 4;
      return p[0] + p[1] + p[2];
    };

    const auto frame = preview(1, "semantic:person.face_skin");
    assert(frame.code == ResultCode::Ok);
    assert(frame.header.width == portrait.width && frame.header.height == portrait.height);
    // Cheek (between the right eye and the nose, below the eye) is brighter;
    // the background corner and the suit are untouched.
    assert(brightness(frame, 380, 230) > original(380, 230) + 60);
    assert(brightness(frame, 20, 20) == original(20, 20));
    assert(brightness(frame, 400, 800) == original(400, 800));

    // Same image, next slider value: segmentation is reused (much faster).
    const auto again = preview(2, "semantic:person.face_skin");
    assert(again.code == ResultCode::Ok && again.rgba == frame.rgba);

    // Teeth are not output by any locked model: nothing changes and the reason is given.
    const auto teeth = preview(3, "semantic:person.teeth");
    assert(teeth.code == ResultCode::Ok);
    assert(brightness(teeth, 380, 230) == original(380, 230));
    assert(std::any_of(teeth.diagnostics.begin(), teeth.diagnostics.end(), [](const auto& d) {
      return d.reason.find("牙齿不可用") != std::string::npos;
    }));
  }
  return 0;
}
