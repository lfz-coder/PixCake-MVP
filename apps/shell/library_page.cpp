#include "export_dialog.hpp"
#include "pages.hpp"
#include "pixcake_theme.hpp"
#include "pixcake_widgets.hpp"
#include "filter_dialog.hpp"
#include "shell_icons.hpp"
#include "sync_dialog.hpp"
#include "trash_dialog.hpp"

#include <QGroupBox>
#include <QListWidget>
#include <QPointer>
#include <QShortcut>
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
  showNameOnHover(home);
  PageActions* actions = &context.actions;
  QObject::connect(home, &QPushButton::clicked, home, [actions] { actions->request_page(Page::Home); });
  top_layout->addWidget(home);
  auto* search = itemAction("library-nav", copyItem("library-nav", "search"), top);
  showNameOnHover(search);
  top_layout->addWidget(search);
  top_layout->addStretch();
  auto* tabs = new QHBoxLayout;
  addPageTabs(tabs, context);
  top_layout->addLayout(tabs);
  top_layout->addStretch();
  for (const auto& entry : ui::shell::page_list("library-toolbar")) {
    top_layout->addWidget(toolbarAction("library-toolbar", entry, top, actions));
  }
  outer->addWidget(top);

  auto* controls = new PixCakeStatusStrip(QStringLiteral("workspace-tools"), page);
  auto* controls_layout = controls->row();
  auto* status = action(qtext("region.library-status"), controls);
  status->setAccessibleName(qtext("library.status.accessible"));
  controls_layout->addWidget(status);
  controls_layout->addWidget(iconAction(qtext("region.library-effect"), QString(), controls));
  for (const auto& entry : ui::shell::page_list("library-tools")) {
    controls_layout->addWidget(entry.id == "import" ? itemAction("library-tools", entry, controls)
                                                    : filterAction("library-tools", entry, controls, actions));
  }
  controls_layout->addStretch();
  // “已选 N 张（共 M 张）”跟着图库的选择变。
  auto* selected = text(QString(), true);
  selected->setObjectName(QStringLiteral("library-selected"));
  const auto total = actions->photos().size();
  const auto show_selected = [selected, actions, total] {
    selected->setText(qtext("library.selected").arg(actions->selected_photos().size()).arg(total));
  };
  show_selected();
  actions->watch_selection(selected, show_selected);
  controls_layout->addWidget(selected);
  auto* remove = action(qtext("region.library-delete"), controls);
  remove->setObjectName(QStringLiteral("library-delete"));
  remove->setEnabled(!actions->selected_photos().empty());
  actions->watch_selection(remove, [remove, actions] { remove->setEnabled(!actions->selected_photos().empty()); });
  QObject::connect(remove, &QPushButton::clicked, remove, [remove, actions] {
    const auto ids = actions->selected_photos();
    if (!ids.empty() && confirmDeletePhotos(remove->window(), ids.size())) actions->request_delete_photos(ids);
  });
  controls_layout->addWidget(remove);
  outer->addWidget(controls);

  // 图库里：⌘C 复制选中的那张的效果，⌘V 粘贴到选中的全部，⇧⌘S 同步到选中图片（需求 §18）。
  bindShortcut(page, "copy-effect", [actions] { actions->request_copy_effect(); });
  bindShortcut(page, "paste-effect", [actions] { actions->request_paste_effect(); });
  bindShortcut(page, "sync-effect", [page, actions] {
    if (actions->has_photo() || !actions->selected_photos().empty()) openSyncDialog(page, actions);
  });
  bindProjectShortcuts(page, actions);
  bindShortcut(page, "grid-view", [actions] { actions->request_library_view("grid"); });
  bindShortcut(page, "single-view", [actions] { actions->request_library_view("single"); });
  bindShortcut(page, "enter-refine", [actions] {
    if (const auto photo = actions->current_photo()) actions->request_open_photo(*photo);
  });
  bindShortcut(page, "library-delete", [page, actions] {
    const auto ids = actions->selected_photos();
    if (!ids.empty() && confirmDeletePhotos(page->window(), ids.size())) actions->request_delete_photos(ids);
  });

  auto* content = new QHBoxLayout;
  content->setSpacing(4);
  auto* grid = new PixCakePanel(QStringLiteral("library-grid"), page);
  auto* grid_layout = new QVBoxLayout(grid);
  grid_layout->setContentsMargins(16, 16, 16, 16);
  const auto photos = actions->photos();
  const bool single = context.display.library_view() == "single" && actions->has_photo();
  // 图库网格双击照片、单视图里点底部缩略图：都在图库里看大图（与像素蛋糕一致，不进精修）。
  const auto view = [actions](std::int64_t photo) { actions->request_view_photo(photo); };
  if (photos.empty()) {
    grid_layout->addWidget(actions->photo_filtered()
                               ? new PixCakeEmptyState(qtext("library.filtered_empty"), QString(), grid)
                               : new PixCakeEmptyState(qtext("library.empty"), qtext("library.empty.hint"), grid),
                           1);
  } else if (single) {
    // 单视图：选中照片的大图，是 renderd 按已提交编辑渲染的预览。
    auto* image_view = new PixCakeImageView(grid);
    image_view->setObjectName(QStringLiteral("library-single-image"));
    grid_layout->addWidget(image_view, 1);
    QPointer<PixCakeImageView> guard(image_view);
    actions->watch_preview(image_view, [guard](const PreviewView& update) {
      if (!guard) return;
      if (!update.image.isNull()) guard->setImage(update.image, update.canvas_size);
      guard->setStatus(update.status);
    });
  } else {
    // 当前项目的照片：单击选中，双击看大图。缩略图由 renderd 生成，到了就补上。
    auto* list = photoList(grid, actions, true, view);
    bindShortcut(page, "select-all", [list] { list->selectAll(); });
    bindShortcut(page, "deselect", [list] { list->clearSelection(); });
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
    const bool built = entry.id == "grid" || entry.id == "single";  // the filter view is not built yet
    auto* button = itemAction("library-views", entry, views, built && !photos.empty());
    button->setObjectName(QStringLiteral("library-view-") + qs(entry.id));
    button->setProperty("selected", context.display.library_view() == entry.id);
    if (built) {
      const std::string id = entry.id;
      QObject::connect(button, &QPushButton::clicked, button, [actions, id] { actions->request_library_view(id); });
    }
    views_layout->addWidget(button);
  }
  views_layout->addStretch();
  outer->addWidget(views);
  // 底部缩略图条：单视图里换大图，网格里选中。
  outer->addWidget(thumbnailStrip(page, true, actions, [actions, single](std::int64_t photo) {
    if (single) {
      actions->request_view_photo(photo);
    } else {
      actions->select_photo(photo);
    }
  }));
  return page;
}

}  // namespace pixcake::shell
