#pragma once

// 同步到选中图片（需求 §18）：按一级功能展开，可只勾二级功能；不能同步的项禁用并写原因。
// “保存设置”后执行。文字来自文案表。

#include "page_kit.hpp"

namespace pixcake::shell {

void openSyncDialog(QWidget* parent, PageActions* actions);

}  // namespace pixcake::shell
