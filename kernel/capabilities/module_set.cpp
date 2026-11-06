#include "pixcake/capabilities/module_set.hpp"

#include "pixcake/capabilities/color_match/color_match_module.hpp"
#include "pixcake/capabilities/color_style/color_style_module.hpp"
#include "pixcake/capabilities/color_global/color_global_module.hpp"
#include "pixcake/capabilities/color_local/color_local_module.hpp"
#include "pixcake/capabilities/color_post/color_post_module.hpp"
#include "pixcake/capabilities/detail/detail_module.hpp"
#include "pixcake/capabilities/portrait_blemish/portrait_blemish_module.hpp"
#include "pixcake/capabilities/portrait_skin/portrait_skin_module.hpp"
#include "pixcake/capabilities/portrait_reshape/portrait_reshape_module.hpp"
#include "pixcake/capabilities/portrait_feature/portrait_feature_module.hpp"
#include "pixcake/capabilities/portrait_body/portrait_body_module.hpp"
#include "pixcake/capabilities/image_background/image_background_module.hpp"
#include "pixcake/capabilities/image_idphoto/image_idphoto_module.hpp"
#include "pixcake/capabilities/image_sky/image_sky_module.hpp"
#include "pixcake/capabilities/clothes/clothes_module.hpp"
#include "pixcake/capabilities/pet/pet_module.hpp"
#include "pixcake/capabilities/crop/crop_module.hpp"
#include "pixcake/capabilities/manual_edge/manual_edge_module.hpp"
#include "pixcake/capabilities/manual_liquify/manual_liquify_module.hpp"
#include "pixcake/capabilities/manual_heal/manual_heal_module.hpp"

#include <memory>

namespace pixcake {
namespace {

using ModuleFactory = std::unique_ptr<CapabilityModule> (*)(CapabilityDescriptor);

struct FactoryEntry {
  const char* capability_id;
  ModuleFactory make;
};

// 能力标识到它自己目录里的工厂。新增能力：建目录、写工厂、在这里加一行。
constexpr FactoryEntry kFactories[] = {
    {"color.match", &make_color_match_module},
    {"color.style", &make_color_style_module},
    {"color.global", &make_color_global_module},
    {"color.local", &make_color_local_module},
    {"color.post", &make_color_post_module},
    {"detail", &make_detail_module},
    {"portrait.blemish", &make_portrait_blemish_module},
    {"portrait.skin", &make_portrait_skin_module},
    {"portrait.reshape", &make_portrait_reshape_module},
    {"portrait.feature", &make_portrait_feature_module},
    {"portrait.body", &make_portrait_body_module},
    {"image.background", &make_image_background_module},
    {"image.idphoto", &make_image_idphoto_module},
    {"image.sky", &make_image_sky_module},
    {"clothes", &make_clothes_module},
    {"pet", &make_pet_module},
    {"crop", &make_crop_module},
    {"manual.edge", &make_manual_edge_module},
    {"manual.liquify", &make_manual_liquify_module},
    {"manual.heal", &make_manual_heal_module},
};

std::vector<std::unique_ptr<CapabilityModule>> build_modules() {
  std::vector<std::unique_ptr<CapabilityModule>> modules;
  const CapabilityRegistry registry = CapabilityRegistry::phase_one();  // 不能对临时对象的成员做范围循环
  for (const auto& descriptor : registry.descriptors()) {
    for (const auto& entry : kFactories) {
      if (descriptor.id == entry.capability_id) {
        modules.push_back(entry.make(descriptor));
        break;
      }
    }
  }
  return modules;
}

const std::vector<std::unique_ptr<CapabilityModule>>& owned_modules() {
  static const std::vector<std::unique_ptr<CapabilityModule>> modules = build_modules();
  return modules;
}

}  // namespace

const std::vector<const CapabilityModule*>& phase_one_capability_modules() {
  static const std::vector<const CapabilityModule*> view = [] {
    std::vector<const CapabilityModule*> out;
    for (const auto& module : owned_modules()) out.push_back(module.get());
    return out;
  }();
  return view;
}

const CapabilityModule* find_capability_module(std::string_view capability_id) {
  for (const auto* module : phase_one_capability_modules()) {
    if (module->id() == capability_id) return module;
  }
  return nullptr;
}

FragmentTransfer transfer_fragment(std::string_view capability_id, const std::optional<FragmentBytes>& source,
                                   const std::optional<FragmentBytes>& target) {
  FragmentTransfer result;
  const CapabilityModule* module = find_capability_module(capability_id);
  if (module == nullptr) {
    result.fragment = source;
    return result;
  }
  if (target && target->version > module->current_version()) {
    result.keep_target = true;
    return result;
  }
  if (!source) return result;
  if (source->version > module->current_version()) {
    result.fragment = source;
    return result;
  }
  const FragmentResult migrated = module->migrate(*source);
  if (migrated.code != ResultCode::Ok) {
    result.code = migrated.code;
    return result;
  }
  const ExtractResult extracted = module->extract(migrated.fragment);
  if (extracted.code != ResultCode::Ok) {
    result.code = extracted.code;
    return result;
  }
  for (const auto& value : extracted.values) {
    if (!validate_parameter_display_value(module->id(), value.parameter_id, value.display_value)) {
      result.code = ResultCode::StateConflict;
      return result;
    }
  }
  result.fragment = migrated.fragment;
  return result;
}

}  // namespace pixcake
