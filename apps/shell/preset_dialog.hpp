#pragma once

// 预设（需求 §9）：精修功能区里的“我的预设”列表，和“保存新预设”弹窗。文字来自 ui/shell 的文案表。

#include "page_kit.hpp"

namespace pixcake::shell {

// 名称、分类、类型（自动或手改）、功能勾选（已调整 / 全选 / 全部取消）、覆盖旧预设；保存后关闭。
void openSavePresetDialog(QWidget* parent, PageActions* actions);
// “我的预设”列表：双击套用，右键套用、重命名、删除；套用会替换已有色彩时先问。没有预设时写提示。
QWidget* buildPresetList(QWidget* parent, PageActions* actions);
// 套用一条预设；会替换已有色彩时先问，不同意就不动。返回是否执行。
bool applyPresetAsking(QWidget* parent, PageActions* actions, const std::string& id, const QString& name);

}  // namespace pixcake::shell
