#pragma once

// 删除确认与回收站（需求 §7）。回收站是一个模态弹窗：项目、图片、虚拟副本各一行，
// 写着所在项目和还剩几天；右键或按钮“还原”，项目可“彻底删除”。文字来自 ui/shell 的文案表。

#include "page_kit.hpp"

namespace pixcake::shell {

// 问“确认删除”。count 张图片；返回是否确认。模态。
bool confirmDeletePhotos(QWidget* parent, std::size_t count);
bool confirmDeleteProject(QWidget* parent, const QString& name);
void openTrashDialog(QWidget* parent, PageActions* actions);

}  // namespace pixcake::shell
