#pragma once

#include "pixcake/capability/capability_module.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace pixcake {

// 第一阶段全部能力模块，一个描述表条目一个（契约 §4）。每个模块来自 kernel/capabilities/<能力>/ 里
// 自己的工厂；没有工厂的标识没有模块。
const std::vector<const CapabilityModule*>& phase_one_capability_modules();
const CapabilityModule* find_capability_module(std::string_view capability_id);

// 预设、批量同步、粘贴：用来源的这一能力替换目标（架构 7.2：提取、迁移经能力模块）。
// - 目标片段比本能力新：keep_target 为真，目标不动。
// - 没有来源：fragment 为空，表示删除目标片段。
// - 来源比本能力新，或标识没有模块：原样复制。
// - 其余：来源经迁移、提取、取值校验后写入；损坏返回 DocumentCorrupt，取值不符合参数模式或迁移失败返回
//   StateConflict，调用方不得改动目标。
struct FragmentTransfer {
  ResultCode code = ResultCode::Ok;
  bool keep_target = false;
  std::optional<FragmentBytes> fragment;
};

FragmentTransfer transfer_fragment(std::string_view capability_id, const std::optional<FragmentBytes>& source,
                                   const std::optional<FragmentBytes>& target);

}  // namespace pixcake
