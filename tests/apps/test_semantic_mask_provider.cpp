// Semantic masks on the preview path, with a fake segmenter (no models):
// RenderService finds "semantic:<part>" mask ids in the render description,
// runs the segmenter once per preview image, hands the masks to the reference
// renderer, and reports parts that are unavailable instead of substituting.
#include "pixcake/renderd/render_service.hpp"
#include "pixcake/renderd/semantic_mask_provider.hpp"

#include <algorithm>
#include <cassert>
#include <memory>
#include <string>

namespace {

using namespace pixcake::renderd;
using pixcake::ResultCode;

class FakeSegmenter final : public SemanticSegmenter {
 public:
  int calls = 0;
  ResultCode fail_with = ResultCode::Ok;

  ResultCode segment(const RgbaInput& input, SegmentationResult& out, std::string& diagnostic) override {
    ++calls;
    if (fail_with != ResultCode::Ok) {
      diagnostic = "fake segmenter failure";
      return fail_with;
    }
    out = {};
    out.model_id = "fake";
    out.model_version = "1";
    for (const auto& descriptor : semantic_part_catalog()) {
      SemanticPartCapability capability;
      capability.part = descriptor.part;
      capability.status = SupportStatus::Unsupported;
      capability.diagnostic = "fake model has no such label";
      if (descriptor.part == SemanticPart::Hair) {
        capability.status = SupportStatus::Supported;
        capability.source_labels = {"hair"};
        capability.diagnostic.clear();
        SemanticMask mask;
        mask.part = SemanticPart::Hair;
        mask.width = input.width;
        mask.height = input.height;
        mask.stride = input.width + 3;  // padded rows must be packed for the renderer
        mask.alpha.assign(static_cast<std::size_t>(mask.stride) * input.height, 7);
        for (std::uint32_t y = 0; y < input.height; ++y) {
          for (std::uint32_t x = 0; x < input.width; ++x) mask.alpha[y * mask.stride + x] = x == 0 ? 255 : 0;
        }
        out.masks.push_back(std::move(mask));
      }
      out.capabilities.push_back(std::move(capability));
    }
    return validate_segmentation_result(input, out, diagnostic);
  }
};

PreviewWork work(std::uint64_t sequence, std::vector<std::string> mask_ids, std::uint8_t grey = 100) {
  PreviewWork value;
  value.client_sequence = sequence;
  value.request.image_id = "image-1";
  value.request.editing_instance_id = "edit-1";
  value.request.base_revision = 7;
  value.request.capability_id = "color.local";
  value.request.parameter_id = "brightness";
  value.request.display_value = "100";
  value.request.max_edge = 4000;
  value.description.image_id = "image-1";
  value.description.editing_instance_id = "edit-1";
  value.description.document_revision = 7;
  pixcake::render::RenderOperation operation;
  operation.capability_id = "color.local";
  operation.stage = "local";
  operation.program = "pixcake.color.local";
  operation.order = 1;
  operation.values = {{"brightness", "brightness", "100", 100.0}};
  operation.mask_ids = std::move(mask_ids);
  value.description.operations.push_back(operation);
  value.input.width = 2;
  value.input.height = 1;
  value.input.stride = 12;  // padded input rows
  value.input.bytes = {grey, grey, grey, 255, grey, grey, grey, 255, 1, 2, 3, 4};
  return value;
}

bool has_diagnostic(const PreviewResult& result, const std::string& needle) {
  return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                     [&](const auto& d) { return d.reason.find(needle) != std::string::npos; });
}

}  // namespace

int main() {
  std::string diagnostic;

  // Only enabled operations' semantic ids are collected, de-duplicated, in order.
  {
    auto w = work(1, {"semantic:person.hair", "brush:1", "semantic:person.lips", "semantic:person.hair"});
    auto disabled = w.description.operations.front();
    disabled.enabled = false;
    disabled.mask_ids = {"semantic:person.teeth"};
    w.description.operations.push_back(disabled);
    const auto ids = SemanticMaskProvider::referenced_semantic_ids(w.description);
    assert((ids == std::vector<std::string>{"semantic:person.hair", "semantic:person.lips"}));
  }

  // The hair mask limits the local brightening to the first pixel.
  auto segmenter = std::make_shared<FakeSegmenter>();
  RenderService service(segmenter);
  assert(service.submit(work(1, {"semantic:person.hair"}), diagnostic) == ResultCode::Ok);
  PreviewResult frame = service.render_latest();
  assert(frame.code == ResultCode::Ok);
  assert(frame.header.width == 2 && frame.header.stride == 8);
  assert(frame.rgba[0] > 200);    // masked pixel: brightened
  assert(frame.rgba[4] == 100);   // unmasked pixel: unchanged
  assert(segmenter->calls == 1);
  assert(std::any_of(frame.statuses.begin(), frame.statuses.end(), [](const auto& s) {
    return s.capability_id == "color.local" && s.status == pixcake::gpu::PassStatus::Implemented;
  }));

  // A slider drag on the same image reuses the segmentation.
  assert(service.submit(work(2, {"semantic:person.hair"}), diagnostic) == ResultCode::Ok);
  frame = service.render_latest();
  assert(frame.code == ResultCode::Ok && segmenter->calls == 1);
  // Different pixels (another image or a new decode) run it again.
  assert(service.submit(work(3, {"semantic:person.hair"}, 120), diagnostic) == ResultCode::Ok);
  frame = service.render_latest();
  assert(frame.code == ResultCode::Ok && segmenter->calls == 2);
  assert(frame.rgba[4] == 120);

  // An unsupported part gets no mask: the operation is skipped with a reason,
  // never painted through another part.
  assert(service.submit(work(4, {"semantic:person.teeth"}, 100), diagnostic) == ResultCode::Ok);
  frame = service.render_latest();
  assert(frame.code == ResultCode::Ok);
  assert(frame.rgba[0] == 100 && frame.rgba[4] == 100);
  assert(has_diagnostic(frame, "牙齿"));
  assert(has_diagnostic(frame, "fake model has no such label"));
  assert(std::any_of(frame.statuses.begin(), frame.statuses.end(), [](const auto& s) {
    return s.status == pixcake::gpu::PassStatus::MissingResource;
  }));

  // An unknown part id is reported as such.
  assert(service.submit(work(5, {"semantic:person.tail"}), diagnostic) == ResultCode::Ok);
  frame = service.render_latest();
  assert(frame.code == ResultCode::Ok && frame.rgba[0] == 100);
  assert(has_diagnostic(frame, "person.tail"));

  // Segmentation failure: preview still renders, masked operation skipped, reason reported.
  segmenter->fail_with = ResultCode::IoFailed;
  assert(service.submit(work(6, {"semantic:person.hair"}, 130), diagnostic) == ResultCode::Ok);
  frame = service.render_latest();
  assert(frame.code == ResultCode::Ok && frame.rgba[0] == 130);
  assert(has_diagnostic(frame, "fake segmenter failure"));
  segmenter->fail_with = ResultCode::Ok;

  // No segmenter (inference not built): explicit reason, no substitution.
  RenderService without;
  assert(without.submit(work(7, {"semantic:person.hair"}), diagnostic) == ResultCode::Ok);
  frame = without.render_latest();
  assert(frame.code == ResultCode::Ok && frame.rgba[0] == 100);
  assert(has_diagnostic(frame, "语义分割"));

  // Operations without semantic masks never run the segmenter.
  const int before = segmenter->calls;
  assert(service.submit(work(8, {}, 140), diagnostic) == ResultCode::Ok);
  frame = service.render_latest();
  assert(frame.code == ResultCode::Ok && frame.rgba[0] > 200 && frame.rgba[4] > 200);
  assert(segmenter->calls == before);
  return 0;
}
