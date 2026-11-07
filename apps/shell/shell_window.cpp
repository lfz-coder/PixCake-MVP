#include "shell_window.hpp"

#include "pages.hpp"

#include <QApplication>
#include <QFileOpenEvent>
#include <QFont>
#include <QVBoxLayout>

namespace pixcake::shell {

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
  setBody(page);
}

void ShellWindow::setBody(QWidget* page) {
  auto* layout = body_->layout();
  if (layout == nullptr) {
    layout = new QVBoxLayout(body_);
    layout->setContentsMargins(0, 0, 0, 0);
  } else {
    while (auto* item = layout->takeAt(0)) {
      // 点击页签的信号还在旧页面的按钮上执行，旧页面延后删除。
      if (item->widget() != nullptr) item->widget()->deleteLater();
      delete item;
    }
  }
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
