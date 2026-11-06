#pragma once

#include "pixcake/capability/capability_module.hpp"

#include <memory>

namespace pixcake {

// `manual.liquify` 的能力模块（契约 §4，架构第 7.2 节）。
std::unique_ptr<CapabilityModule> make_manual_liquify_module(CapabilityDescriptor descriptor);

}  // namespace pixcake
