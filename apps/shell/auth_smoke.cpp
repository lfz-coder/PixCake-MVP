#include "auth_smoke.hpp"
#ifdef PIXCAKE_DEV_PAGES
#include "shell_window.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QThread>

namespace pixcake::shell {
namespace {
bool until(const std::function<bool()>& done) {
  QElapsedTimer clock; clock.start();
  while (!done() && clock.elapsed() < 7000) { QCoreApplication::processEvents(); QThread::msleep(5); }
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  return done();
}
}
bool smoke_auth_flow(ShellWindow& window) {
  auto& controller = window.controller();
  const auto check = [&](bool value, const char* stage) {
    if (!value) qWarning("auth smoke stage=%s error=%d page=%d", stage, static_cast<int>(controller.auth_view().error),
                        static_cast<int>(controller.display().page()));
    return value;
  };
  if (!until([&] { return !controller.auth_view().busy; }) || !controller.auth_view().configured) return false;
  const QString original = QStringLiteral("Long cloud test passphrase 2026");
  const QString replacement = QStringLiteral("Replacement cloud passphrase 2026");
  const auto authenticate = [&](int mode, const QString& secret, bool expected = true) {
    auto* modes = window.findChild<QComboBox*>(QStringLiteral("auth-mode"));
    auto* phone = window.findChild<QLineEdit*>(QStringLiteral("login-account-input"));
    auto* code = window.findChild<QLineEdit*>(QStringLiteral("login-code-input"));
    auto* password = window.findChild<QLineEdit*>(QStringLiteral("auth-password"));
    auto* confirm = window.findChild<QLineEdit*>(QStringLiteral("auth-password-confirm"));
    auto* agreement = window.findChild<QCheckBox*>(QStringLiteral("login-agreement"));
    auto* get_code = window.findChild<QPushButton*>(QStringLiteral("get-code"));
    auto* submit = window.findChild<QPushButton*>(QStringLiteral("primary"));
    if (!modes || !phone || !code || !password || !confirm || !agreement || !get_code || !submit) return false;
    modes->setCurrentIndex(mode); phone->setText(QStringLiteral("13800000001")); agreement->setChecked(true);
    if (mode != 1) {
      if (!until([&] { return get_code->isEnabled(); })) return false;
      get_code->click();
      if (!until([&] { return !controller.auth_view().busy && code->text().size() == 6; })) return false;
      if (mode == 2) {
        if (const auto path = qEnvironmentVariable("PIXCAKE_AUTH_SCREENSHOT"); !path.isEmpty()) {
          window.resize(1386, 900); QCoreApplication::processEvents();
          if (!window.grab().save(path)) return false;
        }
      }
    }
    password->setText(secret); confirm->setText(secret);
    if (!until([&] { return submit->isEnabled(); })) return false;
    submit->click();
    if (!until([&] { return !controller.auth_view().busy; })) return false;
    return expected ? controller.display().page() == Page::Home :
      controller.display().page() == Page::Login && controller.auth_view().error == account::AuthError::CredentialsInvalid;
  };
  const auto logout = [&] {
    auto* button = window.findChild<QPushButton*>(QStringLiteral("auth-logout"));
    if (!button || !button->isEnabled()) return false;
    button->click();
    return until([&] { return !controller.auth_view().busy && controller.display().page() == Page::Login; });
  };
  if (!check(authenticate(2, original), "register")) return false;
  controller.request_page(Page::Cloud);
  if (!until([&] { return !controller.cloud_view().busy; }) || controller.cloud_view().spaces.size() != 1) return false;
  if (!check(logout(), "logout-register") || !check(authenticate(1, original), "password") || !check(logout(), "logout-password")) return false;
  if (!check(authenticate(0, QString()), "sms") || !check(logout(), "logout-sms")) return false;
  if (!check(authenticate(3, replacement), "reset") || !check(logout(), "logout-reset")) return false;
  if (!check(authenticate(1, original, false), "old-password") || !check(authenticate(1, replacement), "new-password")) return false;
  return check(logout(), "logout-final");
}
}  // namespace pixcake::shell
#endif
