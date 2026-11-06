#pragma once

#include "pixcake/capability/capability_module.hpp"

#include <memory>

namespace pixcake {

// `pet` 的能力模块（契约 §4，架构第 7.2 节）。
std::unique_ptr<CapabilityModule> make_pet_module(CapabilityDescriptor descriptor);

}  // namespace pixcake
