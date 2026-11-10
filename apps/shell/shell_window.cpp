#include "shell_window.hpp"

#include "pages.hpp"
#include "pixcake_widgets.hpp"
#include "shell_icons.hpp"
#include "trash_dialog.hpp"

#include <QApplication>
#include <QFileOpenEvent>
#include <QFont>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>

namespace pixcake::shell {

namespace {
QFrame* buildNavigation(QWidget* parent, PageActions* actions) {
  auto* nav = new QFrame(parent);
  nav->setObjectName(QStringLiteral("home-sidebar"));
  nav->setFixedWidth(208);
  auto* layout = new QVBoxLayout(nav);
  layout->setContentsMargins(14, 16, 14, 14);
  layout->setSpacing(4);
  auto* brand_row = new QHBoxLayout;
  brand_row->setSpacing(8);
  brand_row->addWidget(assetIcon(QStringLiteral("pixcake-logo.svg"), 22, qtext("app.title"), nav));
  auto* brand = text(qtext("app.title"));
  brand->setObjectName(QStringLiteral("home-brand"));
  brand_row->addWidget(brand);
  brand_row->addStretch();
  layout->addLayout(brand_row);
  layout->addSpacing(12);
  for (const auto& entry : ui::shell::page_list("home-nav")) {
    auto* button = itemAction("home-nav", entry, nav, true, 16);
    button->setObjectName(QStringLiteral("home-nav"));
    button->setProperty("page-id", QString::fromStdString(entry.id));
    if (entry.id == "trash") {
      QObject::connect(button, &QPushButton::clicked, button, [button, actions] { openTrashDialog(button, actions); });
    } else if (entry.id == "local") {
      QObject::connect(button, &QPushButton::clicked, button, [actions] { actions->request_page(Page::LocalProjects); });
    } else if (entry.id == "cloud") {
      QObject::connect(button, &QPushButton::clicked, button, [actions] { actions->request_page(Page::Cloud); });
    } else if (entry.id == "home") {
      QObject::connect(button, &QPushButton::clicked, button, [actions] { actions->request_page(Page::Home); });
    } else if (entry.id != "home") {
      QObject::connect(button, &QPushButton::clicked, button, [button] { pending(button); });
    }
    layout->addWidget(button);
  }
  layout->addStretch();
  auto* logout = action(qtext("auth.logout"), nav);
  logout->setObjectName(QStringLiteral("auth-logout"));
  QObject::connect(logout, &QPushButton::clicked, logout, [actions] { actions->request_logout(); });
  layout->addWidget(logout);
  auto* more = action(qtext("home.more_versions"), nav);
  QObject::connect(more, &QPushButton::clicked, more, [more] { pending(more); });
  layout->addWidget(more);
  auto* promo = new QPushButton(qtext("home.promo"), nav);
  promo->setObjectName(QStringLiteral("home-promo"));
  promo->setAccessibleName(qtext("home.promo.accessible"));
  promo->setFixedHeight(86);
  QObject::connect(promo, &QPushButton::clicked, promo, [promo] { pending(promo); });
  layout->addWidget(promo);
  return nav;
}
}

ShellWindow::ShellWindow(ShellDisplay display) : controller_(std::move(display)) {
  setWindowTitle(qtext("app.title"));
  setMinimumSize(1024, 680);
  resize(1386, 800);
  auto* root = new QWidget(this);
  root->setObjectName(QStringLiteral("shell-root"));
  auto* layout = new QVBoxLayout(root);
  layout->setContentsMargins(0, 0, 0, 0);
  title_ = text(qtext("page.login"));
  title_->setObjectName(QStringLiteral("page-title"));
  QFont font = title_->font();
  font.setPointSize(20);
  font.setBold(true);
  title_->setFont(font);
  title_->setVisible(false);
  body_ = new QWidget(root);
  layout->addWidget(body_, 1);
  navigation_ = buildNavigation(body_, &controller_);
  navigation_->hide();
  setCentralWidget(root);
  controller_.on_changed([this] { render(); });
  render();
  // macOS delivers files dropped on the Dock icon or the .app in Finder (and
  // "Open With") to the application as file-open events.
  icon_drop_timer_ = new QTimer(this);
  icon_drop_timer_->setSingleShot(true);
  icon_drop_timer_->setInterval(150);
  connect(icon_drop_timer_, &QTimer::timeout, this, [this] { flushIconDrop(); });
  qApp->installEventFilter(this);
}

bool ShellWindow::eventFilter(QObject* watched, QEvent* event) {
  if (watched != qApp || event->type() != QEvent::FileOpen) return QMainWindow::eventFilter(watched, event);
  const auto* open = static_cast<QFileOpenEvent*>(event);
  const QString path = open->file().isEmpty() ? open->url().toLocalFile() : open->file();
  if (path.isEmpty()) return true;
  // B-71: a modal layer open at the moment of the drop keeps the window as it is.
  if (QApplication::activeModalWidget() != nullptr) {
    qInfo("icon drop ignored: a modal layer is open");
    return true;
  }
  icon_drop_.push_back(path.toStdString());
  icon_drop_timer_->start();
  return true;
}

void ShellWindow::flushIconDrop() {
  std::vector<std::string> paths;
  paths.swap(icon_drop_);
  const bool modal_open = QApplication::activeModalWidget() != nullptr;
  const auto action = controller_.drop_on_app_icon(paths, modal_open);
  if (action == ui::shell::IconDropAction::Ignore) {
    qInfo("icon drop of %zu file(s) ignored on this page", paths.size());
    return;
  }
  qInfo("icon drop of %zu file(s): %s", paths.size(),
        action == ui::shell::IconDropAction::NewProject ? "new project" : "appended to the project");
  showNormal();
  raise();
  activateWindow();
}

ResultCode ShellWindow::openPhoto(const std::string& photo_path,
                                  const preview_channel::PreviewSessionOptions& options, std::string& diagnostic) {
  const ResultCode code = controller_.open_photo(photo_path, options, diagnostic);
  render();
  return code;
}

void ShellWindow::render() {
  const ShellDisplay& display = controller_.display();
  if (display.page() == Page::Refine && (!rendered_page_ || *rendered_page_ != Page::Refine)) {
    prepareRefinePageEntry();
  }
  rendered_page_ = display.page();
  title_->setText(QString::fromUtf8(ui::shell::page_label(display.page())));
  const PageContext context{display, controller_};
  QWidget* page = nullptr;
  switch (display.page()) {
    case Page::Login:
      page = buildLoginPage(context);
      break;
    case Page::Home:
      page = buildHomePage(context);
      break;
    case Page::LocalProjects:
      page = buildLocalProjectsPage(context);
      break;
    case Page::Cloud:
      page = buildCloudPage(context);
      break;
    case Page::Library:
      page = buildLibraryPage(context);
      break;
    case Page::Refine:
      page = buildRefinePage(context);
      break;
    case Page::Creative:
      page = buildCreativePage(context);
      break;
  }
  updateNavigation(display.page());
  setBody(page, display.page() == Page::Home || display.page() == Page::LocalProjects || display.page() == Page::Cloud);
}

void ShellWindow::updateNavigation(Page page) {
  if (auto* logout = navigation_->findChild<QPushButton*>(QStringLiteral("auth-logout"))) {
    logout->setEnabled(controller_.auth_view().signed_in);
  }
  const bool visible = page == Page::Home || page == Page::LocalProjects || page == Page::Cloud;
  navigation_->setVisible(visible);
  if (!visible) return;
  const QString selected_id = page == Page::Home ? QStringLiteral("home") :
                              page == Page::Cloud ? QStringLiteral("cloud") : QStringLiteral("local");
  for (auto* button : navigation_->findChildren<QPushButton*>(QStringLiteral("home-nav"))) {
    const QString id = button->property("page-id").toString();
    const bool selected = id == selected_id;
    if (button->property("selected").toBool() == selected) continue;
    button->setProperty("selected", selected);
    button->style()->unpolish(button);
    button->style()->polish(button);
  }
}

void ShellWindow::setBody(QWidget* page, bool with_navigation) {
  auto* old_layout = body_->layout();
  if (old_layout != nullptr) {
    while (auto* item = old_layout->takeAt(0)) {
      if (item->widget() == navigation_) {
        navigation_->setParent(body_);
      } else if (item->widget() != nullptr) {
        // 点击页签的信号还在旧页面的按钮上执行，旧页面延后删除。
        item->widget()->deleteLater();
      }
      delete item;
    }
    delete old_layout;
  }
  if (with_navigation) {
    auto* layout = new QHBoxLayout(body_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(navigation_);
    auto* content = new QWidget(body_);
    auto* content_layout = new QVBoxLayout(content);
    content_layout->setContentsMargins(0, 0, 0, 0);
    const auto& notices = controller_.display().notices();
    if (!notices.empty()) {
      auto* strip = new QFrame(content);
      strip->setObjectName(QStringLiteral("page-notices"));
      auto* rows = new QVBoxLayout(strip);
      for (const auto& notice : notices) {
        auto* line = text(qs(notice));
        line->setObjectName(QStringLiteral("page-notice"));
        line->setAccessibleName(qs(notice));
        rows->addWidget(line);
      }
      content_layout->addWidget(strip);
    }
    content_layout->addWidget(page);
    layout->addWidget(content, 1);
    return;
  }
  auto* layout = new QVBoxLayout(body_);
  layout->setContentsMargins(0, 0, 0, 0);
  const auto& notices = controller_.display().notices();
  if (!notices.empty()) {
    auto* strip = new QFrame(body_);
    strip->setObjectName(QStringLiteral("page-notices"));
    auto* rows = new QVBoxLayout(strip);
    for (const auto& notice : notices) {
      auto* line = text(qs(notice));
      line->setObjectName(QStringLiteral("page-notice"));
      line->setAccessibleName(qs(notice));
      rows->addWidget(line);
    }
    layout->addWidget(strip);
  }
  layout->addWidget(page);
}

}  // namespace pixcake::shell
