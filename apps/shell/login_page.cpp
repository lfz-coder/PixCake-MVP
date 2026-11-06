#include "pages.hpp"
#include "pixcake_widgets.hpp"

#include <QCheckBox>
#include <QCoreApplication>
#include <QPixmap>
#include <QStringList>

namespace pixcake::shell {

QWidget* buildLoginPage(const PageContext& context) {
  (void)context;
  auto* page = new QWidget;
  page->setObjectName(QStringLiteral("login-page"));
  auto* outer = new QHBoxLayout(page);
  outer->setContentsMargins(32, 32, 32, 26);
  outer->setSpacing(28);

  auto* hero = new QLabel(page);
  hero->setObjectName(QStringLiteral("login-hero"));
  hero->setAlignment(Qt::AlignCenter);
  const auto app_dir = QCoreApplication::applicationDirPath();
  const QStringList hero_paths = {app_dir + QStringLiteral("/../Resources/login-hero.png"),
                                  app_dir + QStringLiteral("/login-hero.png"),
                                  app_dir + QStringLiteral("/../../login-hero.png")};
  QPixmap hero_pix;
  for (const auto& path : hero_paths) {
    hero_pix.load(path);
    if (!hero_pix.isNull()) break;
  }
  if (!hero_pix.isNull()) {
    hero->setPixmap(hero_pix.scaled(620, 760, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
  }
  hero->setMinimumWidth(470);
  hero->setMaximumWidth(690);
  outer->addWidget(hero, 11);

  auto* right = new QWidget(page);
  right->setObjectName(QStringLiteral("login-panel"));
  right->setMinimumWidth(520);
  auto* panel = new QVBoxLayout(right);
  panel->setContentsMargins(84, 0, 84, 0);
  panel->setSpacing(14);
  panel->addStretch(1);

  auto* heading = text(qs(copyItem("login-methods", "sms").text));
  heading->setAlignment(Qt::AlignCenter);
  heading->setObjectName(QStringLiteral("login-heading"));
  QFont heading_font = heading->font();
  heading_font.setPointSize(21);
  heading->setFont(heading_font);
  panel->addWidget(heading);

  auto* account = new PixCakeField(qtext("region.login-account"), right);
  account->setObjectName(QStringLiteral("login-account-input"));
  account->setPlaceholderText(qtext("login.account.placeholder"));
  panel->addWidget(account);

  auto* code = new PixCakeField(qtext("region.login-code"), right);
  code->setObjectName(QStringLiteral("login-code-input"));
  code->setMaxLength(6);
  code->setPlaceholderText(qtext("login.code.placeholder"));
  auto* code_row = new QWidget(right);
  auto* code_layout = new QHBoxLayout(code_row);
  code_layout->setContentsMargins(0, 0, 0, 0);
  code_layout->addWidget(code, 1);
  auto* get_code = action(qs(copyItem("login-code", "get-code").text), code_row);
  get_code->setObjectName(QStringLiteral("get-code"));
  code_layout->addWidget(get_code);
  panel->addWidget(code_row);

  auto* login = action(qtext("login.submit"), right);
  login->setObjectName(QStringLiteral("primary"));
  login->setEnabled(false);
  panel->addWidget(login);
  auto* agreement = new QCheckBox(qtext("login.agreement"), right);
  agreement->setObjectName(QStringLiteral("login-agreement"));
  panel->addWidget(agreement);
  panel->addSpacing(50);

  auto* divider_row = new QHBoxLayout;
  divider_row->setSpacing(12);
  divider_row->addWidget(horizontalRule(right), 1);
  auto* or_label = text(qtext("login.or"), true);
  or_label->setAlignment(Qt::AlignCenter);
  divider_row->addWidget(or_label);
  divider_row->addWidget(horizontalRule(right), 1);
  panel->addLayout(divider_row);

  auto* methods = new QHBoxLayout;
  methods->setSpacing(10);
  auto* wechat = action(qs(copyItem("login-methods", "wechat").text), right);
  wechat->setObjectName(QStringLiteral("login-wechat"));
  wechat->setProperty("variant", QStringLiteral("pill"));
  wechat->setEnabled(false);
  applyAssetIcon(wechat, QStringLiteral("wechat.svg"), 20);
  auto* merchant = action(qtext("login.merchant.button"), right);
  merchant->setObjectName(QStringLiteral("login-merchant"));
  merchant->setProperty("variant", QStringLiteral("pill"));
  merchant->setEnabled(false);
  applyAssetIcon(merchant, QStringLiteral("merchant.svg"), 20);
  methods->addStretch();
  methods->addWidget(wechat);
  methods->addWidget(merchant);
  methods->addStretch();
  panel->addLayout(methods);
  panel->addStretch(1);

  QStringList help_entries;
  for (const auto& entry : ui::shell::page_list("login-help")) help_entries.append(qs(entry.text));
  auto* help = action(QStringLiteral("?"), right);
  help->setObjectName(QStringLiteral("login-help"));
  help->setAccessibleName(qtext("region.login-help"));
  help->setToolTip(help_entries.join(QStringLiteral(" / ")));
  applyAssetIcon(help, QStringLiteral("help.svg"), 16);
  if (!help->icon().isNull()) help->setText(QString());
  auto* help_row = new QHBoxLayout;
  help_row->addStretch();
  help_row->addWidget(help);
  help_row->setContentsMargins(0, 0, 8, 16);
  panel->addLayout(help_row);
  outer->addWidget(right, 13);
  return page;
}

}  // namespace pixcake::shell
