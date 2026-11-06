#include "pixcake/capabilities/module_set.hpp"

#include "pixcake/capabilities/color_global/color_global_module.hpp"
#include "pixcake/capabilities/detail/detail_module.hpp"

#include <memory>

namespace pixcake {
namespace {

std::vector<std::unique_ptr<CapabilityModule>> build_modules() {
  std::vector<std::unique_ptr<CapabilityModule>> modules;
  const CapabilityRegistry registry = CapabilityRegistry::phase_one();  // 不能对临时对象的成员做范围循环
  for (const auto& descriptor : registry.descriptors()) {
    if (descriptor.id == "color.global") {
      modules.push_back(std::make_unique<ColorGlobalModule>(descriptor));
    } else if (descriptor.id == "detail") {
      modules.push_back(std::make_unique<DetailModule>(descriptor));
    } else {
      modules.push_back(std::make_unique<ParameterBackedModule>(descriptor));
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

}  // namespace pixcake
