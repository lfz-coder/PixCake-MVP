#include "pages.hpp"
#include "pixcake_theme.hpp"
#include "pixcake_widgets.hpp"
#include "shell_icons.hpp"

#include <QGroupBox>
#include <QListWidget>
#include <QSlider>

namespace pixcake::shell {

QWidget* buildLibraryPage(const PageContext& context) {
  auto* page = new QWidget;
  page->setObjectName(QStringLiteral("library-page"));
  auto* outer = new QVBoxLayout(page);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(4);

  auto* top = new QFrame(page);
  top->setObjectName(QStringLiteral("workspace-top"));
  auto* top_layout = new QHBoxLayout(top);
  top_layout->setContentsMargins(8, 6, 8, 6);
  const auto& home_item = copyItem("library-nav", "home");
  auto* home = itemAction("library-nav", home_item, top, true);
  home->setObjectName(QStringLiteral("top-action"));
  home->setAccessibleName(qs(home_item.text));
  PageActions* actions = &context.actions;
  QObject::connect(home, &QPushButton::clicked, home, [actions] { actions->request_page(Page::Home); });
  top_layout->addWidget(home);
  top_layout->addWidget(itemAction("library-nav", copyItem("library-nav", "search"), top));
  top_layout->addStretch();
  auto* tabs = new QHBoxLayout;
  addPageTabs(tabs, context);
  top_layout->addLayout(tabs);
  top_layout->addStretch();
  for (const auto& entry : ui::shell::page_list("library-toolbar")) {
    top_layout->addWidget(itemAction("library-toolbar", entry, top));
  }
  outer->addWidget(top);

  auto* controls = new PixCakeStatusStrip(QStringLiteral("workspace-tools"), page);
  auto* controls_layout = controls->row();
  auto* status = action(qtext("region.library-status"), controls);
  status->setAccessibleName(qtext("library.status.accessible"));
  controls_layout->addWidget(status);
  controls_layout->addWidget(iconAction(qtext("region.library-effect"), QString(), controls));
  for (const auto& entry : ui::shell::page_list("library-tools")) {
    controls_layout->addWidget(itemAction("library-tools", entry, controls));
  }
  controls_layout->addStretch();
  controls_layout->addWidget(text(qtext("library.selected"), true));
  controls_layout->addWidget(action(qtext("region.library-delete"), controls));
  outer->addWidget(controls);

  auto* content = new QHBoxLayout;
  content->setSpacing(4);
  auto* grid = new PixCakePanel(QStringLiteral("library-grid"), page);
  auto* grid_layout = new QVBoxLayout(grid);
  grid_layout->setContentsMargins(16, 16, 16, 16);
  const auto photos = actions->photos();
  if (photos.empty()) {
    grid_layout->addWidget(new PixCakeEmptyState(qtext("library.empty"), qtext("library.empty.hint"), grid), 1);
  } else {
    // 当前项目的照片；双击进入精修。缩略图由 renderd 生成，到了就补上。
    auto* list = photoList(grid, actions, true);
    grid_layout->addWidget(text(qtext("library.open.hint"), true));
    grid_layout->addWidget(list, 1);
  }
  if (actions->can_import()) {
    auto* add = action(qtext("library.import"), grid, true);
    add->setObjectName(QStringLiteral("library-import"));
    QObject::connect(add, &QPushButton::clicked, add, [add, actions] { chooseAndImport(add, actions); });
    grid_layout->addWidget(add, 0, Qt::AlignLeft);
  }
  content->addWidget(grid, 1);
  auto* side = new QFrame(page);
  side->setObjectName(QStringLiteral("workspace-side"));
  side->setMinimumWidth(PixCakeTheme::kWorkspaceSideWidth);
  auto* side_layout = new QVBoxLayout(side);
  side_layout->setContentsMargins(14, 12, 14, 12);
  side_layout->addWidget(text(qtext("region.library-side")));
  for (const auto& entry : ui::shell::page_list("library-side")) {
    auto* section = new QGroupBox(qs(entry.text), side);
    auto* section_layout = new QVBoxLayout(section);
    section_layout->addWidget(emptyState(qtext("common.empty"), QString(), section));
    side_layout->addWidget(section);
  }
  side_layout->addStretch();
  content->addWidget(side);
  outer->addLayout(content, 1);

  auto* views = new PixCakeToolbar(QStringLiteral("library-views"), page);
  auto* views_layout = views->row();
  auto* span = new QSlider(Qt::Horizontal, views);
  span->setObjectName(QStringLiteral("library-grid-span"));
  span->setAccessibleName(qtext("library.span"));
  span->setRange(1, 7);
  span->setEnabled(false);
  span->setFixedWidth(120);
  views_layout->insertWidget(0, span);
  for (const auto& entry : ui::shell::page_list("library-views")) {
    views_layout->addWidget(itemAction("library-views", entry, views));
  }
  views_layout->addStretch();
  outer->addWidget(views);
  outer->addWidget(thumbnailStrip(page, true, actions));
  return page;
}

}  // namespace pixcake::shell
