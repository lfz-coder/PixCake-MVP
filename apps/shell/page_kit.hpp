#pragma once

// 页面构建的公共部件（代码质量清单 Q-12）：页面文件只画控件，状态变化经 PageActions 交给
// ShellController。文字来自 ui/shell 的文案表，图标按“组/条目编号”查。
#include "pixcake/ui/shell/page_copy.hpp"
#include "pixcake/ui/shell/shell_display.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QWidget>

#include <string>
#include <string_view>

namespace pixcake::shell {

using ui::shell::CopyItem;
using ui::shell::Page;
using ui::shell::ShellDisplay;

// 页面发出的请求。实现方（ShellController）改显示模型；页面不直接改。
class PageActions {
 public:
  virtual ~PageActions() = default;
  virtual void request_page(Page target) = 0;
};

struct PageContext {
  const ShellDisplay& display;
  PageActions& actions;
};

QString qtext(std::string_view key);
QString qs(const std::string& value);
const CopyItem& copyItem(std::string_view list, std::string_view id);

QLabel* text(const QString& value, bool muted = false);
QPushButton* action(const QString& label, QWidget* parent, bool enabled = false);
QPushButton* iconAction(const QString& label, const QString& icon, QWidget* parent, bool enabled = false, int size = 18);
// 文案表里一组中的一项，图标按同一组查。
QPushButton* itemAction(std::string_view list, const CopyItem& item, QWidget* parent, bool enabled = false,
                        int size = 18);
// 图库、精修、创意三个页签。点击只发请求。
void addPageTabs(QHBoxLayout* layout, const PageContext& context);
QFrame* workspaceTool(std::string_view list, const CopyItem& item, QWidget* parent);
QFrame* thumbnailStrip(QWidget* parent, bool library);

}  // namespace pixcake::shell
