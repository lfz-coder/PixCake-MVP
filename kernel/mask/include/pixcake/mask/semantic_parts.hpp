#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace pixcake::mask {

// Semantic parts of requirement 10.4 (局部调色), in requirement order.  This
// table is the single owner of part ids and display names (architecture 6.6):
// capabilities store mask ids only, and the inference adapters in
// renderd/exportd map model labels onto these ids.  Pixels are never produced
// here; kernel/ links no inference runtime.
struct SemanticPartEntry {
  const char* id;
  const char* label;
};

const std::vector<SemanticPartEntry>& semantic_parts();

// A render operation refers to a semantic part through the mask id
// "semantic:<part id>", e.g. "semantic:person.face_skin".
inline constexpr std::string_view kSemanticMaskPrefix = "semantic:";

// Empty when the part id is not in semantic_parts().
std::string semantic_mask_id(std::string_view part_id);
// True when the id carries the semantic prefix (known part or not).
bool is_semantic_mask_id(std::string_view mask_id);
// True and sets `part_id` only for a semantic mask id of a known part;
// otherwise returns false and clears `part_id`.
bool semantic_part_of_mask_id(std::string_view mask_id, std::string& part_id);

}  // namespace pixcake::mask
