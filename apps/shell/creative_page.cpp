#include "pages.hpp"
#include "pixcake_theme.hpp"
#include "pixcake_widgets.hpp"
#include "shell_icons.hpp"

#include <QGroupBox>

namespace pixcake::shell {

QWidget* buildCreativePage(const PageContext& context) {
  auto* page = new QWidget;
  page->setObjectName(QStringLiteral("creative-page"));
  auto* outer = new QVBoxLayout(page);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(4);

  auto* top = new QFrame(page);
  top->setObjectName(QStringLiteral("workspace-top"));
  auto* top_layout = new QHBoxLayout(top);
  top_layout->setContentsMargins(8, 6, 8, 6);
  top_layout->addStretch();
  auto* tabs = new QHBoxLayout;
  addPageTabs(tabs, context);
  top_layout->addLayout(tabs);
  top_layout->addStretch();
  for (const auto& entry : ui::shell::page_list("creative-top")) {
    top_layout->addWidget(itemAction("creative-top", entry, top, false, entry.id == "help" ? 16 : 18));
  }
  outer->addWidget(top);

  auto* center = new QHBoxLayout;
  center->setSpacing(4);
  auto* tool_panel = new PixCakeToolbar(QStringLiteral("workspace-tools"), page);
  auto* tool_layout = tool_panel->row();
  tool_layout->setSpacing(4);
  for (const auto& entry : ui::shell::page_list("creative-tools")) {
    tool_layout->addWidget(workspaceTool("creative-tools", entry, tool_panel));
  }
  tool_layout->addStretch();

  auto* canvas = new PixCakePanel(QStringLiteral("creative-canvas"), page);
  auto* canvas_layout = new QVBoxLayout(canvas);
  canvas_layout->addWidget(new PixCakeEmptyState(qtext("creative.empty"), qtext("creative.empty.hint"), canvas), 1);
  center->addWidget(canvas, 1);

  auto* side = new QFrame(page);
  side->setObjectName(QStringLiteral("workspace-side"));
  side->setMinimumWidth(PixCakeTheme::kWorkspaceSideWidth);
  auto* side_layout = new QVBoxLayout(side);
  side_layout->setContentsMargins(14, 12, 14, 12);
  for (const char* region : {"region.creative-toolbox", "region.creative-side", "region.creative-layers"}) {
    auto* group = new QGroupBox(qtext(region), side);
    auto* group_layout = new QVBoxLayout(group);
    if (std::string_view(region) == "region.creative-side") {
      for (const auto& entry : ui::shell::page_list("creative-side")) {
        const bool has_icon = !iconPath("creative-side", entry.id).isEmpty();
        auto* button = has_icon ? itemAction("creative-side", entry, group, false, entry.id == "help" ? 16 : 18)
                                : action(qs(entry.text), group);
        group_layout->addWidget(button);
      }
    } else {
      group_layout->addWidget(emptyState(qtext("common.empty"), QString(), group));
    }
    side_layout->addWidget(group);
  }
  side_layout->addStretch();
  center->addWidget(side);
  outer->insertWidget(1, tool_panel);
  outer->addLayout(center, 1);
  outer->addWidget(thumbnailStrip(page, false));
  return page;
}

}  // namespace pixcake::shell
