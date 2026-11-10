#include "pages.hpp"
#include "pixcake_widgets.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QRegularExpressionValidator>
#include <QTimer>
#include <QCoreApplication>
#include <QPixmap>
#include <QStringList>
#include <algorithm>

namespace pixcake::shell {

QWidget* buildLoginPage(const PageContext& context) {
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

  auto* modes = new QComboBox(right);
  modes->setObjectName(QStringLiteral("auth-mode"));
  modes->addItem(qtext("auth.sms-login"));
  modes->addItem(qtext("auth.password-login"));
  modes->addItem(qtext("auth.register"));
  modes->addItem(qtext("auth.reset"));
  panel->addWidget(modes);

  auto* account = new PixCakeField(qtext("region.login-account"), right);
  account->setObjectName(QStringLiteral("login-account-input"));
  account->setPlaceholderText(qtext("login.account.placeholder"));
  account->setMaxLength(11);
  account->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9]{0,11}")), account));
  panel->addWidget(account);

  auto* code = new PixCakeField(qtext("region.login-code"), right);
  code->setObjectName(QStringLiteral("login-code-input"));
  code->setMaxLength(6);
  code->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9]{0,6}")), code));
  code->setPlaceholderText(qtext("login.code.placeholder"));
  auto* code_row = new QWidget(right);
  auto* code_layout = new QHBoxLayout(code_row);
  code_layout->setContentsMargins(0, 0, 0, 0);
  code_layout->addWidget(code, 1);
  auto* get_code = action(qs(copyItem("login-code", "get-code").text), code_row);
  get_code->setObjectName(QStringLiteral("get-code"));
  code_layout->addWidget(get_code);
  panel->addWidget(code_row);
  auto* unavailable = text(qtext("login.unconfigured"), true);
  unavailable->setObjectName(QStringLiteral("login-service-status"));
  unavailable->setWordWrap(true);
  panel->addWidget(unavailable);

  auto* password = new PixCakeField(qtext("auth.password"), right);
  password->setObjectName(QStringLiteral("auth-password"));
  password->setPlaceholderText(qtext("auth.password-policy"));
  password->setEchoMode(QLineEdit::Password);
  password->setMaxLength(128);
  panel->addWidget(password);
  auto* confirmation = new PixCakeField(qtext("auth.confirm-password"), right);
  confirmation->setObjectName(QStringLiteral("auth-password-confirm"));
  confirmation->setEchoMode(QLineEdit::Password);
  confirmation->setMaxLength(128);
  confirmation->setPlaceholderText(qtext("auth.confirm-password"));
  panel->addWidget(confirmation);
  auto* show_password = new QCheckBox(qtext("auth.show-password"), right);
  QObject::connect(show_password, &QCheckBox::toggled, password, [password, confirmation](bool show) {
    password->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
    confirmation->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
  });
  panel->addWidget(show_password);

  auto* login = action(qtext("login.submit"), right);
  login->setObjectName(QStringLiteral("primary"));
  login->setEnabled(false);
  panel->addWidget(login);
  auto* agreement = new QCheckBox(qtext("login.agreement"), right);
  agreement->setObjectName(QStringLiteral("login-agreement"));
  panel->addWidget(agreement);
  auto* remember = new QCheckBox(qtext("auth.remember"), right);
  remember->setObjectName(QStringLiteral("auth-remember"));
  panel->addWidget(remember);
  auto* reconnect = action(qtext("auth.reconnect"), right);
  reconnect->setObjectName(QStringLiteral("auth-reconnect"));
  panel->addWidget(reconnect);
  auto* actions = &context.actions;
  const auto purpose = [modes] { return modes->currentIndex() == 2 ? "register" : modes->currentIndex() == 3 ? "reset" : "login"; };
  QObject::connect(reconnect, &QPushButton::clicked, reconnect, [actions] { actions->request_auth_configure(); });
  QObject::connect(get_code, &QPushButton::clicked, get_code, [=] {
    account::AuthRequest request;
    request.operation = account::AuthOperation::SendCode;
    request.phone = account->text().trimmed().toStdString(); request.purpose = purpose();
    actions->request_auth(std::move(request), remember->isChecked());
  });
  QObject::connect(login, &QPushButton::clicked, login, [=] {
    account::AuthRequest request;
    const account::AuthOperation operations[] = {account::AuthOperation::SmsLogin, account::AuthOperation::PasswordLogin,
                                                account::AuthOperation::Register, account::AuthOperation::ResetPassword};
    request.operation = operations[modes->currentIndex()];
    request.phone = account->text().trimmed().toStdString(); request.purpose = purpose();
    const auto view = actions->auth_view();
    if (view.phone == request.phone && view.purpose == request.purpose) request.challenge_id = view.challenge_id;
    request.code = code->text().toStdString(); request.password = password->text().toStdString();
    actions->request_auth(std::move(request), remember->isChecked());
    password->clear(); confirmation->clear();
  });
  QObject::connect(account, &QLineEdit::textChanged, code, [code] { code->clear(); });
  QObject::connect(modes, &QComboBox::currentIndexChanged, code, [=] { code->clear(); password->clear(); confirmation->clear(); });
  const auto update = [=] {
    const auto view = actions->auth_view();
    const int mode = modes->currentIndex();
    const bool phone_ok = account->text().size() == 11 && account->text().startsWith(QLatin1Char('1'));
    const bool password_mode = mode != 0;
    const bool matches = view.phone == account->text().toStdString() && view.purpose == purpose();
#ifdef PIXCAKE_DEV_PAGES
    if (matches && !view.development_code.empty() && code->property("challenge").toString() != qs(view.challenge_id)) {
      code->setText(qs(view.development_code)); code->setProperty("challenge", qs(view.challenge_id));
    }
#endif
    code_row->setVisible(mode != 1);
    password->setVisible(password_mode); confirmation->setVisible(mode >= 2); show_password->setVisible(password_mode);
    password->setEnabled(!view.busy); confirmation->setEnabled(!view.busy);
    account->setEnabled(!view.busy); code->setEnabled(!view.busy); modes->setEnabled(!view.busy);
    heading->setText(modes->currentText());
    login->setText(mode == 2 ? qtext("auth.register") : mode == 3 ? qtext("auth.reset-submit") : qtext("login.submit"));
    unavailable->setText(qs(ui::shell::auth_status_text(view)));
    const auto seconds = matches ? std::max<std::int64_t>(0, view.resend_at - QDateTime::currentSecsSinceEpoch()) : 0;
    get_code->setText(seconds ? qtext("auth.resend-countdown").arg(seconds) : qs(copyItem("login-code", "get-code").text));
    get_code->setEnabled(view.configured && view.sms_enabled && !view.busy && phone_ok && agreement->isChecked() && !seconds);
    const bool code_ok = mode == 1 || (matches && !view.challenge_id.empty() && code->text().size() == 6);
    const bool password_ok = !password_mode || (!password->text().isEmpty() && (mode == 1 ||
                              (password->text().size() >= 15 && password->text() == confirmation->text())));
    login->setEnabled(view.configured && !view.busy && phone_ok && code_ok && password_ok && agreement->isChecked());
    reconnect->setVisible(!view.configured); reconnect->setEnabled(!view.busy);
  };
  auto* timer = new QTimer(page);
  QObject::connect(timer, &QTimer::timeout, page, update);
  timer->start(100); update();
  panel->addSpacing(12);

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
