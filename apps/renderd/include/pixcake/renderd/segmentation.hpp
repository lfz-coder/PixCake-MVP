#pragma once

#include "pixcake/renderd/image_io.hpp"
#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pixcake::renderd {

// These IDs are the project contract from phase-one requirements.  A model's
// labels are mapped to these IDs by a process-side adapter; model labels must
// never silently become public project IDs.
enum class SemanticPart {
  AllPerson,
  AllSkin,
  Face,
  FaceSkin,
  Neck,
  BodySkin,
  Eyebrow,
  EyeWhite,
  Iris,
  Lips,
  Teeth,
  OralCavity,
  Ear,
  Hair,
  AllClothes,
  UpperClothes,
  InnerLayer,
  Jumpsuit,
  LowerClothes,
  VeilOrHat,
  Gloves,
  Shoes,
  Backpack,
  Jewelry,
  OtherObjects,
};

enum class SupportStatus {
  Supported,
  Derived,
  Unsupported,
};

struct SemanticPartDescriptor {
  SemanticPart part;
  const char* id;
  const char* label;
};

const std::vector<SemanticPartDescriptor>& semantic_part_catalog();
const char* semantic_part_id(SemanticPart part);

struct SemanticPartCapability {
  SemanticPart part = SemanticPart::AllPerson;
  SupportStatus status = SupportStatus::Unsupported;
  // Original labels emitted by the selected model.  Derived parts may list
  // more than one source label; unsupported parts must list none.
  std::vector<std::string> source_labels;
  // Required when the part is unsupported.  This is user-visible diagnostic
  // data, not a license or model-selection decision.
  std::string diagnostic;
};

struct SemanticMask {
  SemanticPart part = SemanticPart::AllPerson;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t stride = 0;  // one alpha byte per pixel
  std::vector<std::uint8_t> alpha;
};

struct SegmentationResult {
  std::string model_id;
  std::string model_version;
  std::vector<SemanticPartCapability> capabilities;
  std::vector<SemanticMask> masks;
};

// Validates a model adapter result before it can enter the preview or export
// path.  It rejects missing capability declarations, guessed labels, masks for
// unsupported parts, duplicate parts, and malformed alpha buffers.
ResultCode validate_segmentation_result(const RgbaInput& input, const SegmentationResult& result,
                                        std::string& diagnostic);

class SemanticSegmenter {
 public:
  virtual ~SemanticSegmenter() = default;

  // Implementations belong to renderd/exportd adapters.  They may use a
  // third-party model runtime, but must return an owned, validated result and
  // must not make kernel code depend on that runtime.
  virtual ResultCode segment(const RgbaInput& input, SegmentationResult& out,
                             std::string& diagnostic) = 0;
};

}  // namespace pixcake::renderd
