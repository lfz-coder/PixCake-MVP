#pragma once

#include "pixcake/capability/capability_module.hpp"

#include <string_view>
#include <vector>

namespace pixcake {

// 第一阶段全部能力模块，一个描述表条目一个（契约 §4）。每个模块来自 kernel/capabilities/<能力>/ 里
// 自己的工厂；没有工厂的标识没有模块。
const std::vector<const CapabilityModule*>& phase_one_capability_modules();
const CapabilityModule* find_capability_module(std::string_view capability_id);

}  // namespace pixcake
