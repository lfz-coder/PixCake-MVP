#include "pixcake/mask/semantic_parts.hpp"

#include <algorithm>

namespace pixcake::mask {

const std::vector<SemanticPartEntry>& semantic_parts() {
  static const std::vector<SemanticPartEntry> parts = {
      // 人物蒙版子项（需求 10.4）
      {"person.all", "全部人物/整个人"},
      {"person.skin.all", "全部皮肤"},
      {"person.face", "面部"},
      {"person.face_skin", "面部皮肤"},
      {"person.neck", "脖子"},
      {"person.body_skin", "身体皮肤"},
      {"person.eyebrow", "眉毛"},
      {"person.eye_white", "眼白"},
      {"person.iris", "眼瞳"},
      {"person.lips", "嘴唇"},
      {"person.teeth", "牙齿"},
      {"person.oral_cavity", "口腔"},
      {"person.ear", "耳朵"},
      {"person.hair", "头发"},
      // 衣物蒙版子项（需求 10.4）
      {"clothes.all", "全部衣服"},
      {"clothes.upper", "上衣"},
      {"clothes.inner", "内搭"},
      {"clothes.jumpsuit", "连体衣（裙）"},
      {"clothes.lower", "下装"},
      {"clothes.veil_or_hat", "头纱/帽子"},
      {"clothes.gloves", "手套"},
      {"clothes.shoes", "鞋子"},
      {"clothes.backpack", "背包"},
      {"clothes.jewelry", "首饰"},
      {"clothes.other_objects", "其他物品"},
  };
  return parts;
}

namespace {

bool known_part(std::string_view part_id) {
  const auto& parts = semantic_parts();
  return !part_id.empty() && std::any_of(parts.begin(), parts.end(),
                                         [&](const SemanticPartEntry& p) { return part_id == p.id; });
}

}  // namespace

std::string semantic_mask_id(std::string_view part_id) {
  if (!known_part(part_id)) return {};
  return std::string(kSemanticMaskPrefix) + std::string(part_id);
}

bool is_semantic_mask_id(std::string_view mask_id) {
  return mask_id.substr(0, kSemanticMaskPrefix.size()) == kSemanticMaskPrefix;
}

bool semantic_part_of_mask_id(std::string_view mask_id, std::string& part_id) {
  part_id.clear();
  if (!is_semantic_mask_id(mask_id)) return false;
  const std::string_view candidate = mask_id.substr(kSemanticMaskPrefix.size());
  if (!known_part(candidate)) return false;
  part_id = std::string(candidate);
  return true;
}

}  // namespace pixcake::mask
