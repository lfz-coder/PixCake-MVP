#include "page_kit.hpp"

#include "pixcake/capability/capability_registry.hpp"

#include "pixcake/project/records.hpp"
#include "pixcake_widgets.hpp"
#include "filter_dialog.hpp"
#include "shell_icons.hpp"
#include "trash_dialog.hpp"

#include <QColor>
#include <QStringList>
#include <QFileDialog>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPointer>
#include <QShortcut>

#include <vector>

#include <algorithm>

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

bool reference_drawn(const ui::panels::SliderBinding& binding) {
  if (binding.capability_id == "detail") return binding.parameter_id.rfind("grain_", 0) == 0;
  if (binding.capability_id != "color.global") return false;
  const std::string& id = binding.parameter_id;
  if (id == "profile" || id == "selective_mode") return true;
  for (const char* prefix : {"levels_", "hsl_", "bw_mix_", "selective_", "grading_"}) {
    if (id.rfind(prefix, 0) == 0) return true;
  }
  // Top-level tone and white balance (their parameter ids are their channels).
  for (const char* top : {"temperature", "tint", "exposure", "contrast", "brightness", "highlights", "shadows",
                          "whites", "blacks", "texture", "clarity", "dehaze", "vibrance", "saturation"}) {
    if (id == top) return true;
  }
  return false;
}

std::optional<std::string> parameter_blocked(const PageActions* actions, const ui::panels::SliderBinding& binding) {
  return parameter_disabled_reason(binding.capability_id, binding.parameter_id,
                                   [actions](std::string_view capability, std::string_view parameter) {
                                     return actions->parameter_value({std::string(capability), std::string(parameter)});
                                   });
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

// 标记行：“★3 选取 红”，没有标记的项不写。
QString markLine(const PhotoEntry& photo) {
  QStringList parts;
  if (photo.rating > 0) parts << qtext("mark.star").arg(photo.rating);
  if (!photo.flag.empty()) parts << qs(photo.flag);
  if (!photo.color.empty()) parts << qs(photo.color);
  return parts.join(QLatin1Char(' '));
}

// 色标改变缩略图底色（需求 §6.1）。
QColor colorTint(const std::string& color) {
  if (color == kColorRed) return QColor(120, 40, 40);
  if (color == kColorYellow) return QColor(120, 100, 30);
  if (color == kColorGreen) return QColor(40, 100, 50);
  if (color == kColorBlue) return QColor(40, 70, 130);
  if (color == kColorPurple) return QColor(90, 50, 120);
  return {};
}

// 标记作用的照片：图库是选中的（没选就是当前那张），缩略图条是当前那张。
std::vector<std::int64_t> markTargets(PageActions* actions, bool grid) {
  auto ids = grid ? actions->selected_photos() : std::vector<std::int64_t>{};
  if (ids.empty() && actions->current_photo()) ids = {*actions->current_photo()};
  return ids;
}

// 再按一次同一色标就取消：目标都已经是这个颜色时清除。
std::string toggledColor(PageActions* actions, const std::vector<std::int64_t>& ids, const std::string& color) {
  const auto photos = actions->photos();
  bool all_same = !ids.empty();
  for (const std::int64_t id : ids) {
    const auto found = std::find_if(photos.begin(), photos.end(), [id](const PhotoEntry& photo) { return photo.id == id; });
    all_same = all_same && found != photos.end() && found->color == color;
  }
  return all_same ? std::string() : color;
}

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
      if (photo.copy > 0) label += QStringLiteral("\n") + qtext("library.copy.tag").arg(photo.copy);
      for (const auto* state : {&photo.presence, &photo.edit_status}) {
        if (!state->empty()) label += QStringLiteral("\n") + qs(*state);
      }
      const QString marks = markLine(photo);
      if (!marks.isEmpty()) label += QStringLiteral("\n") + marks;
    }
    auto* item = new QListWidgetItem(thumbnailIcon(actions->thumbnail(photo.id), side), label, list);
    if (const QColor tint = colorTint(photo.color); tint.isValid()) item->setBackground(tint);
    item->setToolTip(qs(photo.file_name));
    item->setData(Qt::UserRole, QVariant::fromValue<qlonglong>(photo.id));
    if (actions->current_photo() == photo.id) list->setCurrentItem(item);
  }
  if (grid) {
    // 图库多选（macOS 常规：单击、⌘ 单击加减、⇧ 单击连选、⌘A、框选；官方手势未核实）。
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    const auto selected = actions->selected_photos();
    for (int row = 0; row < list->count(); ++row) {
      auto* item = list->item(row);
      const auto id = std::int64_t{item->data(Qt::UserRole).toLongLong()};
      item->setSelected(std::find(selected.begin(), selected.end(), id) != selected.end());
    }
  }
  // 右键“创建虚拟副本”（多选时“批量创建虚拟副本”）和 ⌘'：图库按选中的照片，缩略图条按右键点中的那张。
  list->setContextMenuPolicy(Qt::CustomContextMenu);
  QObject::connect(list, &QWidget::customContextMenuRequested, list, [list, actions, grid](const QPoint& at) {
    QListWidgetItem* under = list->itemAt(at);
    if (under == nullptr) return;
    std::vector<std::int64_t> ids;
    if (grid) {
      ids = actions->selected_photos();
      const auto hit = std::int64_t{under->data(Qt::UserRole).toLongLong()};
      if (std::find(ids.begin(), ids.end(), hit) == ids.end()) ids = {hit};
    } else {
      ids = {std::int64_t{under->data(Qt::UserRole).toLongLong()}};
    }
    QMenu menu(list);
    menu.setObjectName(QStringLiteral("photo-menu"));
    QAction* create = menu.addAction(qtext(ids.size() > 1 ? "library.copy.batch" : "library.copy.create"));
    create->setObjectName(QStringLiteral("photo-menu-copy"));
    QMenu* rating_menu = menu.addMenu(qtext("mark.menu.rating"));
    rating_menu->setObjectName(QStringLiteral("photo-menu-rating"));
    for (int stars = 0; stars <= kRatingMax; ++stars) {
      QAction* entry = rating_menu->addAction(stars == 0 ? qtext("mark.none") : qtext("mark.star").arg(stars));
      QObject::connect(entry, &QAction::triggered, list, [actions, ids, stars] { actions->request_rating(ids, stars); });
    }
    QMenu* flag_menu = menu.addMenu(qtext("mark.menu.flag"));
    flag_menu->setObjectName(QStringLiteral("photo-menu-flag"));
    for (const char* flag : {kFlagPick, kFlagExclude, ""}) {
      QAction* entry = flag_menu->addAction(*flag == '\0' ? qtext("mark.remove") : qs(flag));
      QObject::connect(entry, &QAction::triggered, list, [actions, ids, flag] { actions->request_flag(ids, flag); });
    }
    QMenu* color_menu = menu.addMenu(qtext("mark.menu.color"));
    color_menu->setObjectName(QStringLiteral("photo-menu-color"));
    for (const char* color : {kColorRed, kColorYellow, kColorGreen, kColorBlue, kColorPurple, ""}) {
      QAction* entry = color_menu->addAction(*color == '\0' ? qtext("mark.remove") : qs(color));
      QObject::connect(entry, &QAction::triggered, list, [actions, ids, color] { actions->request_color(ids, color); });
    }
    menu.addSeparator();
    QAction* remove = menu.addAction(qtext("library.delete.menu"));
    remove->setObjectName(QStringLiteral("photo-menu-delete"));
    QAction* chosen = menu.exec(list->viewport()->mapToGlobal(at));
    if (chosen == create) actions->request_virtual_copy(ids);
    if (chosen == remove && confirmDeletePhotos(list->window(), ids.size())) actions->request_delete_photos(ids);
  });
  if (grid) {
    auto* shortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+'")), list);
    shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    QObject::connect(shortcut, &QShortcut::activated, list, [actions] {
      const auto ids = actions->selected_photos();
      if (!ids.empty()) actions->request_virtual_copy(ids);
    });
  }
  // 键盘标记（需求 §6.1）：0–5 星级，P 选取、X 排除、U 移除，6–9 红黄绿蓝（再按取消；紫色只能用鼠标）。
  {
    const auto bind = [list, actions, grid](const QString& key, std::function<void(const std::vector<std::int64_t>&)> run) {
      auto* shortcut = new QShortcut(QKeySequence(key), list);
      shortcut->setContext(Qt::WidgetWithChildrenShortcut);
      QObject::connect(shortcut, &QShortcut::activated, list, [actions, grid, run] {
        const auto ids = markTargets(actions, grid);
        if (!ids.empty()) run(ids);
      });
    };
    for (int stars = 0; stars <= kRatingMax; ++stars) {
      bind(QString::number(stars), [actions, stars](const auto& ids) { actions->request_rating(ids, stars); });
    }
    bind(QStringLiteral("P"), [actions](const auto& ids) { actions->request_flag(ids, kFlagPick); });
    bind(QStringLiteral("X"), [actions](const auto& ids) { actions->request_flag(ids, kFlagExclude); });
    bind(QStringLiteral("U"), [actions](const auto& ids) { actions->request_flag(ids, std::string()); });
    const char* keyed[] = {kColorRed, kColorYellow, kColorGreen, kColorBlue};
    for (int index = 0; index < 4; ++index) {
      const std::string color = keyed[index];
      bind(QString::number(6 + index),
           [actions, color](const auto& ids) { actions->request_color(ids, toggledColor(actions, ids, color)); });
    }
  }
  // Grid: a click selects, a double-click (activation) calls `open`; strip: a click calls `open`.
  const auto id_of = [](QListWidgetItem* item) { return std::int64_t{item->data(Qt::UserRole).toLongLong()}; };
  if (grid) {
    QObject::connect(list, &QListWidget::itemSelectionChanged, list, [list, actions, id_of] {
      std::vector<std::int64_t> ids;
      for (int row = 0; row < list->count(); ++row) {
        if (list->item(row)->isSelected()) ids.push_back(id_of(list->item(row)));
      }
      QListWidgetItem* current = list->currentItem();
      actions->select_photos(ids, current != nullptr && current->isSelected() ? id_of(current) : 0);
    });
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
  for (const auto& id : ids) {
    layout->addWidget(filterAction("refine-thumbs", copyItem("refine-thumbs", id), frame, actions));
  }
  if (library) {
    auto* count = action(qtext("thumbs.selected").arg(0), frame);
    count->setObjectName(QStringLiteral("thumbs-selected"));
    if (actions != nullptr) {
      const auto show = [count, actions] { count->setText(qtext("thumbs.selected").arg(actions->selected_photos().size())); };
      show();
      actions->watch_selection(count, show);
    }
    layout->addWidget(count);
  }
  if (actions != nullptr && !actions->photos().empty()) {
    layout->addWidget(photoList(frame, actions, false, std::move(open)), 1);
  } else {
    layout->addStretch();
    layout->addWidget(text(library ? qtext("library.empty") : qtext("thumbs.empty"), true));
  }
  return frame;
}

}  // namespace pixcake::shell
