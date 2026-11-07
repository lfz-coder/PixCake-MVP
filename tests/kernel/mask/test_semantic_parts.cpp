#include "pixcake/mask/semantic_parts.hpp"

#include <cassert>
#include <set>
#include <string>

int main() {
  using namespace pixcake::mask;

  // Requirement 10.4: 14 person parts then 11 clothing parts, stable ids.
  const auto& parts = semantic_parts();
  assert(parts.size() == 25);
  assert(std::string(parts.front().id) == "person.all");
  assert(std::string(parts.front().label) == "全部人物/整个人");
  assert(std::string(parts[3].id) == "person.face_skin");
  assert(std::string(parts.back().id) == "clothes.other_objects");
  std::set<std::string> ids;
  for (const auto& part : parts) {
    assert(ids.insert(part.id).second);  // unique
    const std::string id = part.id;
    assert(id.rfind("person.", 0) == 0 || id.rfind("clothes.", 0) == 0);
    assert(std::string(part.label).size() > 0);
  }

  // Mask ids: "semantic:" + part id, only for known parts.
  assert(semantic_mask_id("person.face_skin") == "semantic:person.face_skin");
  assert(semantic_mask_id("person.unknown").empty());
  assert(semantic_mask_id("").empty());
  std::string part;
  assert(semantic_part_of_mask_id("semantic:person.hair", part) && part == "person.hair");
  assert(!semantic_part_of_mask_id("semantic:person.unknown", part) && part.empty());
  assert(!semantic_part_of_mask_id("brush:3", part));
  assert(!semantic_part_of_mask_id("person.hair", part));  // prefix is required
  assert(is_semantic_mask_id("semantic:anything"));
  assert(!is_semantic_mask_id("layer:1"));
  return 0;
}
