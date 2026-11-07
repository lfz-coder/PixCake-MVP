#include "pixcake/renderd/segmentation.hpp"

#include "pixcake/mask/semantic_parts.hpp"

#include <algorithm>
#include <limits>

namespace pixcake::renderd {
namespace {

// Ids and names come from kernel/mask (architecture 6.6, single owner).  The
// SemanticPart enum lists the same parts in the same order; the catalog pairs
// them by position and is empty if the two ever disagree in size, which makes
// every validation fail loudly instead of mislabelling a part.
std::vector<SemanticPartDescriptor> build_catalog() {
  const auto& parts = mask::semantic_parts();
  constexpr std::size_t kEnumCount = static_cast<std::size_t>(SemanticPart::OtherObjects) + 1;
  std::vector<SemanticPartDescriptor> catalog;
  if (parts.size() != kEnumCount) return catalog;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    catalog.push_back({static_cast<SemanticPart>(i), parts[i].id, parts[i].label});
  }
  return catalog;
}

const std::vector<SemanticPartDescriptor> kCatalog = build_catalog();

bool contains_part(const std::vector<SemanticPartCapability>& capabilities, SemanticPart part,
                   const SemanticPartCapability** found) {
  const auto it = std::find_if(capabilities.begin(), capabilities.end(),
                               [part](const auto& item) { return item.part == part; });
  if (it == capabilities.end()) return false;
  if (found != nullptr) *found = &*it;
  return true;
}

bool contains_mask(const std::vector<SemanticMask>& masks, SemanticPart part, const SemanticMask** found) {
  const auto it = std::find_if(masks.begin(), masks.end(),
                               [part](const auto& item) { return item.part == part; });
  if (it == masks.end()) return false;
  if (found != nullptr) *found = &*it;
  return true;
}

}  // namespace

const std::vector<SemanticPartDescriptor>& semantic_part_catalog() { return kCatalog; }

const char* semantic_part_id(SemanticPart part) {
  const auto it = std::find_if(kCatalog.begin(), kCatalog.end(),
                               [part](const auto& item) { return item.part == part; });
  return it == kCatalog.end() ? "" : it->id;
}

ResultCode validate_segmentation_result(const RgbaInput& input, const SegmentationResult& result,
                                        std::string& diagnostic) {
  const std::uint64_t minimum_input_stride = static_cast<std::uint64_t>(input.width) * 4ull;
  if (input.width == 0 || input.height == 0 || minimum_input_stride > std::numeric_limits<std::uint32_t>::max() ||
      input.stride < minimum_input_stride ||
      static_cast<std::uint64_t>(input.stride) * input.height > std::numeric_limits<std::size_t>::max() ||
      input.bytes.size() < static_cast<std::size_t>(input.stride) * input.height) {
    diagnostic = "segmentation input geometry is invalid";
    return ResultCode::StateConflict;
  }
  if (result.model_id.empty() || result.model_version.empty()) {
    diagnostic = "segmentation result is missing model identity";
    return ResultCode::StateConflict;
  }
  if (result.capabilities.size() != kCatalog.size()) {
    diagnostic = "segmentation result must declare every project semantic part";
    return ResultCode::StateConflict;
  }

  for (const auto& descriptor : kCatalog) {
    const SemanticPartCapability* capability = nullptr;
    if (!contains_part(result.capabilities, descriptor.part, &capability)) {
      diagnostic = std::string("segmentation result is missing capability: ") + descriptor.id;
      return ResultCode::StateConflict;
    }
    const auto duplicate = std::count_if(result.capabilities.begin(), result.capabilities.end(),
                                         [part = descriptor.part](const auto& item) { return item.part == part; });
    if (duplicate != 1) {
      diagnostic = std::string("segmentation result has duplicate capability: ") + descriptor.id;
      return ResultCode::StateConflict;
    }
    if (capability->status == SupportStatus::Unsupported) {
      if (!capability->source_labels.empty() || capability->diagnostic.empty()) {
        diagnostic = std::string("unsupported semantic part has an invalid declaration: ") + descriptor.id;
        return ResultCode::StateConflict;
      }
    } else if (capability->source_labels.empty() || !capability->diagnostic.empty()) {
      diagnostic = std::string("supported semantic part lacks an explicit source label: ") + descriptor.id;
      return ResultCode::StateConflict;
    }
  }

  for (const auto& mask : result.masks) {
    const SemanticPartCapability* capability = nullptr;
    if (!contains_part(result.capabilities, mask.part, &capability)) {
      diagnostic = "segmentation mask has an undeclared semantic part";
      return ResultCode::StateConflict;
    }
    if (capability->status == SupportStatus::Unsupported) {
      diagnostic = std::string("segmentation mask was returned for an unsupported part: ") +
                   semantic_part_id(mask.part);
      return ResultCode::UnsupportedFormat;
    }
    const auto duplicate = std::count_if(result.masks.begin(), result.masks.end(),
                                         [part = mask.part](const auto& item) { return item.part == part; });
    if (duplicate != 1 || mask.width != input.width || mask.height != input.height || mask.stride < mask.width ||
        static_cast<std::uint64_t>(mask.stride) * mask.height > std::numeric_limits<std::size_t>::max() ||
        mask.alpha.size() < static_cast<std::size_t>(mask.stride) * mask.height) {
      diagnostic = std::string("segmentation mask geometry or alpha buffer is invalid: ") +
                   semantic_part_id(mask.part);
      return ResultCode::ImageCorrupt;
    }
  }

  for (const auto& capability : result.capabilities) {
    const SemanticMask* mask = nullptr;
    const bool has_mask = contains_mask(result.masks, capability.part, &mask);
    if (capability.status == SupportStatus::Unsupported && has_mask) {
      diagnostic = "unsupported semantic part has a mask";
      return ResultCode::UnsupportedFormat;
    }
    if (capability.status != SupportStatus::Unsupported && !has_mask) {
      diagnostic = std::string("supported semantic part has no alpha mask: ") + semantic_part_id(capability.part);
      return ResultCode::ImageCorrupt;
    }
  }

  diagnostic.clear();
  return ResultCode::Ok;
}

}  // namespace pixcake::renderd
