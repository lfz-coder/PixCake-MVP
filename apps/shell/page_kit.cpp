#include "page_kit.hpp"

#include "pixcake_widgets.hpp"
#include "shell_icons.hpp"

#include <QFileDialog>
#include <QListWidget>
#include <QPainter>
#include <QPointer>

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

void chooseAndImport(QWidget* parent, PageActions* actions) {
  const QStringList chosen =
      QFileDialog::getOpenFileNames(parent, qtext("home.import.dialog"), QString(), qtext("home.import.filter"));
  std::vector<std::string> paths;
  for (const auto& path : chosen) paths.push_back(path.toStdString());
  if (!paths.empty()) actions->request_import(paths);
}

namespace {

// Thumbnails keep their aspect inside a fixed square so the grid stays even.
QIcon thumbnailIcon(const QImage& image, int side) {
  QPixmap canvas(side * 2, side * 2);
  canvas.setDevicePixelRatio(2.0);
  canvas.fill(Qt::transparent);
  if (!image.isNull()) {
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    QSizeF size = image.size();
    size.scale(side, side, Qt::KeepAspectRatio);
    painter.drawImage(QRectF(QPointF((side - size.width()) / 2, (side - size.height()) / 2), size), image);
  }
  return QIcon(canvas);
}

}  // namespace

QListWidget* photoList(QWidget* parent, PageActions* actions, bool grid, std::function<void(std::int64_t)> open) {
  const int side = grid ? 140 : 56;
  auto* list = new QListWidget(parent);
  list->setObjectName(grid ? QStringLiteral("library-photos") : QStringLiteral("thumbs-photos"));
  list->setAccessibleName(qtext("library.open.hint"));
  list->setViewMode(QListView::IconMode);
  list->setMovement(QListView::Static);
  list->setIconSize(QSize(side, side));
  list->setHorizontalScrollBarPolicy(grid ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
  if (grid) {
    list->setResizeMode(QListView::Adjust);
    list->setGridSize(QSize(side + 24, side + 70));  // icon + two lines (name, state)
    list->setWordWrap(true);
  } else {
    list->setFlow(QListView::LeftToRight);
    list->setWrapping(false);
    list->setFixedHeight(side + 22);
    list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  }
  for (const auto& photo : actions->photos()) {
    QString label;
    if (grid) {
      label = qs(photo.file_name);
      for (const auto* state : {&photo.presence, &photo.edit_status}) {
        if (!state->empty()) label += QStringLiteral("\n") + qs(*state);
      }
    }
    auto* item = new QListWidgetItem(thumbnailIcon(actions->thumbnail(photo.id), side), label, list);
    item->setToolTip(qs(photo.file_name));
    item->setData(Qt::UserRole, QVariant::fromValue<qlonglong>(photo.id));
    if (actions->current_photo() == photo.id) list->setCurrentItem(item);
  }
  // Grid: a click selects, a double-click (activation) calls `open`; strip: a click calls `open`.
  const auto id_of = [](QListWidgetItem* item) { return std::int64_t{item->data(Qt::UserRole).toLongLong()}; };
  if (grid) {
    QObject::connect(list, &QListWidget::itemClicked, list,
                     [actions, id_of](QListWidgetItem* item) { actions->select_photo(id_of(item)); });
    QObject::connect(list, &QListWidget::itemActivated, list,
                     [open, id_of](QListWidgetItem* item) { if (open) open(id_of(item)); });
  } else {
    QObject::connect(list, &QListWidget::itemClicked, list,
                     [open, id_of](QListWidgetItem* item) { if (open) open(id_of(item)); });
  }
  QPointer<QListWidget> guard(list);
  actions->watch_thumbnails(list, [guard, side](std::int64_t photo_id, const QImage& image) {
    if (!guard) return;
    for (int row = 0; row < guard->count(); ++row) {
      auto* item = guard->item(row);
      if (item->data(Qt::UserRole).toLongLong() == photo_id) item->setIcon(thumbnailIcon(image, side));
    }
  });
  return list;
}

QFrame* thumbnailStrip(QWidget* parent, bool library, PageActions* actions, std::function<void(std::int64_t)> open) {
  auto* frame = new QFrame(parent);
  frame->setObjectName(QStringLiteral("workspace-thumbs"));
  auto* layout = new QHBoxLayout(frame);
  layout->setContentsMargins(10, 8, 10, 8);
  layout->setSpacing(8);
  const bool can_import = actions != nullptr && actions->can_import();
  auto* add = action(qs(copyItem("refine-thumbs", "add").text), frame, can_import);
  add->setObjectName(QStringLiteral("icon-button"));
  add->setAccessibleName(qtext("thumbs.add.name"));
  if (can_import) QObject::connect(add, &QPushButton::clicked, add, [add, actions] { chooseAndImport(add, actions); });
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
  if (actions != nullptr && !actions->photos().empty()) {
    layout->addWidget(photoList(frame, actions, false, std::move(open)), 1);
  } else {
    layout->addStretch();
    layout->addWidget(text(library ? qtext("library.empty") : qtext("thumbs.empty"), true));
  }
  return frame;
}

}  // namespace pixcake::shell
