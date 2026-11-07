// Full-resolution semantic masks for exportd (requirement 10.4), with a fake
// segmenter and decoder (no models): the original is decoded at the preview
// edge, segmented once, and each referenced mask is resized to the original's
// size; parts the model does not output are skipped as in the preview; no
// segmentation (not built, or failed) is an error the export reports.
#include "pixcake/exportd/export_masks.hpp"

#include <cassert>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace pixcake;
using namespace pixcake::exportd;
using renderd::RgbaInput;

// Hair is the left column of the preview-size image; nothing else is output.
class FakeSegmenter final : public renderd::SemanticSegmenter {
 public:
  int calls = 0;
  ResultCode fail_with = ResultCode::Ok;
  std::uint32_t seen_width = 0;

  ResultCode segment(const RgbaInput& input, renderd::SegmentationResult& out, std::string& diagnostic) override {
    ++calls;
    seen_width = input.width;
    if (fail_with != ResultCode::Ok) {
      diagnostic = "fake failure";
      return fail_with;
    }
    out = {};
    out.model_id = "fake";
    out.model_version = "1";
    for (const auto& descriptor : renderd::semantic_part_catalog()) {
      renderd::SemanticPartCapability capability;
      capability.part = descriptor.part;
      capability.status = renderd::SupportStatus::Unsupported;
      capability.diagnostic = "fake model has no such label";
      if (descriptor.part == renderd::SemanticPart::Hair) {
        capability.status = renderd::SupportStatus::Supported;
        capability.source_labels = {"hair"};
        capability.diagnostic.clear();
        renderd::SemanticMask mask;
        mask.part = renderd::SemanticPart::Hair;
        mask.width = input.width;
        mask.height = input.height;
        mask.stride = input.width;
        mask.alpha.assign(static_cast<std::size_t>(input.width) * input.height, 0);
        for (std::uint32_t y = 0; y < input.height; ++y) mask.alpha[y * input.width] = 255;
        out.masks.push_back(std::move(mask));
      }
      out.capabilities.push_back(std::move(capability));
    }
    return renderd::validate_segmentation_result(input, out, diagnostic);
  }
};

// Decodes to a fixed preview-size image and records the request.
class FakeDecoder final : public renderd::ImageDecoder {
 public:
  std::uint32_t width = 4;
  std::uint32_t height = 2;
  int calls = 0;
  renderd::DecodeRequest last;

  ResultCode decode(const renderd::DecodeRequest& request, renderd::DecodedImage& out, std::string&) override {
    ++calls;
    last = request;
    out = {};
    out.width = width;
    out.height = height;
    out.stride = width * 4;
    out.bytes.assign(static_cast<std::size_t>(out.stride) * height, 128);
    return ResultCode::Ok;
  }
};

// Nearest neighbour: enough to see where each target pixel came from.
ResultCode nearest(const std::uint8_t* source, std::uint32_t width, std::uint32_t height, std::uint32_t target_width,
                   std::uint32_t target_height, std::vector<std::uint8_t>& out, std::string&) {
  out.assign(static_cast<std::size_t>(target_width) * target_height, 0);
  for (std::uint32_t y = 0; y < target_height; ++y) {
    for (std::uint32_t x = 0; x < target_width; ++x) {
      out[static_cast<std::size_t>(y) * target_width + x] =
          source[static_cast<std::size_t>(y * height / target_height) * width + x * width / target_width];
    }
  }
  return ResultCode::Ok;
}

render::RenderDescription described(std::vector<std::string> mask_ids) {
  render::RenderDescription description;
  description.image_id = "photo-1";
  render::RenderOperation operation;
  operation.capability_id = "color.local";
  operation.stage = "local";
  operation.program = "pixcake.color.local";
  operation.mask_ids = std::move(mask_ids);
  description.operations.push_back(operation);
  return description;
}

}  // namespace

int main() {
  auto segmenter = std::make_shared<FakeSegmenter>();
  auto decoder = std::make_shared<FakeDecoder>();
  SemanticExportMasks masks(segmenter, decoder, nearest);
  ExportMasks out;
  std::string diagnostic;

  // Nothing referenced: no decode, no segmentation.
  assert(masks.provide("/photo.jpg", described({}), 8, 4, out, diagnostic) == ResultCode::Ok);
  assert(out.masks.empty() && decoder->calls == 0 && segmenter->calls == 0);

  // Hair at 8x4 from the 4x2 preview: the left two target columns; lips are
  // not output by the model and are skipped with a reason; a brush mask is
  // not a semantic mask (no source here, the renderer skips it as in preview).
  assert(masks.provide("/photo.jpg", described({"semantic:person.hair", "semantic:person.lips", "brush:1"}), 8, 4,
                       out, diagnostic) == ResultCode::Ok);
  assert(decoder->calls == 1 && decoder->last.source_path == "/photo.jpg");
  assert(decoder->last.max_edge == SemanticExportMasks::kPreviewEdge);
  assert(segmenter->calls == 1 && segmenter->seen_width == 4);
  assert(out.masks.size() == 1 && out.masks[0].id == "semantic:person.hair");
  assert(out.masks[0].width == 8 && out.masks[0].height == 4 && out.masks[0].alpha.size() == 32);
  for (std::uint32_t y = 0; y < 4; ++y) {
    for (std::uint32_t x = 0; x < 8; ++x) assert(out.masks[0].alpha[y * 8 + x] == (x < 2 ? 255 : 0));
  }
  assert(out.skipped.size() == 1 && out.skipped[0].reason.find("person.lips") != std::string::npos);

  // The original as small as the preview: no resize needed.
  assert(masks.provide("/photo.jpg", described({"semantic:person.hair"}), 4, 2, out, diagnostic) == ResultCode::Ok);
  assert(out.masks.size() == 1 && out.masks[0].width == 4);

  // A preview with another orientation than the original is refused.
  assert(masks.provide("/photo.jpg", described({"semantic:person.hair"}), 4, 8, out, diagnostic) ==
         ResultCode::StateConflict);

  // Segmentation failed: an error with the reason (the export fails, retryable).
  segmenter->fail_with = ResultCode::IoFailed;
  assert(masks.provide("/photo.jpg", described({"semantic:person.hair"}), 8, 4, out, diagnostic) ==
         ResultCode::UnsupportedFormat);
  assert(diagnostic.find("fake failure") != std::string::npos && out.masks.empty());

  // Not built: an error, never an export without the effect.
  SemanticExportMasks unbuilt(nullptr, decoder, nearest);
  assert(unbuilt.provide("/photo.jpg", described({"semantic:person.hair"}), 8, 4, out, diagnostic) ==
         ResultCode::UnsupportedFormat);
  assert(!diagnostic.empty());

  std::printf("export masks ok\n");
  return 0;
}
