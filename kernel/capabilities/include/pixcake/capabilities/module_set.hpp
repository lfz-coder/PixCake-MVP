#pragma once

#include "pixcake/capability/capability_module.hpp"

#include <string_view>
#include <vector>

namespace pixcake {

// 第一阶段全部能力模块，一个描述表条目一个（契约 §4）。color.global、detail 是 kernel/capabilities/
// 下的专门模块；其余能力暂用 ParameterBackedModule，按描述表和参数模式实现架构 7.2 的操作。
const std::vector<const CapabilityModule*>& phase_one_capability_modules();
const CapabilityModule* find_capability_module(std::string_view capability_id);

}  // namespace pixcake
