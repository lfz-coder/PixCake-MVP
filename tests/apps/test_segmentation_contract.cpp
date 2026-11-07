#include "pixcake/renderd/segmentation.hpp"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <utility>

namespace {

pixcake::renderd::SegmentationResult baseline() {
  using namespace pixcake::renderd;
  SegmentationResult result;
  result.model_id = "test.segmenter";
  result.model_version = "fixture-1";
  for (const auto& descriptor : semantic_part_catalog()) {
    SemanticPartCapability capability;
    capability.part = descriptor.part;
    capability.status = SupportStatus::Unsupported;
    capability.diagnostic = "fixture does not provide this part";
    result.capabilities.push_back(std::move(capability));
  }
  return result;
}

}  // namespace

int main() {
  using namespace pixcake;
  using namespace pixcake::renderd;

  assert(semantic_part_catalog().size() == 25);
  assert(std::string(semantic_part_id(SemanticPart::EyeWhite)) == "person.eye_white");
  assert(std::string(semantic_part_id(SemanticPart::UpperClothes)) == "clothes.upper");

  RgbaInput input;
  input.source_format = SourceFormat::Jpeg;
  input.width = 2;
  input.height = 1;
  input.stride = 8;
  input.bytes.assign(8, 255);

  std::string diagnostic;
  auto result = baseline();
  assert(validate_segmentation_result(input, result, diagnostic) == ResultCode::Ok);

  auto face = std::find_if(result.capabilities.begin(), result.capabilities.end(),
                           [](const auto& item) { return item.part == SemanticPart::Face; });
  assert(face != result.capabilities.end());
  face->status = SupportStatus::Supported;
  face->source_labels = {"face"};
  face->diagnostic.clear();
  result.masks.push_back({SemanticPart::Face, 2, 1, 2, {255, 0}});
  const auto supported_code = validate_segmentation_result(input, result, diagnostic);
  if (supported_code != ResultCode::Ok) std::cerr << diagnostic << '\n';
  assert(supported_code == ResultCode::Ok);

  result.masks.push_back({SemanticPart::EyeWhite, 2, 1, 2, {255, 255}});
  assert(validate_segmentation_result(input, result, diagnostic) == ResultCode::UnsupportedFormat);
  result.masks.pop_back();

  result.model_version.clear();
  assert(validate_segmentation_result(input, result, diagnostic) == ResultCode::StateConflict);
  result.model_version = "fixture-1";

  result.capabilities.front().diagnostic.clear();
  assert(validate_segmentation_result(input, result, diagnostic) == ResultCode::StateConflict);
  result.capabilities.front().diagnostic = "fixture does not provide this part";

  face->status = SupportStatus::Derived;
  face->source_labels.clear();
  assert(validate_segmentation_result(input, result, diagnostic) == ResultCode::StateConflict);
  face->source_labels = {"face"};
  assert(validate_segmentation_result(input, result, diagnostic) == ResultCode::Ok);

  // An input whose pixel buffer is shorter than stride * height is rejected
  // even when the caller skipped the adapter.
  auto short_input = input;
  short_input.bytes.resize(7);
  assert(validate_segmentation_result(short_input, result, diagnostic) == ResultCode::StateConflict);
  return 0;
}
