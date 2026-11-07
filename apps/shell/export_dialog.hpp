#pragma once

// 导出弹窗与导出列表（需求第 19 节）。弹窗只收集选择，经 PageActions 交给 ShellController；
// 文字来自 ui/shell 的文案表。

#include "page_kit.hpp"

#include <QPushButton>

#include <string_view>

namespace pixcake::shell {

// 快速导出、自定设置两页可用；排版印刷页签在，置灰并说明原因。模态。
void openExportDialog(QWidget* parent, PageActions* actions);
// 导出列表：文件名、状态、失败原因、导出位置，失败的可重试、排队的可取消。非模态，只开一个。
void openExportList(QWidget* parent, PageActions* actions);
// 顶栏按钮：“导出”打开导出弹窗（有选中照片时可用），“导出列表”打开导出列表（导出可用时）；
// 其余按钮照旧（禁用）。
QPushButton* toolbarAction(std::string_view list, const CopyItem& item, QWidget* parent, PageActions* actions);

}  // namespace pixcake::shell
