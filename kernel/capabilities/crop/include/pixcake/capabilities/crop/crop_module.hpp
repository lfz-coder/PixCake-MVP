#pragma once

#include "pixcake/capability/capability_module.hpp"

#include <memory>

namespace pixcake {

// `crop` 的能力模块（契约 §4，架构第 7.2 节）。
std::unique_ptr<CapabilityModule> make_crop_module(CapabilityDescriptor descriptor);

}  // namespace pixcake
