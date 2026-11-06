#include "pixcake/capabilities/manual_edge/manual_edge_module.hpp"

#include <utility>

namespace pixcake {

// 祛边（需求第 13.1 节）。笔触会话的主人是 kernel/manual，这里只编译已提交的增量（架构第 196 行）。
std::unique_ptr<CapabilityModule> make_manual_edge_module(CapabilityDescriptor descriptor) {
  // 没有写明依赖检测、素材或纯色背景，也没有跳到创意的入口（架构第 7.2、7.5 节）。
  return std::make_unique<ParameterBackedModule>(std::move(descriptor), ModuleTraits{});
}

}  // namespace pixcake
