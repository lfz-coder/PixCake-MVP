#include "export_dialog.hpp"
#include "pages.hpp"
#include "pixcake_widgets.hpp"
#include "shell_icons.hpp"
#include "trash_dialog.hpp"

#include <QComboBox>
#include <QFileDialog>
#include <QIcon>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QScrollArea>
#include <QToolTip>

#include <algorithm>
#include <functional>
#include <memory>

namespace pixcake::shell {
void pending(QWidget* widget) {
  QToolTip::showText(widget->mapToGlobal(QPoint(widget->width() / 2, widget->height())), qtext("common.pending"), widget);
}

namespace {
QLabel* title(const QString& value) {
  auto* label = text(value);
  label->setObjectName("home-section-title");
  return label;
}

QPixmap coverPixmap(bool cloud) {
  QPixmap cover(312, 200);
  cover.fill(QColor("#1c1c1c"));
  if (cloud) {
    QPainter painter(&cover);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#bdbdbd"));
    painter.drawEllipse(12, 12, 12, 8);
    painter.drawEllipse(20, 8, 16, 12);
    painter.drawEllipse(30, 12, 12, 8);
  }
  return cover;
}

struct ProjectListState {
  int tab = 0;
  bool sort_enabled = false;
  bool descending = false;
};

struct ProjectList {
  QListWidget* widget;
  std::function<void()> refresh;
};

ProjectList projectList(QWidget* parent, PageActions* actions, QLineEdit* search, QComboBox* scope,
                        std::shared_ptr<ProjectListState> state) {
  auto* list = new QListWidget(parent);
  list->setObjectName("home-projects");
  list->setAccessibleName(qtext("region.home-projects"));
  list->setViewMode(QListView::IconMode);
  list->setMovement(QListView::Static);
  list->setResizeMode(QListView::Adjust);
  list->setGridSize(QSize(166, 158));
  list->setIconSize(QSize(156, 100));
  list->setSpacing(10);
  list->setWordWrap(true);
  list->setSelectionMode(QAbstractItemView::ExtendedSelection);
  list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  list->setContextMenuPolicy(Qt::CustomContextMenu);
  auto rebuild = [list, actions, search, scope, state] {
    list->clear();
    const auto all = actions->projects();
    const QString needle = search->text();
    const int filter = scope->currentIndex();
    std::vector<ProjectEntry> visible;
    for (const auto& project : all) {
      if (state->tab == 2) continue;
      if (state->tab == 1 && project.origin != kOriginLocal) continue;
      if (filter == 1 && project.origin != kOriginLocal && project.origin != kOriginCloudDownloaded) continue;
      if (filter == 2 && !project.cloud_marked) continue;
      if (!needle.isEmpty() && !qs(project.name).contains(needle, Qt::CaseInsensitive)) continue;
      visible.push_back(project);
    }
    if (state->sort_enabled) {
      std::stable_sort(visible.begin(), visible.end(), [state](const auto& a, const auto& b) {
        const int order = QString::compare(qs(a.name), qs(b.name), Qt::CaseInsensitive);
        return state->descending ? order > 0 : order < 0;
      });
    }
    if (visible.empty()) {
      const auto key = state->tab == 2 ? "home.projects.participated.empty"
                                       : all.empty() ? "home.projects.empty" : "home.projects.no_match";
      auto* empty = new QListWidgetItem(qtext(key), list);
      empty->setFlags(Qt::NoItemFlags);
      return;
    }
    for (const auto& project : visible) {
      auto* item = new QListWidgetItem(QIcon(coverPixmap(project.cloud_marked)), qs(project.name), list);
      item->setData(Qt::UserRole, QVariant::fromValue<qlonglong>(project.id));
      item->setToolTip(qs(project.name));
    }
  };
  QObject::connect(search, &QLineEdit::textChanged, list, rebuild);
  QObject::connect(scope, qOverload<int>(&QComboBox::currentIndexChanged), list, rebuild);
  QObject::connect(list, &QListWidget::itemActivated, list, [actions](QListWidgetItem* item) {
    if (item->data(Qt::UserRole).isValid()) actions->request_open_project(item->data(Qt::UserRole).toLongLong());
  });
  QObject::connect(list, &QWidget::customContextMenuRequested, list, [list, actions](const QPoint& at) {
    auto* item = list->itemAt(at);
    if (item == nullptr || !item->data(Qt::UserRole).isValid()) return;
    QMenu menu(list);
    auto* remove = menu.addAction(qtext("home.project.delete"));
    remove->setObjectName("project-menu-delete");
    if (menu.exec(list->viewport()->mapToGlobal(at)) == remove && confirmDeleteProject(list->window(), item->text())) {
      actions->request_delete_project(item->data(Qt::UserRole).toLongLong());
    }
  });
  bindShortcut(list, "home-delete", [list, actions] {
    auto selected = list->selectedItems();
    if (selected.empty() && list->currentItem()) selected << list->currentItem();
    std::vector<std::int64_t> ids;
    for (auto* item : selected) {
      if (item->data(Qt::UserRole).isValid()) ids.push_back(item->data(Qt::UserRole).toLongLong());
    }
    if (ids.empty()) return;
    const QString name = ids.size() == 1 ? selected.front()->text() : qtext("home.project.delete.count").arg(ids.size());
    // Actions can rebuild the page. Keep values, not item pointers, across callbacks.
    if (confirmDeleteProject(list->window(), name)) {
      for (const auto id : ids) actions->request_delete_project(id);
    }
  }, Qt::WidgetShortcut);
  rebuild();
  return {list, rebuild};
}
}  // namespace

QWidget* buildHomePage(const PageContext& context) {
  PageActions* actions = &context.actions;
  auto* page = new QWidget;
  page->setObjectName("home-page");
  auto* root = new QVBoxLayout(page);
  root->setContentsMargins(0, 0, 0, 0);
  auto* right = new QFrame(page);
  right->setObjectName("home-content");
  root->addWidget(right);
  auto* right_layout = new QVBoxLayout(right);
  right_layout->setContentsMargins(20, 12, 20, 0);
  right_layout->setSpacing(10);
  auto* top = new QHBoxLayout;
  auto* search = new PixCakeField(qtext("home.search.name"), right);
  search->setObjectName("home-search");
  search->setPlaceholderText(qtext("home.search.placeholder"));
  search->setFixedSize(320, 30);
  search->addAction(QIcon(loadAssetPixmap(iconPath("global", "search"), 18)), QLineEdit::LeadingPosition);
  top->addStretch();
  top->addWidget(search);
  top->addStretch();
  for (const auto& entry : ui::shell::page_list("home-toolbar")) {
    auto* button = toolbarAction("home-toolbar", entry, right, actions);
    button->setObjectName("home-toolbar");
    if (entry.id == "membership") button->setText(qs(entry.text));
    else button->setToolTip(qs(entry.text));
    if (entry.id == "support" || entry.id == "messages" || entry.id == "transfers") {
      QObject::connect(button, &QPushButton::clicked, button, [button] { pending(button); });
    }
    top->addWidget(button);
  }
  right_layout->addLayout(top);
  auto* scroll = new QScrollArea(right);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto* body = new QWidget;
  auto* main = new QVBoxLayout(body);
  main->setContentsMargins(0, 0, 0, 18);
  main->setSpacing(10);
  main->addWidget(title(qtext("page.home")));
  auto* cards = new QHBoxLayout;
  cards->setSpacing(12);
  auto* import = new PixCakeDropPanel("home-import", body);
  import->setAccessibleName(qtext("home.import.name"));
  import->setMinimumHeight(156);
  auto* import_layout = new QVBoxLayout(import);
  import_layout->setAlignment(Qt::AlignCenter);
  auto* plus = text(qtext("home.import.plus"));
  plus->setAlignment(Qt::AlignCenter);
  plus->setStyleSheet("background:#ffe000;color:#1a1a1a;border-radius:8px;font-size:28px;");
  plus->setFixedSize(36, 36);
  import_layout->addWidget(plus, 0, Qt::AlignCenter);
  import_layout->addWidget(text(qtext("home.import.name")), 0, Qt::AlignCenter);
  import_layout->addWidget(text(qtext("home.import.hint"), true), 0, Qt::AlignCenter);
  if (actions->can_import()) {
    import->onClicked([import, actions] { chooseAndImport(import, actions); });
    bindShortcut(page, "create-project", [import, actions] { chooseAndImport(import, actions); });
    import->onDropped([actions](const std::vector<std::string>& paths) { actions->request_import(paths); });
  }
  import->setContextMenuPolicy(Qt::CustomContextMenu);
  QObject::connect(import, &QWidget::customContextMenuRequested, import, [import, actions](const QPoint& at) {
    if (!actions->can_import()) return;
    QMenu menu(import);
    auto* folder = menu.addAction(qtext("home.import.folder"));
    folder->setObjectName("home-import-folder");
    if (menu.exec(import->mapToGlobal(at)) == folder) {
      const QString directory = QFileDialog::getExistingDirectory(import, qtext("home.import.folder"));
      if (!directory.isEmpty()) actions->request_import({directory.toStdString()});
    }
  });
  cards->addWidget(import, 5);
  for (const auto& entry : ui::shell::page_list("home-cards")) {
    auto* card = new QPushButton(body);
    card->setObjectName("home-card");
    card->setMinimumHeight(156);
    auto* layout = new QVBoxLayout(card);
    layout->setAlignment(Qt::AlignCenter);
    layout->addWidget(pendingIcon(card, 28, qs(entry.text)), 0, Qt::AlignCenter);
    layout->addWidget(text(qs(entry.text)), 0, Qt::AlignCenter);
    const auto subtitle = entry.id == "capture" ? "home.card.capture.subtitle"
                        : entry.id == "toolbox" ? "home.card.toolbox.subtitle" : "home.card.assistant.subtitle";
    layout->addWidget(text(qtext(subtitle), true), 0, Qt::AlignCenter);
    QObject::connect(card, &QPushButton::clicked, card, [card] { pending(card); });
    cards->addWidget(card, 2);
  }
  main->addLayout(cards);
  main->addWidget(title(qtext("region.home-quick")));
  auto* quick = new QHBoxLayout;
  for (const auto& entry : ui::shell::page_list("home-quick")) {
    auto* button = new QPushButton(qs(entry.text), body);
    button->setObjectName("home-quick");
    button->setFixedSize(168, 64);
    QObject::connect(button, &QPushButton::clicked, button, [button] { pending(button); });
    quick->addWidget(button);
  }
  quick->addStretch();
  main->addLayout(quick);
  auto state = std::make_shared<ProjectListState>();
  auto* tools = new QHBoxLayout;
  auto* tabs = new QHBoxLayout;
  tools->addLayout(tabs);
  tools->addStretch();
  auto* scope = new QComboBox(body);
  scope->setObjectName("home-project-scope");
  scope->addItems({qtext("home.projects.all"), qtext("home.projects.local"), qtext("home.projects.cloud")});
  tools->addWidget(scope);
  auto* sort = new QPushButton(qtext("home.projects.name"), body);
  sort->setObjectName("home-project-sort");
  tools->addWidget(sort);
  auto* refresh = new QPushButton(qtext("home.projects.refresh.symbol"), body);
  refresh->setObjectName("home-project-refresh");
  refresh->setAccessibleName(qtext("home.projects.refresh"));
  tools->addWidget(refresh);
  main->addLayout(tools);
  const auto list = projectList(body, actions, search, scope, state);
  main->addWidget(list.widget, 1);
  int index = 0;
  for (const auto& entry : ui::shell::page_list("home-project-tabs")) {
    auto* button = new QPushButton(qs(entry.text), body);
    button->setObjectName("home-project-tab");
    tabs->addWidget(button);
    QObject::connect(button, &QPushButton::clicked, list.widget, [state, index, rebuild = list.refresh] {
      state->tab = index;
      rebuild();
    });
    ++index;
  }
  QObject::connect(sort, &QPushButton::clicked, list.widget, [state, rebuild = list.refresh] {
    state->descending = !state->descending;
    state->sort_enabled = true;
    rebuild();
  });
  QObject::connect(refresh, &QPushButton::clicked, list.widget, list.refresh);
  scroll->setWidget(body);
  right_layout->addWidget(scroll, 1);
  return page;
}
}  // namespace pixcake::shell
