#pragma once

// 筛选与排序弹窗（需求 §6.2、§6.3）：条件一改就生效（取交集），“重置”清空全部条件。文字来自文案表。

#include "page_kit.hpp"

namespace pixcake::shell {

void openFilterDialog(QWidget* parent, PageActions* actions);
// 图库和精修缩略图条里的“筛选”“排序”按钮：打开弹窗；有条件时按钮标成选中。其他按钮返回禁用的占位。
QPushButton* filterAction(std::string_view list, const CopyItem& item, QWidget* parent, PageActions* actions);

}  // namespace pixcake::shell
