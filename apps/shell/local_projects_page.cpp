#include "pages.hpp"

#include "pixcake_widgets.hpp"
#include "shell_icons.hpp"
#include "trash_dialog.hpp"

#include <QLineEdit>
#include <QListWidget>
#include <QMenu>

#include <algorithm>

namespace pixcake::shell {
namespace {

bool is_local_project(const ProjectEntry& project) {
  return project.origin == kOriginLocal || project.origin == kOriginCloudDownloaded;
}

QListWidget* projectList(QWidget* parent, PageActions* actions, const std::vector<ProjectEntry>& projects) {
  auto* list = new QListWidget(parent);
  list->setObjectName(QStringLiteral("local-projects-list"));
  list->setAccessibleName(qtext("region.local-projects-list"));
  list->setViewMode(QListView::IconMode);
  list->setMovement(QListView::Static);
  list->setResizeMode(QListView::Adjust);
  list->setGridSize(QSize(220, 76));
  list->setWordWrap(true);
  list->setSelectionMode(QAbstractItemView::SingleSelection);
  list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  for (const auto& project : projects) {
    QIcon icon;
    if (project.cloud_marked) icon = QIcon(loadAssetPixmap(iconPath("home-nav", "cloud"), 18));
    auto* item = new QListWidgetItem(icon, qs(project.name), list);
    item->setToolTip(project.cloud_marked ? qtext("local-projects.cloud-marked") + qtext("local-projects.separator") + qs(project.name)
                                          : qs(project.name));
    item->setData(Qt::UserRole, QVariant::fromValue<qlonglong>(project.id));
    item->setData(Qt::UserRole + 1, qs(project.name));
  }
  list->setContextMenuPolicy(Qt::CustomContextMenu);
  QObject::connect(list, &QWidget::customContextMenuRequested, list, [list, actions](const QPoint& at) {
    auto* under = list->itemAt(at);
    if (under == nullptr) return;
    QMenu menu(list);
    menu.setObjectName(QStringLiteral("local-project-menu"));
    auto* remove = menu.addAction(qtext("home.project.delete"));
    remove->setObjectName(QStringLiteral("local-project-menu-delete"));
    if (menu.exec(list->viewport()->mapToGlobal(at)) == remove &&
        confirmDeleteProject(list->window(), under->text())) {
      actions->request_delete_project(under->data(Qt::UserRole).toLongLong());
    }
  });
  QObject::connect(list, &QListWidget::itemActivated, list, [actions](QListWidgetItem* item) {
    actions->request_open_project(item->data(Qt::UserRole).toLongLong());
  });
  return list;
}

}  // namespace

QWidget* buildLocalProjectsPage(const PageContext& context) {
  PageActions* actions = &context.actions;
  auto* page = new QWidget;
  page->setObjectName(QStringLiteral("local-projects-page"));
  auto* outer = new QVBoxLayout(page);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(8);

  auto* top = new QFrame(page);
  top->setObjectName(QStringLiteral("local-projects-top"));
  auto* top_layout = new QHBoxLayout(top);
  top_layout->setContentsMargins(18, 12, 18, 12);
  const auto home_item = copyItem("local-projects-nav", "home");
  auto* home = itemAction("local-projects-nav", home_item, top, true);
  home->setObjectName(QStringLiteral("local-projects-home"));
  home->setAccessibleName(qs(home_item.text));
  showNameOnHover(home);
  QObject::connect(home, &QPushButton::clicked, home, [actions] { actions->request_page(Page::Home); });
  top_layout->addWidget(home);
  auto* title = text(qtext("page.local-projects"));
  title->setObjectName(QStringLiteral("local-projects-title"));
  top_layout->addWidget(title);
  top_layout->addStretch();
  auto* search = new PixCakeField(qtext("local-projects.search.name"), top);
  search->setObjectName(QStringLiteral("local-projects-search"));
  search->setPlaceholderText(qtext("local-projects.search.placeholder"));
  search->setMaximumWidth(360);
  top_layout->addWidget(search);
  outer->addWidget(top);

  auto projects = actions->projects();
  projects.erase(std::remove_if(projects.begin(), projects.end(),
                                 [](const ProjectEntry& project) { return !is_local_project(project); }),
                 projects.end());
  auto* body = new QFrame(page);
  body->setObjectName(QStringLiteral("local-projects-body"));
  auto* body_layout = new QVBoxLayout(body);
  body_layout->setContentsMargins(24, 8, 24, 24);
  body_layout->addWidget(text(qtext("region.local-projects-list")));
  if (projects.empty()) {
    body_layout->addWidget(emptyState(qtext("local-projects.empty"), qtext("local-projects.empty.hint"), body), 1);
  } else {
    auto* list = projectList(body, actions, projects);
    QObject::connect(search, &QLineEdit::textChanged, list, [list](const QString& value) {
      const QString needle = value.trimmed();
      for (int row = 0; row < list->count(); ++row) {
        auto* item = list->item(row);
        item->setHidden(!needle.isEmpty() && !item->text().contains(needle, Qt::CaseInsensitive));
      }
    });
    body_layout->addWidget(list, 1);
  }
  outer->addWidget(body, 1);
  return page;
}

}  // namespace pixcake::shell
