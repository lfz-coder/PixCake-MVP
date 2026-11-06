#include "page_kit.hpp"

#include "pixcake_widgets.hpp"
#include "shell_icons.hpp"

#include <vector>

namespace pixcake::shell {

QString qtext(std::string_view key) { return QString::fromStdString(ui::shell::page_text(key)); }
QString qs(const std::string& value) { return QString::fromStdString(value); }
const CopyItem& copyItem(std::string_view list, std::string_view id) { return ui::shell::page_item(list, id); }

QLabel* text(const QString& value, bool muted) {
  auto* label = new QLabel(value);
  label->setWordWrap(true);
  if (muted) label->setProperty("muted", true);
  return label;
}

QPushButton* action(const QString& label, QWidget* parent, bool enabled) {
  auto* button = new PixCakeButton(label, parent);
  button->setEnabled(enabled);
  return button;
}

QPushButton* iconAction(const QString& label, const QString& icon, QWidget* parent, bool enabled, int size) {
  auto* button = action(label, parent, enabled);
  if (!icon.isEmpty()) applyAssetIcon(button, icon, size);
  return button;
}

QPushButton* itemAction(std::string_view list, const CopyItem& item, QWidget* parent, bool enabled, int size) {
  return iconAction(qs(item.text), iconPath(list, item.id), parent, enabled, size);
}

void addPageTabs(QHBoxLayout* layout, const PageContext& context) {
  const std::vector<std::pair<std::string, Page>> targets = {
      {"library", Page::Library}, {"refine", Page::Refine}, {"creative", Page::Creative}};
  for (const auto& [id, target] : targets) {
    const QString label = qs(copyItem("page-switch", id).text);
    auto* button = new PixCakeButton(label);
    button->setObjectName(QStringLiteral("navigate-%1").arg(label));
    button->setProperty("selected", context.display.page() == target);
    button->setProperty("page-tab", true);
    const auto icon = iconPath("page-switch", id);
    if (!icon.isEmpty()) applyAssetIcon(button, icon, 18);
    PageActions* actions = &context.actions;
    const Page page = target;
    QObject::connect(button, &QPushButton::clicked, button, [actions, page] { actions->request_page(page); });
    layout->addWidget(button);
  }
}

QFrame* workspaceTool(std::string_view list, const CopyItem& item, QWidget* parent) {
  const QString label = qs(item.text);
  auto* frame = new QFrame(parent);
  frame->setObjectName(QStringLiteral("workspace-tool-button"));
  frame->setAccessibleName(label);
  auto* layout = new QVBoxLayout(frame);
  layout->setContentsMargins(5, 6, 5, 6);
  layout->setSpacing(4);
  const auto asset = iconPath(list, item.id);
  auto* glyph = asset.isEmpty() ? pendingIcon(frame, 20, label + qtext("icon.pending_named"))
                                : assetIcon(asset, 20, label + qtext("icon.suffix"), frame);
  layout->addWidget(glyph, 0, Qt::AlignCenter);
  auto* name = text(label);
  name->setObjectName(QStringLiteral("workspace-tool-label"));
  name->setAlignment(Qt::AlignCenter);
  name->setWordWrap(true);
  layout->addWidget(name);
  return frame;
}

QFrame* thumbnailStrip(QWidget* parent, bool library) {
  auto* frame = new QFrame(parent);
  frame->setObjectName(QStringLiteral("workspace-thumbs"));
  auto* layout = new QHBoxLayout(frame);
  layout->setContentsMargins(10, 8, 10, 8);
  layout->setSpacing(8);
  auto* add = action(qs(copyItem("refine-thumbs", "add").text), frame);
  add->setObjectName(QStringLiteral("icon-button"));
  add->setAccessibleName(qtext("thumbs.add.name"));
  layout->addWidget(add);
  std::vector<std::string> ids;
  if (library) {
    ids = {"filter", "sort", "cull"};
  } else {
    for (const auto& entry : ui::shell::page_list("refine-thumbs")) {
      if (entry.id != "add") ids.push_back(entry.id);
    }
  }
  for (const auto& id : ids) layout->addWidget(itemAction("refine-thumbs", copyItem("refine-thumbs", id), frame));
  if (library) layout->addWidget(action(qtext("thumbs.selected"), frame));
  layout->addStretch();
  layout->addWidget(text(library ? qtext("library.empty") : qtext("thumbs.empty"), true));
  return frame;
}

}  // namespace pixcake::shell
