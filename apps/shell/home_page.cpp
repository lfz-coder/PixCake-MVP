#include "pages.hpp"
#include "pixcake_widgets.hpp"
#include "shell_icons.hpp"

#include <QIcon>
#include <QLineEdit>

namespace pixcake::shell {
namespace {

QFrame* sectionCard(QWidget* parent, const QString& empty_text, bool row) {
  auto* card = new QFrame(parent);
  card->setObjectName(QStringLiteral("home-card"));
  QBoxLayout* inner = row ? static_cast<QBoxLayout*>(new QHBoxLayout(card)) : new QVBoxLayout(card);
  inner->setContentsMargins(16, row ? 12 : 20, 16, row ? 12 : 20);
  if (row) {
    inner->addWidget(text(empty_text, true));
    inner->addStretch();
  } else {
    inner->addWidget(text(empty_text, true), 1, Qt::AlignCenter);
  }
  return card;
}

QLabel* sectionTitle(const QString& label) {
  auto* title = text(label);
  title->setObjectName(QStringLiteral("home-section-title"));
  return title;
}

}  // namespace

QWidget* buildHomePage(const PageContext& context) {
  (void)context;
  auto* page = new QWidget;
  page->setObjectName(QStringLiteral("home-page"));
  auto* outer = new QHBoxLayout(page);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(0);

  auto* nav = new QFrame(page);
  nav->setObjectName(QStringLiteral("home-sidebar"));
  nav->setFixedWidth(220);
  auto* nav_layout = new QVBoxLayout(nav);
  nav_layout->setContentsMargins(18, 20, 18, 18);
  nav_layout->setSpacing(6);
  auto* brand_row = new QWidget(nav);
  auto* brand_layout = new QHBoxLayout(brand_row);
  brand_layout->setContentsMargins(0, 0, 0, 0);
  brand_layout->setSpacing(8);
  brand_layout->addWidget(assetIcon(QStringLiteral("pixcake-logo.svg"), 24, qtext("app.title"), brand_row));
  auto* brand = text(qtext("app.title"));
  brand->setObjectName(QStringLiteral("home-brand"));
  brand_layout->addWidget(brand);
  brand_layout->addStretch();
  nav_layout->addWidget(brand_row);
  nav_layout->addSpacing(18);
  for (const auto& entry : ui::shell::page_list("home-nav")) {
    auto* button = itemAction("home-nav", entry, nav, entry.id == "home");
    button->setObjectName(QStringLiteral("home-nav"));
    button->setProperty("selected", entry.id == "home");
    nav_layout->addWidget(button);
  }
  nav_layout->addStretch();
  nav_layout->addWidget(action(qtext("home.more_versions"), nav));
  outer->addWidget(nav);

  auto* content = new QFrame(page);
  content->setObjectName(QStringLiteral("home-content"));
  auto* main = new QVBoxLayout(content);
  main->setContentsMargins(24, 16, 24, 18);
  main->setSpacing(12);
  auto* top = new QHBoxLayout;
  auto* search = new PixCakeField(qtext("home.search.name"), content);
  search->setObjectName(QStringLiteral("home-search"));
  search->setPlaceholderText(qtext("home.search.placeholder"));
  search->setMaximumWidth(420);
  const auto search_icon = loadAssetPixmap(iconPath("global", "search"), 18);
  if (!search_icon.isNull()) search->addAction(QIcon(search_icon), QLineEdit::LeadingPosition);
  top->addStretch();
  top->addWidget(search);
  top->addStretch();
  for (const auto& entry : ui::shell::page_list("home-toolbar")) {
    auto* button = itemAction("home-toolbar", entry, content);
    button->setObjectName(QStringLiteral("home-toolbar"));
    button->setToolTip(qs(entry.text));
    top->addWidget(button);
  }
  main->addLayout(top);
  main->addWidget(sectionTitle(qs(copyItem("home-nav", "home").text)));

  auto* hero_row = new QHBoxLayout;
  hero_row->setSpacing(12);
  auto* import = new PixCakePanel(QStringLiteral("home-import"), content);
  import->setAccessibleName(qtext("home.import.name"));
  auto* import_layout = new QVBoxLayout(import);
  import_layout->setContentsMargins(24, 14, 24, 14);
  import_layout->setAlignment(Qt::AlignCenter);
  auto* plus = text(qtext("home.import.plus"));
  plus->setAlignment(Qt::AlignCenter);
  QFont plus_font = plus->font();
  plus_font.setPointSize(30);
  plus->setFont(plus_font);
  import_layout->addWidget(plus);
  auto* import_title = text(qtext("home.import.name"));
  import_title->setAlignment(Qt::AlignCenter);
  import_layout->addWidget(import_title);
  import_layout->addWidget(text(qtext("home.import.hint"), true), 0, Qt::AlignCenter);
  hero_row->addWidget(import, 5);
  for (const auto& entry : ui::shell::page_list("home-cards")) {
    const QString label = qs(entry.text);
    auto* card = new QFrame(content);
    card->setObjectName(QStringLiteral("home-card"));
    card->setMinimumHeight(116);
    card->setAccessibleName(label);
    auto* card_layout = new QVBoxLayout(card);
    card_layout->setAlignment(Qt::AlignCenter);
    const auto asset = iconPath("home-cards", entry.id);
    auto* icon = asset.isEmpty() ? pendingIcon(card, 28, label + qtext("icon.pending_named"))
                                 : assetIcon(asset, 28, label + qtext("icon.suffix"), card);
    card_layout->addWidget(icon, 0, Qt::AlignCenter);
    auto* title = text(label);
    title->setObjectName(QStringLiteral("home-card-title"));
    title->setAlignment(Qt::AlignCenter);
    card_layout->addWidget(title);
    card_layout->addWidget(text(qtext("common.empty"), true), 0, Qt::AlignCenter);
    hero_row->addWidget(card, 1);
  }
  main->addLayout(hero_row);

  main->addWidget(sectionTitle(qtext("region.home-common")));
  main->addWidget(sectionCard(content, qtext("common.empty"), false));
  main->addWidget(sectionTitle(qtext("region.home-quick")));
  main->addWidget(sectionCard(content, qtext("home.quick.empty"), true));
  main->addWidget(sectionTitle(qtext("region.home-projects")));
  main->addWidget(sectionCard(content, qtext("home.projects.empty"), false), 1);
  outer->addWidget(content, 1);
  return page;
}

}  // namespace pixcake::shell
