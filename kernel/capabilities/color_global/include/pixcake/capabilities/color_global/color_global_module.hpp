#pragma once

#include "pixcake/capability/capability_module.hpp"

namespace pixcake {

// 全图色彩工具（契约 §4 `color.global`）。配置文件决定哪一组参与绘制：黑白配置文件下 HSL、颜色编辑器、
// 鲜艳度和饱和度不画，黑白组画；其他配置文件相反（需求第 10.3 节）。切换只改变哪一边参与编译，不删数据。
class ColorGlobalModule final : public ParameterBackedModule {
 public:
  explicit ColorGlobalModule(CapabilityDescriptor descriptor);

 protected:
  bool draws_parameter(const ParameterReader& reader, const ParameterMode& mode) const override;
};

}  // namespace pixcake
