#include "pixcake/account/account_session.hpp"
#include "pixcake/instance/single_instance.hpp"
#include "pixcake/ui/panels/slider_spec.hpp"
#include "pixcake/ui/shell/page_copy.hpp"
#include "pixcake/ui/shell/shell_display.hpp"
#include "pixcake_theme.hpp"
#include "pixcake_widgets.hpp"
#include "shell_window.hpp"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QEvent>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSocketNotifier>
#include <QStringList>

#include <cstdio>
#include <memory>

namespace {

using pixcake::ui::shell::Page;
using pixcake::ui::shell::ShellDisplay;
namespace panels = pixcake::ui::panels;

#ifdef PIXCAKE_DEV_PAGES
// 只在开发构建：--page 直接预览某个页面（门禁规则 18）。发布构建由会话决定首屏（安全清单 S-25）。
Page parse_page(const QString& value) {
  if (value == QStringLiteral("home")) return Page::Home;
  if (value == QStringLiteral("library")) return Page::Library;
  if (value == QStringLiteral("refine")) return Page::Refine;
  if (value == QStringLiteral("creative")) return Page::Creative;
  return Page::Login;
}
#endif

QStringList list_texts(std::string_view list) {
  QStringList out;
  for (const auto& entry : pixcake::ui::shell::page_list(list)) out.append(QString::fromStdString(entry.text));
  return out;
}

// 精修某个面板的标签全部按文案表画出，且都禁用。
int check_disabled_labels(const QWidget& window, const char* object_name, std::string_view list) {
  const auto* panel = window.findChild<QWidget*>(QString::fromLatin1(object_name));
  QStringList found;
  if (panel != nullptr) {
    for (const auto* label : panel->findChildren<QLabel*>()) {
      found.append(label->text());
      if (label->isEnabled()) {
        qCritical("%s label enabled", object_name);
        return 3;
      }
    }
  }
  if (panel == nullptr || found != list_texts(list)) {
    qCritical("missing %s", object_name);
    return 3;
  }
  return 0;
}

// 精修色彩面板的滑杆只能来自参数模式：绑定的 95 支与内核规格一致，其余一律“范围待确认”。
int check_refine_sliders(const QWidget& window) {
  const auto* panel = window.findChild<QWidget*>(QStringLiteral("refine-color-labels"));
  if (panel == nullptr) {
    qCritical("missing refine color panel");
    return 4;
  }
  int bound = 0;
  bool exposure_ok = false;
  for (const auto* slider : panel->findChildren<QSlider*>()) {
    const std::string name = slider->accessibleName().toStdString();
    const std::string label = panels::slider_label_of(name);
    if (slider->isEnabled()) {
      qCritical("refine slider enabled: %s", name.c_str());
      return 4;
    }
    if (panels::slider_range_pending(name)) {
      if (slider->maximum() != 0) {
        qCritical("unbound slider has ticks: %s", name.c_str());
        return 4;
      }
      continue;
    }
    auto binding = panels::refine_color_binding(label);
    if (!binding) binding = panels::refine_subgroup_binding(label);
    const auto spec = panels::slider_spec(label, binding);
    if (!spec.bound || name != spec.accessible_name || slider->minimum() != 0 || slider->maximum() != spec.tick_count ||
        slider->value() != spec.default_tick) {
      qCritical("refine slider not from parameter mode: %s", name.c_str());
      return 4;
    }
    ++bound;
    if (binding && binding->parameter_id == "exposure") exposure_ok = true;
  }
  // 顶层影调 14 + 色阶 5 + HSL 24 + 可选颜色 36 + 颜色分级 13（“平衡”未注册）+ 色彩编辑器偏移 3。
  if (bound != 95 || !exposure_ok) {
    qCritical("expected 95 parameter-mode sliders, found %d", bound);
    return 4;
  }
  return 0;
}

// Q-12：页签点击经 ShellController 改显示模型并重画；旧页面删除，新页面出现。
int check_tab_navigation(QWidget& window, Page first_page) {
  if (first_page == Page::Login || first_page == Page::Home) return 0;
  const bool to_refine = first_page == Page::Library;
  const auto& target = pixcake::ui::shell::page_item("page-switch", to_refine ? "refine" : "library");
  const QString before = first_page == Page::Library ? QStringLiteral("library-page")
                         : first_page == Page::Refine ? QStringLiteral("refine-page")
                                                      : QStringLiteral("creative-page");
  const QString after = to_refine ? QStringLiteral("refine-page") : QStringLiteral("library-page");
  auto* tab = window.findChild<QPushButton*>(QStringLiteral("navigate-") + QString::fromStdString(target.text));
  if (tab == nullptr) {
    qCritical("missing page tab");
    return 7;
  }
  tab->click();
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  if (window.findChild<QWidget*>(after) == nullptr || window.findChild<QWidget*>(before) != nullptr) {
    qCritical("page tab did not switch pages");
    return 7;
  }
  return 0;
}

int smoke(QWidget& window, Page first_page) {
  const auto bundled = QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources/assets/");
  for (const auto& relative : pixcake::shell::uploadedShellAssets()) {
    if (!QFileInfo::exists(bundled + relative) || pixcake::shell::loadAssetPixmap(relative, 16).isNull()) {
      qCritical("missing shell asset: %s", qPrintable(relative));
      return 2;
    }
  }
  if (first_page == Page::Refine) {
    if (const int crop = check_disabled_labels(window, "refine-crop-labels", "refine-crop"); crop != 0) return crop;
    if (const int history = check_disabled_labels(window, "refine-history-labels", "refine-history"); history != 0) {
      return history;
    }
    if (const int sliders = check_refine_sliders(window); sliders != 0) return sliders;
  }
  if (const int tabs = check_tab_navigation(window, first_page); tabs != 0) return tabs;
  // 页面用到的文案键都在文案表里（Q-11）。
  for (const auto& key : pixcake::ui::shell::missing_page_copy()) {
    qCritical("page copy key missing: %s", key.c_str());
    return 6;
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  pixcake::shell::PixCakeTheme::apply(app);
  QCoreApplication::setApplicationName(QStringLiteral("PixCake"));
  QCoreApplication::setApplicationVersion(QStringLiteral("0.1"));
  QCommandLineParser parser;
  parser.setApplicationDescription(QStringLiteral("PixCake desktop shell"));
  parser.addHelpOption();
#ifdef PIXCAKE_DEV_PAGES
  QString default_page = QStringLiteral("login");
  const auto executable = QFileInfo(QString::fromLocal8Bit(argv[0])).baseName();
  for (const auto& page : {QStringLiteral("home"), QStringLiteral("library"), QStringLiteral("refine"),
                           QStringLiteral("creative")}) {
    if (executable.endsWith(QStringLiteral("-") + page)) default_page = page;
  }
  QCommandLineOption page_option(QStringList() << QStringLiteral("page"),
                                 QStringLiteral("Development build only: display a read-only shell page: login, "
                                                "home, library, refine, creative."),
                                 QStringLiteral("page"), default_page);
  parser.addOption(page_option);
#endif
  QCommandLineOption smoke_option(QStringList() << QStringLiteral("smoke"),
                                  QStringLiteral("Construct the first page and exit without showing a window."));
  parser.addOption(smoke_option);
  QCommandLineOption instance_option(
      QStringList() << QStringLiteral("instance-dir"),
      QStringLiteral("Directory holding the single-instance lock and socket (default: the application support directory)."),
      QStringLiteral("dir"));
  parser.addOption(instance_option);
  parser.process(app);

  // 需求 §3.2：同一用户只跑一个主实例。抢锁必须在打开任何数据库之前；冒烟检查不抢锁，以便并行运行。
  std::unique_ptr<pixcake::instance::PrimaryInstance> primary;
  if (!parser.isSet(smoke_option)) {
    const std::string instance_dir = parser.isSet(instance_option) ? parser.value(instance_option).toStdString()
                                                                   : pixcake::instance::default_instance_directory();
    auto claim = pixcake::instance::claim_instance(instance_dir);
    if (claim.another_running) {
      const auto notified = pixcake::instance::notify_primary(instance_dir, "activate");
      if (notified != pixcake::ResultCode::Ok) {
        std::fprintf(stderr, "PixCake is already running but did not answer: %s\n", pixcake::result_code_label(notified));
        return 5;
      }
      std::printf("PixCake is already running; activated the existing window.\n");
      return 0;
    }
    if (claim.code != pixcake::ResultCode::Ok || !claim.primary) {
      std::fprintf(stderr, "PixCake could not take the single-instance lock in %s: %s\n", instance_dir.c_str(),
                   pixcake::result_code_label(claim.code));
      return 4;
    }
    primary = std::move(claim.primary);
  }

  // 发布构建：首屏由账号会话决定（需求第 79、103 行，安全清单 S-25）。本机会话缓存接入之前，
  // 账号会话是未登录，首屏是登录页。
  pixcake::account::AccountSession account;
#ifdef PIXCAKE_DEV_PAGES
  const Page first_page = parse_page(parser.value(page_option));
  pixcake::shell::ShellWindow window(first_page == Page::Login ? ShellDisplay::for_session(account)
                                                               : ShellDisplay::dev_preview(first_page));
#else
  const Page first_page = Page::Login;
  pixcake::shell::ShellWindow window(ShellDisplay::for_session(account));
#endif
  if (parser.isSet(smoke_option)) return smoke(window, first_page);
  window.show();
  QSocketNotifier activation(primary->listen_fd(), QSocketNotifier::Read);
  QObject::connect(&activation, &QSocketNotifier::activated, &window, [&window, &primary]() {
    while (const auto message = primary->take_message()) {
      if (*message != "activate") continue;
      window.showNormal();
      window.raise();
      window.activateWindow();
      qInfo("activated by another launch");
    }
  });
  return app.exec();
}
