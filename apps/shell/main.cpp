#include "pixcake/account/account_session.hpp"
#include "pixcake/instance/single_instance.hpp"
#include "pixcake/mask/brush_mask.hpp"
#include "pixcake/logging/file_log_sink.hpp"
#include "pixcake/platform/mac_source_access.hpp"
#include "pixcake/project_session/project_session.hpp"
#include "pixcake/ui/panels/slider_spec.hpp"
#include "pixcake/ui/shell/page_copy.hpp"
#include "pixcake/ui/shell/shell_display.hpp"
#include "filter_dialog.hpp"
#include "pixcake_theme.hpp"
#include "preset_dialog.hpp"
#include "pixcake_widgets.hpp"
#include "sync_dialog.hpp"
#include "trash_dialog.hpp"
#include "shell_window.hpp"

#include <QApplication>
#include <QFrame>
#include <QDialog>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QKeySequence>
#include <set>
#include <QSpinBox>
#include <QToolButton>
#include <QGroupBox>
#include <QDoubleSpinBox>
#include <QRadioButton>
#include <QDir>
#include <QFile>
#include <QFileOpenEvent>
#include <QElapsedTimer>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QCheckBox>
#include <QComboBox>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QEvent>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QSocketNotifier>
#include <QStringList>
#include <QTabBar>
#include <QTableWidget>
#include <QTreeWidget>
#include <QMenu>
#include <QMessageBox>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {

using pixcake::ui::shell::Page;
using pixcake::ui::shell::ShellDisplay;
namespace panels = pixcake::ui::panels;

#ifdef PIXCAKE_DEV_PAGES
// 只在开发构建：--page 直接预览某个页面（门禁规则 18）。发布构建由会话决定首屏（安全清单 S-25）。
Page parse_page(const QString& value) {
  if (value == QStringLiteral("home")) return Page::Home;
  if (value == QStringLiteral("local-projects")) return Page::LocalProjects;
  if (value == QStringLiteral("library")) return Page::Library;
  if (value == QStringLiteral("refine")) return Page::Refine;
  if (value == QStringLiteral("creative")) return Page::Creative;
  return Page::Login;
}
#endif

// 官方图标条只在鼠标悬停时显示名字（图 1 的「参考线」）。页签、导出、像素助手、会员续费
// 在官方顶栏里一直有字，其余顶栏图标、上下工具条和右侧图标栏不能把文案画在按钮上。
int check_icon_hover_chrome(const QWidget& window) {
  const QString assistant = QString::fromStdString(pixcake::ui::shell::page_item("refine-top", "assistant").text);
  const QString exported = QString::fromStdString(pixcake::ui::shell::page_item("refine-global", "export").text);
  const QString membership = QString::fromStdString(pixcake::ui::shell::page_item("home-toolbar", "membership").text);
  const auto hover_ok = [](const QAbstractButton* button) {
    return button->text().isEmpty() && !button->accessibleName().isEmpty() &&
           button->toolTip() == button->accessibleName() && !button->icon().isNull();
  };
  const auto caption_ok = [](const QAbstractButton* button) {
    return !button->text().isEmpty() && button->text() == button->accessibleName();
  };
  if (const auto* top = window.findChild<QFrame*>(QStringLiteral("workspace-top"))) {
    const auto buttons = top->findChildren<QPushButton*>();
    if (buttons.isEmpty()) {
      qCritical("workspace top has no buttons");
      return 8;
    }
    for (const auto* button : buttons) {
      const QString name = button->accessibleName();
      if (button->property("page-tab").toBool() || name == assistant || name == exported || name == membership) {
        if (!caption_ok(button)) {
          qCritical("top caption missing: %s", qPrintable(name));
          return 8;
        }
      } else if (!hover_ok(button)) {
        qCritical("top button still shows '%s' (%s)", qPrintable(button->text()), qPrintable(name));
        return 8;
      }
    }
  }
  for (auto* button : window.findChildren<QPushButton*>(QStringLiteral("home-toolbar"))) {
    const bool keep = button->accessibleName() == membership;
    if (keep ? !caption_ok(button) : !hover_ok(button)) {
      qCritical("home toolbar caption: %s", qPrintable(button->accessibleName()));
      return 8;
    }
  }
  const bool refine = window.findChild<QWidget*>(QStringLiteral("refine-page")) != nullptr;
  const bool creative = window.findChild<QWidget*>(QStringLiteral("creative-page")) != nullptr;
  const bool library = window.findChild<QWidget*>(QStringLiteral("library-page")) != nullptr;
  const auto tools = window.findChildren<QFrame*>(QStringLiteral("workspace-tool-button"));
  if ((refine || creative) && tools.isEmpty()) {
    qCritical("missing workspace tools");
    return 8;
  }
  for (const auto* tool : tools) {
    if (tool->findChild<QLabel*>(QStringLiteral("workspace-tool-label")) != nullptr || tool->toolTip().isEmpty() ||
        tool->toolTip() != tool->accessibleName()) {
      qCritical("workspace tool shows a caption: %s", qPrintable(tool->accessibleName()));
      return 8;
    }
    const auto* glyph = tool->findChild<QLabel*>(QStringLiteral("shell-icon"));
    const auto* pending = tool->findChild<QLabel*>(QStringLiteral("pending-icon"));
    if ((glyph == nullptr) == (pending == nullptr) || (glyph != nullptr && glyph->toolTip() != tool->toolTip()) ||
        (pending != nullptr && pending->toolTip() != tool->toolTip())) {
      qCritical("workspace tool icon/tooltip: %s", qPrintable(tool->accessibleName()));
      return 8;
    }
  }
  if (refine || creative || library) {
    const auto* thumbs = window.findChild<QFrame*>(QStringLiteral("workspace-thumbs"));
    if (thumbs == nullptr) {
      qCritical("missing thumbnail strip");
      return 8;
    }
    for (const auto* button : thumbs->findChildren<QPushButton*>()) {
      if (button->objectName() == QStringLiteral("icon-button") ||
          button->objectName() == QStringLiteral("thumbs-selected")) {
        continue;
      }
      if (!hover_ok(button)) {
        qCritical("thumbnail button still shows '%s' (%s)", qPrintable(button->text()),
                  qPrintable(button->accessibleName()));
        return 8;
      }
    }
  }
  if (refine) {
    int rails = 0;
    for (const auto* rail : window.findChildren<QToolButton*>()) {
      if (!rail->objectName().startsWith(QStringLiteral("refine-rail-"))) continue;
      ++rails;
      const QString id = rail->objectName().mid(QStringLiteral("refine-rail-").size());
      const bool uploaded =
          !pixcake::shell::shellAssetFile(QStringLiteral("refine-modules/") + id + QStringLiteral(".svg")).isEmpty();
      if (!hover_ok(rail) || rail->property("pending-icon").toBool() == uploaded) {
        qCritical("refine rail caption or icon: %s", qPrintable(rail->objectName()));
        return 8;
      }
    }
    if (rails == 0) {
      qCritical("missing refine rail");
      return 8;
    }
  }
  return 0;
}

// 精修某个面板的标签全部按文案表画出，且都禁用。
int check_disabled_labels(const QWidget& window, const char* object_name, std::string_view list,
                          const std::set<std::string>& live_ids = {}) {
  const auto* panel = window.findChild<QWidget*>(QString::fromLatin1(object_name));
  QStringList found;
  if (panel != nullptr) {
    for (const auto* label : panel->findChildren<QLabel*>()) {
      // A panel with live controls: only the plain labels (direct children) are checked; a control's own labels are not.
      if (!live_ids.empty() && label->parent() != panel) continue;
      found.append(label->text());
      if (label->isEnabled()) {
        qCritical("%s label enabled", object_name);
        return 3;
      }
    }
  }
  QStringList expected;
  for (const auto& entry : pixcake::ui::shell::page_list(list)) {
    if (live_ids.count(entry.id) == 0) expected.append(QString::fromStdString(entry.text));
  }
  if (panel == nullptr || found != expected) {
    qCritical("missing %s", object_name);
    return 3;
  }
  return 0;
}

// 精修色彩面板的滑杆只能来自参数模式：绑定的 95 支与内核规格一致，其余一律“范围待确认”。
// 色温、色调色带：整条轨道铺满，同一位置的颜色不随数值变化；默认值处有初始位置标记，滑块离开后露出来。
int check_track_sliders(const QWidget& window) {
  using pixcake::shell::PixCakeSlider;
  for (const char* track : {"temperature", "tint"}) {
    bool found = false;
    for (const auto* candidate : window.findChildren<QSlider*>()) {
      const auto* slider = dynamic_cast<const PixCakeSlider*>(candidate);
      if (slider == nullptr || slider->track() != QLatin1String(track)) continue;
      found = true;
      if (!slider->defaultValue() || *slider->defaultValue() != (slider->minimum() + slider->maximum()) / 2) {
        qCritical("%s slider has no centre default marker", track);
        return 4;
      }
    }
    if (!found) {
      qCritical("no %s colour band slider on the refine page", track);
      return 4;
    }
  }
  QWidget body;
  body.setObjectName(QStringLiteral("refine-section-body"));
  body.resize(300, 40);
  auto* slider = new PixCakeSlider(&body);
  slider->setTrack(QStringLiteral("temperature"));
  slider->setRange(0, 200);
  slider->setDefaultValue(100);
  slider->setGeometry(20, 10, 260, 20);
  body.ensurePolished();
  const auto shot = [slider](int value) {
    slider->setValue(value);
    QImage image = slider->grab().toImage();
    image.setDevicePixelRatio(1.0);
    return image.scaled(slider->size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
  };
  const QImage left = shot(30);
  const QImage right = shot(170);
  // Development aid: with PIXCAKE_SMOKE_SCREENSHOT=<file.png>, save both positions next to it.
  if (const char* file = std::getenv("PIXCAKE_SMOKE_SCREENSHOT"); file != nullptr && *file != '\0') {
    body.setAutoFillBackground(true);
    QPalette dark = body.palette();
    dark.setColor(QPalette::Window, QColor(0x1f, 0x21, 0x26));
    body.setPalette(dark);
    for (const QString& track : {QStringLiteral("temperature"), QStringLiteral("tint"), QStringLiteral("skin-temperature"),
                                 QStringLiteral("skin-tint"), QString()}) {
      slider->setTrack(track);
      for (const int value : {30, 100, 170}) {
        for (const bool hovered : {false, true}) {
          slider->setValue(value);
          slider->setAttribute(Qt::WA_UnderMouse, hovered);
          QString path = QString::fromLocal8Bit(file);
          path.insert(path.lastIndexOf(QLatin1Char('.')), QStringLiteral("-slider-%1-%2%3")
                                                              .arg(track.isEmpty() ? QStringLiteral("plain") : track)
                                                              .arg(value)
                                                              .arg(hovered ? QStringLiteral("-hover") : QString()));
          body.grab(slider->geometry()).save(path);
        }
      }
    }
    slider->setAttribute(Qt::WA_UnderMouse, false);
    slider->setTrack(QStringLiteral("temperature"));
  }
  const int mid_y = slider->height() / 2;
  const int marker_x = qRound(slider->handleCenterFor(100));
  const auto same = [](QColor a, QColor b) {
    return std::abs(a.red() - b.red()) <= 3 && std::abs(a.green() - b.green()) <= 3 && std::abs(a.blue() - b.blue()) <= 3;
  };
  // 两个滑块位置之外的几处：颜色一样（色带不跟着滑块伸缩）。
  for (const int x : {2, 90, 110, 257}) {
    if (!same(left.pixelColor(x, mid_y), right.pixelColor(x, mid_y))) {
      qCritical("temperature band at x=%d changed with the value: %s vs %s", x,
                qPrintable(left.pixelColor(x, mid_y).name()), qPrintable(right.pixelColor(x, mid_y).name()));
      return 4;
    }
  }
  // 左端是蓝、右端是黄（官方色标），不是灰轨道。
  const QColor blue = left.pixelColor(1, mid_y);
  const QColor yellow = left.pixelColor(slider->width() - 2, mid_y);
  if (blue.blue() < 140 || blue.red() > 70 || yellow.blue() > 110 || yellow.red() < 200) {
    qCritical("temperature band ends are %s / %s", qPrintable(blue.name()), qPrintable(yellow.name()));
    return 4;
  }
  // 滑块离开默认值：标记露出来，比色带亮、在中线上下都有。
  for (const int dy : {-2, 2}) {
    const QColor mark = left.pixelColor(marker_x, mid_y + dy);
    if (mark.red() < 180 || mark.green() < 180 || mark.blue() < 180) {
      qCritical("default marker missing at x=%d: %s", marker_x, qPrintable(mark.name()));
      return 4;
    }
  }
  // 色带没有填充段：标记和滑块之间仍是色带颜色。
  const int between_x = (qRound(slider->handleCenterFor(30)) + marker_x) / 2;
  if (!same(left.pixelColor(between_x, mid_y), right.pixelColor(between_x, mid_y))) {
    qCritical("temperature band has a fill segment at x=%d", between_x);
    return 4;
  }
  // 普通轨道：标记到滑块之间是浅色填充段，另一侧是 #36383d 轨道。
  slider->setTrack(QString());
  const QImage plain = shot(30);
  const QColor filled = plain.pixelColor(between_x, mid_y);
  const QColor bare = plain.pixelColor(slider->width() - 30, mid_y);
  if (filled.red() < 180 || filled.green() < 180 || bare != QColor(0x36, 0x38, 0x3d)) {
    qCritical("plain slider fill %s / track %s", qPrintable(filled.name()), qPrintable(bare.name()));
    return 4;
  }
  return 0;
}

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
  // 顶层影调 14 + 色阶 5 + HSL 24 + 黑白混合 8 + 可选颜色 36 + 颜色分级 13（“平衡”未注册）+ 色彩编辑器偏移 3
  // + 颗粒 7。
  if (bound != 110 || !exposure_ok) {
    qCritical("expected 110 parameter-mode sliders, found %d", bound);
    return 4;
  }
  return 0;
}

// Q-12：页签点击经 ShellController 改显示模型并重画；旧页面删除，新页面出现。
int check_tab_navigation(QWidget& window, Page first_page) {
  if (first_page == Page::Login || first_page == Page::Home || first_page == Page::LocalProjects) return 0;
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
    if (const int tracks = check_track_sliders(window); tracks != 0) return tracks;
  }
  if (const int chrome = check_icon_hover_chrome(window); chrome != 0) return chrome;
  if (first_page == Page::LocalProjects) {
    const auto* page = window.findChild<QWidget*>(QStringLiteral("local-projects-page"));
    const auto* list = window.findChild<QListWidget*>(QStringLiteral("local-projects-list"));
    const auto* empty = window.findChild<QWidget*>(QStringLiteral("empty-state"));
    if (page == nullptr || (list == nullptr && empty == nullptr) || (list != nullptr && empty != nullptr)) {
      qCritical("local projects page has neither a project list nor an empty state");
      return 7;
    }
  }
  if (first_page == Page::Refine) {
    if (const int crop = check_disabled_labels(window, "refine-crop-labels", "refine-crop",
                                                  {"crop-2", "crop-4", "crop-14", "crop-15", "crop-16", "crop-17", "angle"}); crop != 0) return crop;
    if (const int history = check_disabled_labels(window, "refine-history-labels", "refine-history"); history != 0) {
      return history;
    }
    if (const int sliders = check_refine_sliders(window); sliders != 0) return sliders;
    // 色彩调节、局部调色的直方图卡片和 AI 校准固定在滚动区外（官方 9.3.0）：两块，都不在任何 QScrollArea 里。
    {
      const auto tops = window.findChildren<QWidget*>(QStringLiteral("refine-color-top"));
      bool pinned = tops.size() == 2;
      for (const auto* top : tops) {
        for (const QWidget* up = top->parentWidget(); up != nullptr; up = up->parentWidget()) {
          if (qobject_cast<const QScrollArea*>(up) != nullptr) pinned = false;
        }
      }
      if (!pinned) {
        qCritical("the colour page tops must be two and outside the scroll areas (%lld found)",
                  static_cast<long long>(tops.size()));
        return 4;
      }
    }
    // Development aid: the whole window, then every module page with all its sections open.
    if (const char* shot = std::getenv("PIXCAKE_SMOKE_SCREENSHOT"); shot != nullptr && *shot != '\0') {
      window.resize(1386, 800);
      QCoreApplication::processEvents();
      window.grab().save(QString::fromLocal8Bit(shot));
      for (auto* rail : window.findChildren<QToolButton*>()) {
        if (!rail->objectName().startsWith(QStringLiteral("refine-rail-"))) continue;
        rail->click();
        for (auto* toggle : window.findChildren<QToolButton*>(QStringLiteral("refine-section-toggle"))) {
          if (toggle->isVisibleTo(&window)) toggle->setChecked(true);
        }
        for (auto* expander : window.findChildren<QToolButton*>(QStringLiteral("refine-expander"))) {
          if (expander->isVisibleTo(&window) && expander->isEnabled()) expander->setChecked(true);
        }
        QCoreApplication::processEvents();
        for (auto* content : window.findChildren<QWidget*>(QStringLiteral("refine-module-content"))) {
          if (!content->isVisibleTo(&window)) continue;
          content->adjustSize();
          QString path = QString::fromLocal8Bit(shot);
          path.insert(path.lastIndexOf(QLatin1Char('.')), QLatin1Char('-') + rail->objectName());
          content->grab().save(path);
        }
        // The page as shown, scrolled to the bottom: the pinned top must still be there.
        for (auto* scroll : window.findChildren<QScrollArea*>(QStringLiteral("refine-module-scroll"))) {
          if (!scroll->isVisibleTo(&window)) continue;
          scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
          QCoreApplication::processEvents();
          QString path = QString::fromLocal8Bit(shot);
          path.insert(path.lastIndexOf(QLatin1Char('.')), QLatin1Char('-') + rail->objectName() + QStringLiteral("-scrolled"));
          scroll->parentWidget()->grab().save(path);
        }
      }
    }
  }
  if (const int tabs = check_tab_navigation(window, first_page); tabs != 0) return tabs;
  // 页面用到的文案键都在文案表里（Q-11）。
  for (const auto& key : pixcake::ui::shell::missing_page_copy()) {
    qCritical("page copy key missing: %s", key.c_str());
    return 6;
  }
  return 0;
}

// renderd 与壳层在同一个 .app 的 Contents/MacOS 里；开发构建退回构建目录里的 renderd。
std::string renderd_executable() {
  const QString bundled = QCoreApplication::applicationDirPath() + QStringLiteral("/pixcake_app_renderd");
  if (QFileInfo(bundled).isExecutable()) return bundled.toStdString();
#ifdef PIXCAKE_RENDERD_DEV_PATH
  return PIXCAKE_RENDERD_DEV_PATH;
#else
  return bundled.toStdString();
#endif
}

// exportd 同 renderd：同一个 .app 的 Contents/MacOS 里；开发构建退回构建目录里的 exportd。
std::string exportd_executable() {
  const QString bundled = QCoreApplication::applicationDirPath() + QStringLiteral("/pixcake_app_exportd");
  if (QFileInfo(bundled).isExecutable()) return bundled.toStdString();
#ifdef PIXCAKE_EXPORTD_DEV_PATH
  return PIXCAKE_EXPORTD_DEV_PATH;
#else
  return bundled.toStdString();
#endif
}

pixcake::preview_channel::PreviewSessionOptions preview_options() {
  pixcake::preview_channel::PreviewSessionOptions options;
  options.renderd_executable = renderd_executable();
  options.log_directory = pixcake::logging::default_log_directory();
  options.document_directory =
      (QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/documents")).toStdString();
  return options;
}

// 登录后接上项目库（需求 5.2）：<数据目录>/projects.sqlite 与 documents/。未登录不打开。
void attach_projects(pixcake::shell::ShellWindow& window, const pixcake::account::AccountSession& session,
                     pixcake::SourceAccess& access, const QString& data_directory) {
  if (!session.signed_in()) return;
  QDir().mkpath(data_directory);
  std::unique_ptr<pixcake::project_session::ProjectSession> projects;
  std::string diagnostic;
  const auto code = pixcake::project_session::ProjectSession::open(
      data_directory.toStdString(), session, access, &pixcake::platform::MacSourceAccess::create_bookmark, projects,
      diagnostic);
  if (code != pixcake::ResultCode::Ok) {
    qWarning("project library unavailable: %s", diagnostic.c_str());
    return;
  }
  window.controller().attach_projects(std::move(projects), preview_options());
  // 导出（需求第 19 节）：导出日志在 <数据目录>/exports，导出进程第一次导出时启动。
  pixcake::export_session::ExportOptions exports;
  exports.exportd_executable = exportd_executable();
  exports.log_directory = pixcake::logging::default_log_directory();
  exports.data_directory = data_directory.toStdString();
  // 本机账本第一次建立时记入账号的剩余张数（没有服务器，之后的扣减只记在本机）。
  exports.opening_sheets = session.snapshot().remaining_sheets;
  window.controller().attach_exports(session, std::move(exports));
}

#ifdef PIXCAKE_DEV_PAGES
// 开发登录：没有账号服务时，让会话视为已登录（只在开发构建，和 --page 同一开关）。
pixcake::account::AccountSnapshot dev_account() {
  pixcake::account::AccountSnapshot snapshot;
  snapshot.account_id = "dev";
  snapshot.session_handle = "dev-session";
  snapshot.remaining_sheets = 100;  // 开发用：够导几张精修图
  return snapshot;
}

QListWidget* library_photos(QWidget& window) {
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  return window.findChild<QListWidget*>(QStringLiteral("library-photos"));
}

// Development aid: with PIXCAKE_SMOKE_SCREENSHOT=<file.png>, save the window as <file>-<name>.png.
void screenshot(QWidget& window, const char* name) {
  const char* shot = std::getenv("PIXCAKE_SMOKE_SCREENSHOT");
  if (shot == nullptr || *shot == '\0') return;
  if (window.isWindow() && qobject_cast<QDialog*>(&window) == nullptr) window.resize(1386, 800);
  QCoreApplication::processEvents();
  QString path = QString::fromLocal8Bit(shot);
  path.insert(path.lastIndexOf(QLatin1Char('.')), QLatin1Char('-') + QLatin1String(name));
  window.grab().save(path);
}

QString qtext_for_smoke(std::string_view key) { return QString::fromStdString(pixcake::ui::shell::page_text(key)); }

double mean_red(const QImage& image) {
  if (image.isNull()) return -1;
  double sum = 0;
  for (int y = 0; y < image.height(); ++y) {
    const auto* row = image.constScanLine(y);
    for (int x = 0; x < image.width(); ++x) sum += row[x * 4];
  }
  return sum / (static_cast<double>(image.width()) * image.height());
}

// --open 冒烟：等第一帧；画布有图、直方图有数据、只有全图色彩的滑杆可用；把曝光调高，
// 等新的一帧，画面整体变亮。
// Waits until the canvas shows the frame answering the newest request.
bool wait_current(const pixcake::shell::ShellWindow& window, int timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  while (!window.controller().preview_current() && timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents, 50);
  }
  return window.controller().preview_current();
}

bool wait_frames(const pixcake::shell::ShellWindow& window, std::uint64_t count, int timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  while (window.controller().frames_shown() < count && timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents, 50);
  }
  return window.controller().frames_shown() >= count;
}

// --dev-sign-in --data-dir --import 冒烟：主页导入 → 新项目（按 5.2 命名）→ 精修首张出帧 → 调曝光
// 标成已编辑 → 图库列出照片 → 回主页看到项目 → 重新打开项目，曝光仍在。
int smoke_open_photo(pixcake::shell::ShellWindow& window);
int smoke_icon_drop(pixcake::shell::ShellWindow& window, const std::vector<std::string>& imported);

// 导出（需求第 19 节）：精修里顶栏“导出”打开弹窗；未接入的项置灰并带原因；选 JPG、指定文件夹后导出，
// The visible child widget of a type and object name (pending deletions are flushed first).
template <typename T>
T* visibleChild(QWidget& window, const char* name) {
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  T* found = nullptr;
  for (auto* candidate : window.findChildren<T*>(QString::fromLatin1(name))) {
    if (candidate->isVisibleTo(&window)) found = candidate;
  }
  return found;
}

// 局部调色的蒙版列表浮窗（画布上）。行的角色见 refine_page.cpp：Qt::UserRole+1 种类（mask/item/edit/whole），
// Qt::UserRole+2 是否选中。
QTreeWidget* mask_tree(QWidget& window) { return visibleChild<QTreeWidget>(window, "refine-mask-tree"); }
std::vector<QTreeWidgetItem*> mask_rows(QWidget& window) {
  std::vector<QTreeWidgetItem*> rows;
  if (auto* tree = mask_tree(window)) {
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
      if (tree->topLevelItem(i)->data(0, Qt::UserRole + 1).toString() == QStringLiteral("mask")) rows.push_back(tree->topLevelItem(i));
    }
  }
  return rows;
}
// 选中蒙版的子项行的文字。
std::optional<QStringList> selected_mask_items(QWidget& window) {
  for (auto* row : mask_rows(window)) {
    if (!row->data(0, Qt::UserRole + 2).toBool()) continue;
    QStringList texts;
    for (int i = 0; i < row->childCount(); ++i) {
      if (row->child(i)->data(0, Qt::UserRole + 1).toString() == QStringLiteral("item")) texts << row->child(i)->text(0);
    }
    return texts;
  }
  return std::nullopt;
}

// 精修右侧图标栏点开一个模块（面板一次只显示一个模块，别的模块的控件不可见）。
bool show_refine_module(QWidget& window, const char* id) {
  const std::string name = std::string("refine-rail-") + id;
  auto* button = visibleChild<QToolButton>(window, name.c_str());
  if (button == nullptr) {
    qCritical("missing refine rail button %s", id);
    return false;
  }
  button->click();
  return button->isChecked();
}

// 展开某一页的折叠分组（色彩调节、局部调色、后期调色共用同一套分组，默认收起）。
bool open_refine_section(QWidget& window, const char* page, const char* label) {
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  for (auto* toggle : window.findChildren<QToolButton*>(QStringLiteral("refine-section-toggle"))) {
    const QWidget* section = toggle->parentWidget() != nullptr ? toggle->parentWidget()->parentWidget() : nullptr;
    if (!toggle->isVisibleTo(&window) || toggle->text() != QString::fromUtf8(label) || section == nullptr ||
        section->property("page").toString() != QString::fromLatin1(page)) {
      continue;
    }
    toggle->setChecked(true);
    return true;
  }
  qCritical("missing %s section %s", page, label);
  return false;
}

// 导出进程写出成品，导出列表显示成功；成品带着精修的曝光（和画面一样偏红亮）。
int smoke_export(pixcake::shell::ShellWindow& window, const QString& folder) {
  auto& controller = window.controller();
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  const QString label = QString::fromStdString(pixcake::ui::shell::page_item("refine-global", "export").text);
  QPushButton* button = nullptr;
  for (auto* candidate : window.findChildren<QPushButton*>()) {
    if (candidate->accessibleName() == label && candidate->isVisibleTo(&window)) button = candidate;
  }
  if (button == nullptr || !button->isEnabled() || !controller.export_target()) {
    qCritical("the refine export button is missing or disabled with a photo selected (%d %d %d %d)", button != nullptr,
              button != nullptr && button->isEnabled(), controller.has_exports(), controller.export_target().has_value());
    return 10;
  }
  // A watermark remembered from an earlier export: a green 60x30 material,
  // stored by the session (the file dialog is not driven here), switched on.
  QTemporaryDir material_dir;
  const QString material_file = material_dir.path() + QStringLiteral("/logo.png");
  QImage green(60, 30, QImage::Format_RGBA8888);
  green.fill(QColor(0, 200, 0));
  std::string material;
  std::string reason;
  if (!green.save(material_file) || !controller.store_watermark(material_file.toStdString(), material, reason)) {
    qCritical("watermark material not stored: %s", reason.c_str());
    return 10;
  }
  auto remembered = controller.export_target()->defaults;
  remembered.watermark_on = true;
  remembered.watermark = pixcake::ExportWatermark{material, {}};
  // 排版印刷也记住上次的开关和参数：5寸 竖向、普通照片排版、600 dpi、3 行 2 列、间隔 25、上边距 11。
  pixcake::ExportLayout sheet;
  sheet.id_photo = false;
  sheet.paper_width_mm = 89;
  sheet.paper_height_mm = 127;
  sheet.dpi = 600;
  sheet.rows = 3;
  sheet.cols = 2;
  sheet.gap_px = 25;
  sheet.margin_top_mm = 11;
  remembered.layout_on = true;
  remembered.layout = sheet;
  controller.remember_export_defaults(remembered);

  QString problem;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
    if (dialog == nullptr || dialog->objectName() != QStringLiteral("export-dialog")) {
      problem = QStringLiteral("no export dialog");
      return;
    }
    auto* tabs = dialog->findChild<QTabBar*>(QStringLiteral("export-tabs"));
    auto* watermark = dialog->findChild<QGroupBox*>(QStringLiteral("export-watermark"));
    auto* format = dialog->findChild<QComboBox*>(QStringLiteral("export-format"));
    auto* path = dialog->findChild<QLineEdit*>(QStringLiteral("export-folder"));
    auto* material_button = dialog->findChild<QToolButton*>(QStringLiteral("export-wm-material"));
    auto* portrait = dialog->findChild<QToolButton*>(QStringLiteral("export-wm-shape-portrait"));
    auto* centre = dialog->findChild<QToolButton*>(QStringLiteral("export-wm-anchor-4"));
    auto* top_left = dialog->findChild<QToolButton*>(QStringLiteral("export-wm-anchor-0"));
    auto* x = dialog->findChild<QSpinBox*>(QStringLiteral("export-wm-x"));
    auto* y = dialog->findChild<QSpinBox*>(QStringLiteral("export-wm-y"));
    // The photo has only a committed exposure: one free-effect picture, nothing charged
    // from the development account's 100 sheets.
    auto* free_count = dialog->findChild<QLabel*>(QStringLiteral("export-count-free"));
    auto* sheets = dialog->findChild<QLabel*>(QStringLiteral("export-sheets"));
    if (free_count == nullptr || free_count->text() != qtext_for_smoke("export.count.free").arg(1) ||
        sheets == nullptr || sheets->text() != qtext_for_smoke("export.sheets").arg(0).arg(100)) {
      problem = QStringLiteral("export estimate: %1 / %2")
                    .arg(free_count ? free_count->text() : QString(), sheets ? sheets->text() : QString());
      dialog->reject();
      return;
    }
    if (tabs == nullptr || tabs->count() != 3 || !tabs->isTabEnabled(2) ||
        watermark == nullptr || !watermark->isEnabled() || !watermark->isChecked() || material_button == nullptr ||
        material_button->toolTip() != qtext_for_smoke("export.wm.replace") || portrait == nullptr ||
        !portrait->isChecked() || centre == nullptr || top_left == nullptr || x == nullptr || y == nullptr ||
        format == nullptr || format->count() != 4 || path == nullptr) {
      problem = QStringLiteral("export dialog controls differ from the observed dialog");
      dialog->reject();
      return;
    }
    // The layout tab: nine papers; the switch and every parameter come back as remembered (seeded above);
    // plain layout hides the grid; going back to the observed defaults is what a fresh account sees.
    {
      auto* layout_group = dialog->findChild<QGroupBox*>(QStringLiteral("export-layout"));
      auto* paper = dialog->findChild<QComboBox*>(QStringLiteral("export-layout-paper"));
      auto* dpi = dialog->findChild<QSpinBox*>(QStringLiteral("export-layout-dpi"));
      auto* rows = dialog->findChild<QSpinBox*>(QStringLiteral("export-layout-rows"));
      auto* cols = dialog->findChild<QSpinBox*>(QStringLiteral("export-layout-cols"));
      auto* gap = dialog->findChild<QSpinBox*>(QStringLiteral("export-layout-gap"));
      auto* top = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("export-layout-margin-top"));
      auto* unit = dialog->findChild<QComboBox*>(QStringLiteral("export-layout-unit"));
      auto* plain_layout = dialog->findChild<QRadioButton*>(QStringLiteral("export-layout-plain"));
      auto* portrait_layout = dialog->findChild<QRadioButton*>(QStringLiteral("export-layout-portrait"));
      tabs->setCurrentIndex(2);
      const bool restored = layout_group != nullptr && layout_group->isVisibleTo(dialog) && layout_group->isChecked() &&
                            paper != nullptr && paper->count() == 9 && paper->currentData().toString() == QStringLiteral("89x127") &&
                            portrait_layout != nullptr && portrait_layout->isChecked() && plain_layout != nullptr &&
                            plain_layout->isChecked() && dpi != nullptr && dpi->value() == 600 && rows != nullptr &&
                            rows->value() == 3 && cols != nullptr && cols->value() == 2 && gap != nullptr &&
                            gap->value() == 25 && top != nullptr && top->value() == 11;
      const QString snapshot = QStringLiteral("group %1 checked %2 paper %3 rows %4 dpi %5 plain %6")
                                   .arg(layout_group != nullptr).arg(layout_group && layout_group->isChecked())
                                   .arg(paper ? paper->currentData().toString() : QString()).arg(rows ? rows->value() : -1)
                                   .arg(dpi ? dpi->value() : -1).arg(plain_layout && plain_layout->isChecked());
      // Plain layout hides the grid; the id layout shows it.
      bool grid_follows_kind = false;
      // The preview shows the paper (89x127 portrait here) while the switch is on, when a thumbnail exists.
      bool paper_in_preview = true;
      // The layout tab has the layout panel only: the counts, preset and folder block is hidden there.
      auto* common = dialog->findChild<QWidget*>(QStringLiteral("export-common"));
      bool common_hidden = common != nullptr && !common->isVisibleTo(dialog);
      tabs->setCurrentIndex(0);
      common_hidden = common_hidden && common->isVisibleTo(dialog);
      tabs->setCurrentIndex(2);
      if (restored) {
        if (auto* preview = dialog->findChild<QLabel*>(QStringLiteral("export-preview"))) {
          const QPixmap shown = preview->pixmap();
          if (!shown.isNull()) paper_in_preview = std::abs(shown.width() * 127.0 / (shown.height() * 89.0) - 1.0) < 0.03;
        }
      }
      if (restored) {
        screenshot(*dialog, "export-layout");
        // The ruler unit only changes the display: 11 mm is 1.1 cm and 0.43 in, and comes back as 11 mm.
        bool unit_ok = unit != nullptr && unit->count() == 3 && unit->currentData().toString() == QStringLiteral("mm");
        if (unit_ok) {
          unit->setCurrentIndex(unit->findData(QStringLiteral("cm")));
          unit_ok = std::abs(top->value() - 1.1) < 0.001;
          unit->setCurrentIndex(unit->findData(QStringLiteral("in")));
          unit_ok = unit_ok && std::abs(top->value() - 0.43) < 0.001;
          unit->setCurrentIndex(unit->findData(QStringLiteral("mm")));
          unit_ok = unit_ok && std::abs(top->value() - 11.0) < 0.001;
        }
        paper_in_preview = paper_in_preview && unit_ok;
        const bool plain_hidden = !rows->isVisibleTo(dialog);
        dialog->findChild<QRadioButton*>(QStringLiteral("export-layout-id"))->setChecked(true);
        grid_follows_kind = plain_hidden && rows->isVisibleTo(dialog);
        // Leave the observed defaults in place for what follows (the dialog is rejected, which remembers them).
        layout_group->setChecked(false);
        dialog->findChild<QRadioButton*>(QStringLiteral("export-layout-landscape"))->setChecked(true);
        paper->setCurrentIndex(paper->findData(QStringLiteral("152x102")));
        dpi->setValue(300);
        rows->setValue(1);
        cols->setValue(1);
        gap->setValue(10);
        top->setValue(0);
      }
      tabs->setCurrentIndex(1);
      if (!restored || !grid_follows_kind || !paper_in_preview || !common_hidden) {
        problem = QStringLiteral("the layout tab differs from the observed one (restored %1, grid follows kind %2, %3)")
                      .arg(restored).arg(grid_follows_kind && paper_in_preview).arg(snapshot);
        dialog->reject();
        return;
      }
    }
    // 同时导出无水印图片 is available inside the watermark group, with the observed explanation.
    auto* plain = dialog->findChild<QCheckBox*>(QStringLiteral("export-wm-plain"));
    if (plain == nullptr || !plain->isEnabled() || plain->toolTip() != qtext_for_smoke("export.wm.plain.tip")) {
      problem = QStringLiteral("the watermark-free copy option is missing or disabled");
      dialog->reject();
      return;
    }
    // The grid resets the two positions (observed): centre 0 / 0, a corner 3 / 3.
    centre->click();
    const bool centred = x->value() == 0 && y->value() == 0;
    top_left->click();
    if (!centred || x->value() != 3 || y->value() != 3) {
      problem = QStringLiteral("the watermark grid did not reset the positions");
      dialog->reject();
      return;
    }
    x->setValue(0);
    y->setValue(0);
    // 水印同步: another shape is read-only; the sync button copies this shape's placement to it.
    auto* landscape = dialog->findChild<QToolButton*>(QStringLiteral("export-wm-shape-landscape"));
    auto* sync = dialog->findChild<QPushButton*>(QStringLiteral("export-wm-sync"));
    auto* opacity = dialog->findChild<QSpinBox*>(QStringLiteral("export-wm-opacity"));
    if (landscape == nullptr || sync == nullptr || opacity == nullptr || !sync->isEnabled()) {
      problem = QStringLiteral("the watermark sync controls are missing");
      dialog->reject();
      return;
    }
    opacity->setValue(60);
    landscape->click();
    const bool read_only = !opacity->isEnabled();
    const int before_sync = opacity->value();
    sync->click();
    if (!read_only || before_sync == 60 || opacity->value() != 60 || !sync->isEnabled()) {
      problem = QStringLiteral("watermark sync: read-only %1, opacity %2 -> %3")
                    .arg(read_only).arg(before_sync).arg(opacity->value());
      dialog->reject();
      return;
    }
    portrait->click();
    opacity->setValue(100);
    screenshot(*dialog, "export-quick");
    tabs->setCurrentIndex(1);
    for (auto* fold : dialog->findChildren<QAbstractButton*>(QStringLiteral("export-advanced"))) fold->setChecked(true);
    screenshot(*dialog, "export-custom");
    if (auto* scroll = dialog->findChild<QScrollArea*>()) scroll->ensureWidgetVisible(watermark);
    screenshot(*dialog, "export-watermark");
    format->setCurrentIndex(format->findData(QStringLiteral("jpg")));
    // 文件大小限制 0.10 MB instead of a quality.
    auto* by_size = dialog->findChild<QRadioButton*>(QStringLiteral("export-by-size"));
    auto* size = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("export-size-limit"));
    if (by_size == nullptr || size == nullptr || size->isEnabled() || size->value() != 10.0) {
      problem = QStringLiteral("size limit controls missing, enabled while unselected, or not 10.00 MB");
      dialog->reject();
      return;
    }
    by_size->setChecked(true);
    size->setValue(0.10);
    path->setText(folder);
    for (auto* submit : dialog->findChildren<QPushButton*>()) {
      if (submit->objectName() == QStringLiteral("primary")) submit->click();
    }
    if (dialog->isVisible()) {
      problem = QStringLiteral("export dialog did not accept");
      dialog->reject();
    }
  });
  button->click();
  if (!problem.isEmpty()) {
    qCritical("%s", qPrintable(problem));
    return 10;
  }
  QElapsedTimer timer;
  timer.start();
  std::vector<pixcake::export_session::ExportRow> rows;
  for (;;) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    rows = controller.export_rows();
    if (!rows.empty() && (rows.front().succeeded || rows.front().can_retry)) break;
    if (timer.elapsed() > 60'000) break;
  }
  if (rows.empty() || !rows.front().succeeded) {
    qCritical("export did not succeed: %s", rows.empty() ? "no task" : rows.front().reason.c_str());
    return 10;
  }
  const QString product = QString::fromStdString(rows.front().destination_path);
  const QImage image = QImage(product).convertToFormat(QImage::Format_RGBA8888);
  const double red = mean_red(image);
  auto* list = window.findChild<QDialog*>(QStringLiteral("export-list-dialog"));
  auto* table = list != nullptr ? list->findChild<QTableWidget*>(QStringLiteral("export-list")) : nullptr;
  const qint64 bytes = QFileInfo(product).size();
  // The watermark in the top-left corner: short edge 10% of the long edge.
  const int edge = std::max(image.width(), image.height()) / 10;
  const QColor mark = image.isNull() ? QColor() : image.pixelColor(edge, edge / 2);
  const QColor beside = image.isNull() ? QColor() : image.pixelColor(edge * 3, edge / 2);
  if (!product.startsWith(folder) || !product.endsWith(QStringLiteral(".jpg")) || !(red > 200) || table == nullptr ||
      table->rowCount() < 1 || bytes <= 0 || bytes > 104857 ||  // 0.10 MB
      !(mark.green() > 150 && mark.red() < 90) || !(beside.red() > 150)) {
    qCritical("export product or list wrong: %s, red %.1f, %lld bytes, watermark %d %d %d beside %d %d %d",
              qPrintable(product), red, static_cast<long long>(bytes), mark.red(), mark.green(), mark.blue(),
              beside.red(), beside.green(), beside.blue());
    return 10;
  }
  // 计费一栏：只调了曝光，是免费效果图，不扣（记账在读到导出进程的进度之后，最多等几秒）。
  const QString free = qtext_for_smoke("export.charge.free");
  for (int i = 0; i < 100 && (table->item(0, 3) == nullptr || table->item(0, 3)->text() != free); ++i) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QThread::msleep(50);
  }
  // 个人中心可用（菜单第一项是本机账本的剩余张数）。
  const QString account_label = QString::fromStdString(pixcake::ui::shell::page_item("refine-global", "account").text);
  QPushButton* account = nullptr;
  for (auto* candidate : window.findChildren<QPushButton*>()) {
    if (candidate->accessibleName() == account_label && candidate->isVisibleTo(&window)) account = candidate;
  }
  const bool account_enabled = account != nullptr && account->isEnabled();
  QString first_entry;
  if (account_enabled) {
    QTimer::singleShot(0, [&first_entry] {
      auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
      if (menu != nullptr && menu->objectName() == QStringLiteral("account-menu") && !menu->actions().isEmpty()) {
        first_entry = menu->actions().front()->text();
      }
      if (menu != nullptr) menu->close();
    });
    account->click();
  }
  if (table->item(0, 3) == nullptr || table->item(0, 3)->text() != free || !account_enabled ||
      first_entry != qtext_for_smoke("account.sheets").arg(100) ||
      window.controller().remaining_sheets() != std::optional<std::int64_t>(100)) {
    qCritical("export charge column or account wrong: '%s', account %d '%s'",
              table->item(0, 3) ? qPrintable(table->item(0, 3)->text()) : "", account_enabled, qPrintable(first_entry));
    return 10;
  }
  screenshot(*list, "export-list");
  list->close();
  std::printf("export ok: %s, red %.1f, %lld bytes (limit 0.10 MB), watermark %d %d %d\n", qPrintable(product), red,
              static_cast<long long>(bytes), mark.red(), mark.green(), mark.blue());
  return 0;
}

// 图库多选：两张都选中后“已选 2 张（共 2 张）”、顶栏“导出”可用；快速导出一次排两张，两张都成功。
// 最后回到只选原来那张。
int smoke_library_batch_export(pixcake::shell::ShellWindow& window, QListWidget* list) {
  auto& controller = window.controller();
  const auto primary = controller.current_photo();
  auto* count = window.findChild<QLabel*>(QStringLiteral("library-selected"));
  const auto label = QString::fromStdString(pixcake::ui::shell::page_item("library-toolbar", "export").text);
  QPushButton* button = nullptr;
  for (auto* candidate : window.findChildren<QPushButton*>()) {
    if (candidate->accessibleName() == label && candidate->isVisibleTo(&window)) button = candidate;
  }
  // Nothing selected: nothing to export, and the count says so.
  list->clearSelection();
  if (count == nullptr || button == nullptr || button->isEnabled() ||
      count->text() != qtext_for_smoke("library.selected").arg(0).arg(2)) {
    qCritical("library with nothing selected: export %s, label %s",
              button != nullptr && button->isEnabled() ? "enabled" : "disabled",
              count == nullptr ? "missing" : qPrintable(count->text()));
    return 11;
  }
  for (int row = 0; row < list->count(); ++row) list->item(row)->setSelected(true);
  const QString two = qtext_for_smoke("library.selected").arg(2).arg(2);
  const auto target = controller.export_target();
  if (controller.selected_photos().size() != 2 || count == nullptr || count->text() != two || button == nullptr ||
      !button->isEnabled() || !target || target->photo_ids.size() != 2) {
    qCritical("library multi-selection: %zu selected, label %s, export %s", controller.selected_photos().size(),
              count == nullptr ? "missing" : qPrintable(count->text()),
              button != nullptr && button->isEnabled() ? "enabled" : "disabled");
    return 11;
  }
  QTemporaryDir folder;
  const std::size_t before = controller.export_rows().size();
  QString problem;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
    auto* path = dialog != nullptr ? dialog->findChild<QLineEdit*>(QStringLiteral("export-folder")) : nullptr;
    bool heading = false;
    for (auto* text : dialog != nullptr ? dialog->findChildren<QLabel*>() : QList<QLabel*>{}) {
      heading = heading || text->text() == qtext_for_smoke("export.heading").arg(2);
    }
    if (dialog == nullptr || path == nullptr || !heading) {
      problem = QStringLiteral("export dialog missing or not for 2 photos");
      if (dialog != nullptr) dialog->reject();
      return;
    }
    path->setText(folder.path());  // quick export: everything else as remembered
    for (auto* submit : dialog->findChildren<QPushButton*>()) {
      if (submit->objectName() == QStringLiteral("primary")) submit->click();
    }
  });
  button->click();
  if (!problem.isEmpty()) {
    qCritical("%s", qPrintable(problem));
    return 11;
  }
  QElapsedTimer timer;
  timer.start();
  std::vector<pixcake::export_session::ExportRow> rows;
  for (;;) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    rows = controller.export_rows();
    std::size_t done = 0;
    for (std::size_t i = 0; i + before < rows.size() && i < 2; ++i) done += rows[i].succeeded ? 1 : 0;
    if (rows.size() == before + 2 && done == 2) break;
    if (timer.elapsed() > 60'000) {
      qCritical("batch export did not finish: %zu new task(s)", rows.size() - before);
      return 11;
    }
  }
  if (auto* open = window.findChild<QDialog*>(QStringLiteral("export-list-dialog"))) open->close();
  for (int row = 0; row < list->count(); ++row) {
    auto* item = list->item(row);
    if (item->data(Qt::UserRole).toLongLong() == primary) list->setCurrentItem(item, QItemSelectionModel::ClearAndSelect);
  }
  std::printf("library batch export ok: 2 selected, both exported (%s, %s)\n", rows[0].file_name.c_str(),
              rows[1].file_name.c_str());
  return controller.selected_photos().size() == 1 ? 0 : 11;
}

// 虚拟副本（需求 §7）：创建后副本紧跟原图、带“副本 1”、成为选中的；有自己的缩略图，和原图的效果一致。
int smoke_virtual_copy(pixcake::shell::ShellWindow& window) {
  auto& controller = window.controller();
  controller.request_page(Page::Library);
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  const auto before = controller.photos();
  if (before.empty()) return 12;
  const auto source = before.front().id;  // the edited photo
  controller.request_virtual_copy({source});
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  const auto after = controller.photos();
  auto* list = library_photos(window);
  const auto selected = controller.selected_photos();
  if (after.size() != before.size() + 1 || after[0].id != source || after[1].id >= 0 || after[1].copy != 1 ||
      after[1].file_name != after[0].file_name || list == nullptr || list->count() != static_cast<int>(after.size()) ||
      !list->item(1)->text().contains(qtext_for_smoke("library.copy.tag").arg(1)) || selected.size() != 1 ||
      selected.front() != after[1].id) {
    qCritical("virtual copy: %zu photos (was %zu), second id %lld copy %lld", after.size(), before.size(),
              after.size() > 1 ? static_cast<long long>(after[1].id) : 0LL,
              after.size() > 1 ? static_cast<long long>(after[1].copy) : 0LL);
    return 12;
  }
  QElapsedTimer timer;
  timer.start();
  while (mean_red(controller.thumbnail(after[1].id)) < 200 && timer.elapsed() < 60'000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents, 50);
  }
  const double copy_red = mean_red(controller.thumbnail(after[1].id));
  if (!(copy_red > 200)) {
    qCritical("the copy's thumbnail does not carry the edit (red %.1f)", copy_red);
    return 12;
  }
  // The copy opens in refine with its own document: the edit carried over.
  controller.request_open_photo(after[1].id);
  if (controller.current_photo() != std::optional<std::int64_t>(after[1].id) || !controller.has_photo()) {
    qCritical("the copy does not open in refine");
    return 12;
  }
  // A copy of a photo that was never edited has no document of its own yet: still a thumbnail (the original's).
  const auto plain = before.back().id;
  controller.request_virtual_copy({plain});
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  std::int64_t plain_copy = 0;
  for (const auto& entry : controller.photos()) {
    if (entry.copy > 0 && controller.selected_photos() == std::vector<std::int64_t>{entry.id}) plain_copy = entry.id;
  }
  timer.restart();
  while (plain_copy != 0 && controller.thumbnail(plain_copy).isNull() && timer.elapsed() < 30'000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents, 50);
  }
  if (plain_copy == 0 || controller.thumbnail(plain_copy).isNull() || mean_red(controller.thumbnail(plain_copy)) > 160) {
    qCritical("the copy of an unedited photo has no (or an edited) thumbnail: %lld", static_cast<long long>(plain_copy));
    return 12;
  }
  // Marks: the copy shows its original's marks; the grid label carries them.
  {
    controller.request_page(Page::Library);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    const auto entries = controller.photos();
    controller.request_rating({entries[1].id}, 4);  // the copy: the mark lands on the original
    controller.request_color({entries[0].id}, pixcake::kColorRed);
    const auto marked = controller.photos();
    auto* list = library_photos(window);
    if (marked[0].rating != 4 || marked[1].rating != 4 || marked[1].color != pixcake::kColorRed || list == nullptr ||
        !list->item(0)->text().contains(qtext_for_smoke("mark.star").arg(4))) {
      qCritical("marks: ratings %d %d, copy colour '%s'", marked[0].rating, marked[1].rating, marked[1].color.c_str());
      return 12;
    }
    controller.request_rating({entries[0].id}, 0);
    controller.request_color({entries[0].id}, "");
  }
  // Filter and sort: the dialog's controls change the library's list; reset brings everything back.
  {
    controller.request_page(Page::Library);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    const auto all = controller.photos();
    controller.request_rating({all[0].id}, 5);
    QPushButton* filter = nullptr;
    for (auto* candidate : window.findChildren<QPushButton*>(QStringLiteral("photo-filter"))) {
      if (candidate->isVisibleTo(&window)) filter = candidate;
    }
    if (filter == nullptr || !filter->isEnabled()) {
      qCritical("filter button missing or disabled");
      return 12;
    }
    filter->click();
    auto* dialog = window.findChild<QDialog*>(QStringLiteral("filter-dialog"));
    auto* minimum = dialog != nullptr ? dialog->findChild<QSpinBox*>(QStringLiteral("filter-rating-min")) : nullptr;
    auto* reset = dialog != nullptr ? dialog->findChild<QPushButton*>(QStringLiteral("filter-reset")) : nullptr;
    auto* shown = dialog != nullptr ? dialog->findChild<QLabel*>(QStringLiteral("filter-shown")) : nullptr;
    if (minimum == nullptr || reset == nullptr || shown == nullptr) {
      qCritical("filter dialog incomplete");
      return 12;
    }
    minimum->setValue(5);
    const auto narrowed = controller.photos();
    if (!controller.photo_filtered() || narrowed.size() != 2 || narrowed[0].id != all[0].id ||
        shown->text() != qtext_for_smoke("filter.shown").arg(1).arg(2)) {
      qCritical("filter: %zu photos, label '%s'", narrowed.size(), qPrintable(shown->text()));
      return 12;
    }
    reset->click();
    if (controller.photo_filtered() || controller.photos().size() != all.size() || minimum->value() != 0) {
      qCritical("filter reset: %zu photos", controller.photos().size());
      return 12;
    }
    dialog->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    controller.request_rating({all[0].id}, 0);
  }
  // Shortcuts from the table: select all and deselect in the library grid, single view and back to the grid.
  {
    controller.request_page(Page::Library);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    const auto fire = [&window](const char* id) {
      QShortcut* found = nullptr;
      for (auto* candidate : window.findChildren<QShortcut*>(QStringLiteral("shortcut-") + QString::fromLatin1(id))) {
        const auto* owner = qobject_cast<QWidget*>(candidate->parent());
        if (owner != nullptr && owner->isVisibleTo(&window)) found = candidate;
      }
      if (found == nullptr) return false;
      emit found->activated();
      return true;
    };
    auto* grid_list = library_photos(window);
    if (grid_list == nullptr || !fire("select-all") || static_cast<std::size_t>(controller.selected_photos().size()) !=
                                                           controller.photos().size()) {
      qCritical("select-all shortcut: %zu of %zu selected", controller.selected_photos().size(), controller.photos().size());
      return 12;
    }
    if (!fire("deselect") || !controller.selected_photos().empty()) {
      qCritical("deselect shortcut left %zu selected", controller.selected_photos().size());
      return 12;
    }
    controller.select_photos({controller.photos().front().id}, controller.photos().front().id);
    if (!fire("single-view") || controller.display().library_view() != "single") {
      qCritical("single-view shortcut did not switch the view");
      return 12;
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    if (!fire("grid-view") || controller.display().library_view() != "grid") {
      qCritical("grid-view shortcut did not switch back");
      return 12;
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
  // Effects: copy one photo's effect, paste it on another (library); then sync through the dialog.
  {
    controller.request_page(Page::Library);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    const auto all = controller.photos();
    std::int64_t from = 0;
    std::int64_t to = 0;
    for (const auto& entry : all) {
      if (entry.copy != 0) continue;
      if (from == 0) from = entry.id;
      else if (to == 0) to = entry.id;
    }
    if (from == 0 || to == 0) {
      qCritical("effects: need two original photos");
      return 12;
    }
    controller.select_photos({from}, from);
    controller.request_copy_effect();
    if (!controller.can_paste_effect()) {
      qCritical("effects: nothing was copied");
      return 12;
    }
    controller.select_photos({to}, to);
    controller.request_paste_effect();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    const auto after_paste = controller.photos();
    const auto edited = [&after_paste](std::int64_t id) {
      for (const auto& entry : after_paste) {
        if (entry.id == id) return !entry.edit_status.empty();
      }
      return false;
    };
    if (!edited(to)) {
      qCritical("effects: the pasted photo is not marked edited");
      return 12;
    }
    // Sync dialog: the tree lists the sync items, blocked ones disabled; save runs it for the other selected photo.
    controller.select_photos({from, to}, from);
    pixcake::shell::openSyncDialog(&window, &controller);
    auto* dialog = window.findChild<QDialog*>(QStringLiteral("sync-dialog"));
    auto* tree = dialog != nullptr ? dialog->findChild<QTreeWidget*>(QStringLiteral("sync-tree")) : nullptr;
    auto* targets = dialog != nullptr ? dialog->findChild<QLabel*>(QStringLiteral("sync-targets")) : nullptr;
    auto* save = dialog != nullptr ? dialog->findChild<QPushButton*>(QStringLiteral("sync-save")) : nullptr;
    if (tree == nullptr || targets == nullptr || save == nullptr || tree->topLevelItemCount() == 0 ||
        targets->text() != qtext_for_smoke("sync.targets").arg(1) || !save->isEnabled()) {
      qCritical("sync dialog missing or wrong");
      return 12;
    }
    bool blocked_listed = false;
    for (QTreeWidgetItemIterator it(tree); *it; ++it) {
      if ((*it)->parent() != nullptr && !((*it)->flags() & Qt::ItemIsEnabled)) blocked_listed = true;
    }
    if (!blocked_listed) {
      qCritical("sync dialog does not list blocked items disabled");
      return 12;
    }
    save->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    if (window.findChild<QDialog*>(QStringLiteral("sync-dialog")) != nullptr) {
      qCritical("sync dialog stayed open after a successful sync");
      return 12;
    }
    controller.select_photos({from}, from);
  }
  // Delete and trash: the bulk delete asks first; a copy goes alone; the original takes its copies; restore.
  {
    const auto total = controller.photos().size();
    controller.request_page(Page::Library);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    controller.select_photos({controller.photos().back().id}, controller.photos().back().id);  // a copy
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QPushButton* remove = nullptr;
    for (auto* candidate : window.findChildren<QPushButton*>(QStringLiteral("library-delete"))) {
      if (candidate->isVisibleTo(&window)) remove = candidate;
    }
    QString confirm_text;
    QTimer answer;
    answer.setInterval(20);
    QObject::connect(&answer, &QTimer::timeout, [&confirm_text, &answer] {
      auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
      if (box == nullptr) return;
      answer.stop();
      confirm_text = box->text();
      for (auto* candidate : box->buttons()) {
        if (box->buttonRole(candidate) == QMessageBox::AcceptRole) candidate->click();
      }
    });
    answer.start();
    if (remove == nullptr || !remove->isEnabled()) {
      qCritical("library delete button missing or disabled");
      return 12;
    }
    remove->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    auto trash = controller.trash();
    if (confirm_text != qtext_for_smoke("trash.confirm.photo") || controller.photos().size() != total - 1 ||
        trash.size() != 1 || trash[0].kind != pixcake::shell::TrashKind::Copy) {
      qCritical("delete of a copy: confirm '%s', %zu -> %zu, trash %zu", qPrintable(confirm_text), total,
                controller.photos().size(), trash.size());
      return 12;
    }
    controller.request_restore(trash[0].kind, trash[0].id);
    if (controller.photos().size() != total || !controller.trash().empty()) {
      qCritical("restoring the copy: %zu photos, %zu in trash", controller.photos().size(), controller.trash().size());
      return 12;
    }
    // The trash dialog has a clear button; with no project in the trash it stays off.
    {
      pixcake::shell::openTrashDialog(&window, &controller);
      auto* open = window.findChild<QDialog*>(QStringLiteral("trash-dialog"));
      auto* clear = open != nullptr ? open->findChild<QPushButton*>(QStringLiteral("trash-clear")) : nullptr;
      bool has_project = false;
      for (const auto& entry : controller.trash()) has_project = has_project || entry.kind == pixcake::shell::TrashKind::Project;
      if (clear == nullptr || clear->isEnabled() != has_project) {
        qCritical("the clear-trash button is missing or its enabled state is wrong");
        return 12;
      }
      open->close();
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    // Deleting the original takes its copy too; restoring the original brings it back.
    controller.request_delete_photos({source});
    trash = controller.trash();
    bool original_listed = false;
    for (const auto& entry : trash) original_listed = original_listed || entry.kind == pixcake::shell::TrashKind::Photo;
    if (controller.photos().size() != total - 2 || !original_listed || controller.current_photo()) {
      qCritical("delete of the original: %zu photos (was %zu), original listed %d", controller.photos().size(), total,
                original_listed);
      return 12;
    }
    // The trash dialog lists them (name, kind, left); restoring the selected original from it.
    pixcake::shell::openTrashDialog(&window, &controller);
    auto* dialog = window.findChild<QDialog*>(QStringLiteral("trash-dialog"));
    auto* table = dialog != nullptr ? dialog->findChild<QTableWidget*>(QStringLiteral("trash-table")) : nullptr;
    auto* restore = dialog != nullptr ? dialog->findChild<QPushButton*>(QStringLiteral("trash-restore")) : nullptr;
    int original_row = -1;
    for (int row = 0; table != nullptr && row < table->rowCount(); ++row) {
      if (table->item(row, 1)->text() == qtext_for_smoke("trash.kind.photo")) original_row = row;
    }
    if (table == nullptr || restore == nullptr || table->rowCount() != static_cast<int>(trash.size()) ||
        original_row < 0 || table->item(original_row, 3)->text() != qtext_for_smoke("trash.left").arg(30)) {
      qCritical("trash dialog missing or wrong: %d rows for %zu entries", table != nullptr ? table->rowCount() : -1,
                trash.size());
      return 12;
    }
    table->selectRow(original_row);
    restore->click();
    dialog->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    const auto restored = controller.photos();
    if (restored.size() < total - 1 || restored.front().id != source) {
      qCritical("restoring the original: %zu photos", restored.size());
      return 12;
    }
    controller.request_open_photo(source);
  }
  std::printf("virtual copy ok: copy %lld follows the original, thumbnail red %.1f\n",
              static_cast<long long>(after[1].id), copy_red);
  return 0;
}

int smoke_import_flow(pixcake::shell::ShellWindow& window, const std::vector<std::string>& paths,
                      const std::string& expected_project) {
  auto& controller = window.controller();
  if (controller.display().page() != Page::Home || !controller.can_import()) {
    qCritical("dev sign-in did not reach home with a project library");
    return 9;
  }
  controller.request_import(paths);
  if (controller.display().page() != Page::Refine || !controller.current_project() || !controller.has_photo()) {
    qCritical("import did not open the new project in refine");
    for (const auto& notice : controller.display().notices()) qCritical("notice: %s", notice.c_str());
    return 9;
  }
  if (const int opened = smoke_open_photo(window); opened != 0) return opened;
  const auto photos = controller.photos();
  if (photos.empty() || photos.front().edit_status.empty()) {
    qCritical("the committed photo is not marked edited");
    return 9;
  }
  // Thumbnails: every photo gets one, and the edited photo's follows its edit.
  const auto edited = photos.front().id;
  const auto original = photos.back().id;
  QElapsedTimer timer;
  timer.start();
  while ((controller.thumbnails_ready() < photos.size() || mean_red(controller.thumbnail(edited)) < 200) &&
         timer.elapsed() < 60'000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents, 50);
  }
  const double edited_red = mean_red(controller.thumbnail(edited));
  const double original_red = mean_red(controller.thumbnail(original));
  if (controller.thumbnails_ready() < photos.size() || edited_red < 200 || original_red > 160 || original_red < 0) {
    qCritical("thumbnails missing or not following the edit: %zu of %zu, edited %.1f, original %.1f",
              controller.thumbnails_ready(), photos.size(), edited_red, original_red);
    return 9;
  }
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  auto* strip = window.findChild<QListWidget*>(QStringLiteral("thumbs-photos"));
  if (strip == nullptr || strip->count() != static_cast<int>(photos.size()) || strip->item(0)->icon().isNull()) {
    qCritical("refine thumbnail strip does not show the project's photos");
    return 9;
  }
  // Library (evidence 2026-11-06-project-entry-observation.md): a click selects,
  // a double-click shows the large image in the library's single view.
  controller.request_page(Page::Library);
  auto* list = library_photos(window);
  screenshot(window, "library");
  if (list == nullptr || list->count() != static_cast<int>(photos.size()) ||
      !list->item(0)->text().contains(QString::fromStdString(photos.front().edit_status))) {
    qCritical("library does not list the project's photos");
    return 9;
  }
  const auto item_of = [](QListWidget* in, std::int64_t id) -> QListWidgetItem* {
    for (int row = 0; row < in->count(); ++row) {
      if (in->item(row)->data(Qt::UserRole).toLongLong() == id) return in->item(row);
    }
    return nullptr;
  };
  list->setCurrentItem(item_of(list, original), QItemSelectionModel::ClearAndSelect);  // a plain click
  if (controller.display().page() != Page::Library || controller.display().library_view() != "grid" ||
      controller.current_photo() != original || controller.selected_photos().size() != 1) {
    qCritical("a click on a library photo did not just select it (page %d view %s current %lld selected %zu)",
              static_cast<int>(controller.display().page()), controller.display().library_view().c_str(),
              static_cast<long long>(controller.current_photo().value_or(-1)), controller.selected_photos().size());
    return 9;
  }
  if (const int batch = smoke_library_batch_export(window, list); batch != 0) return batch;
  // The edited photo is still open from refine: the single view shows its current frame at once.
  emit list->itemActivated(item_of(list, edited));
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  const auto* single = dynamic_cast<const pixcake::shell::PixCakeImageView*>(
      window.findChild<QWidget*>(QStringLiteral("library-single-image")));
  if (!wait_current(window, 60'000) || controller.display().page() != Page::Library ||
      controller.display().library_view() != "single" || controller.current_photo() != edited || single == nullptr ||
      single->image().isNull()) {
    qCritical("a double-click on a library photo did not show it in the single view");
    return 9;
  }
  screenshot(window, "library-single");
  const double single_red = mean_red(controller.preview_view().image);
  if (!(single_red > 200)) {
    qCritical("the single view does not show the edit (red %.1f)", single_red);
    return 9;
  }

  // Home: a click selects a project, a double-click opens it where it was left (the library).
  controller.request_page(Page::Home);
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  auto* projects = window.findChild<QListWidget*>(QStringLiteral("home-projects"));
  QListWidgetItem* project = nullptr;
  for (int row = 0; projects != nullptr && row < projects->count(); ++row) {
    if (projects->item(row)->text() == QString::fromStdString(expected_project)) project = projects->item(row);
  }
  screenshot(window, "home");
  if (project == nullptr || controller.has_photo()) {
    qCritical("home does not list project %s (or the photo stayed open)", expected_project.c_str());
    return 9;
  }
  projects->setCurrentItem(project);
  emit projects->itemClicked(project);
  if (controller.display().page() != Page::Home) {
    qCritical("a click on a project opened it");
    return 9;
  }
  emit projects->itemActivated(project);
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  if (controller.display().page() != Page::Library || controller.display().library_view() != "grid" ||
      controller.current_photo() != edited || controller.has_photo()) {
    qCritical("reopening the project did not return to the library grid with the selected photo");
    return 9;
  }
  // Refine shows the selected photo with its edit; the project is then remembered in refine.
  auto before = controller.frames_shown();
  controller.request_page(Page::Refine);
  if (!wait_frames(window, before + 1, 60'000) || controller.display().page() != Page::Refine) {
    qCritical("refine shows no frame for the selected photo");
    return 9;
  }
  controller.request_page(Page::Home);
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  projects = window.findChild<QListWidget*>(QStringLiteral("home-projects"));
  before = controller.frames_shown();
  if (projects != nullptr && projects->count() > 0) emit projects->itemActivated(projects->item(0));
  if (!wait_frames(window, before + 1, 60'000) || controller.display().page() != Page::Refine ||
      controller.current_photo() != edited) {
    qCritical("reopening the project did not return to refine");
    return 9;
  }
  // The committed exposure is applied again: much brighter than the original.
  const double red = mean_red(controller.preview_view().image);
  if (!(red > 200)) {
    qCritical("reopened photo lost its edit (red %.1f)", red);
    return 9;
  }
  {
    QTemporaryDir exported;
    if (const int code = smoke_export(window, exported.path()); code != 0) return code;
  }
  if (const int copied = smoke_virtual_copy(window); copied != 0) return copied;
  if (const int dropped = smoke_icon_drop(window, paths); dropped != 0) return dropped;
  std::printf("import flow ok: project %s, %zu photo(s), reopened red %.1f, thumbnails edited %.1f original %.1f\n",
              expected_project.c_str(), photos.size(), red, edited_red, original_red);
  return 0;
}

// Requirement 3.2 / B-71 with the events macOS sends for a drop on the Dock icon.
void drop_on_icon(const QString& path) {
  QFileOpenEvent event(path);
  QCoreApplication::sendEvent(qApp, &event);
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 400) QCoreApplication::processEvents(QEventLoop::AllEvents, 50);  // the batch window
}

int smoke_icon_drop(pixcake::shell::ShellWindow& window, const std::vector<std::string>& imported) {
  auto& controller = window.controller();
  QTemporaryDir drops;
  QString photo;
  const QFileInfo first(QString::fromStdString(imported.front()));
  if (first.isDir()) {
    const auto files = QDir(first.absoluteFilePath()).entryInfoList(QStringList() << QStringLiteral("*.png"), QDir::Files);
    if (!files.isEmpty()) photo = files.front().absoluteFilePath();
  } else {
    photo = first.absoluteFilePath();
  }
  const QString append = drops.filePath(QStringLiteral("drop-append.png"));
  const QString ignored = drops.filePath(QStringLiteral("drop-ignored.png"));
  const QString fresh = drops.filePath(QStringLiteral("Drop Shoot.png"));
  if (photo.isEmpty() || !QFile::copy(photo, append) || !QFile::copy(photo, ignored) || !QFile::copy(photo, fresh)) {
    qCritical("cannot prepare the dropped files");
    return 9;
  }
  // Refine: appended to the current project, no new project.
  const auto project = controller.current_project();
  const auto photo_count = controller.photos().size();
  const auto project_count = controller.projects().size();
  drop_on_icon(append);
  if (controller.current_project() != project || controller.photos().size() != photo_count + 1 ||
      controller.projects().size() != project_count) {
    qCritical("a drop in refine was not appended to the project");
    return 9;
  }
  // A modal layer open: nothing is imported and the page stays.
  {
    QDialog modal(&window);
    modal.setWindowModality(Qt::ApplicationModal);
    modal.show();
    drop_on_icon(ignored);
    modal.close();
  }
  if (controller.photos().size() != photo_count + 1 || controller.projects().size() != project_count ||
      controller.display().page() != Page::Refine) {
    qCritical("a drop under a modal layer was imported");
    return 9;
  }
  // Home: a new project named after the first file (requirement 5.2), entered in refine.
  controller.request_page(Page::Home);
  drop_on_icon(fresh);
  const auto projects = controller.projects();
  const bool named = std::any_of(projects.begin(), projects.end(),
                                 [](const auto& entry) { return entry.name == "Drop Shoot"; });
  if (projects.size() != project_count + 1 || !named || controller.current_project() == project ||
      controller.display().page() != Page::Refine) {
    qCritical("a drop on home did not create project \"Drop Shoot\"");
    return 9;
  }
  std::printf("icon drop ok: appended in refine, ignored under a modal layer, new project from home\n");
  return 0;
}

int smoke_open_photo(pixcake::shell::ShellWindow& window) {
  // The page built before the photo was opened is deleted later; drop it first.
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  if (!wait_frames(window, 1, 60'000)) {
    qCritical("no preview frame: %s", qPrintable(window.controller().preview_view().status));
    return 8;
  }
  auto* view =
      dynamic_cast<pixcake::shell::PixCakeImageView*>(window.findChild<QFrame*>(QStringLiteral("refine-canvas-image")));
  const auto* chart =
      dynamic_cast<const pixcake::shell::PixCakeHistogram*>(window.findChild<QFrame*>(QStringLiteral("refine-histogram")));
  if (view == nullptr || view->image().isNull() || chart == nullptr || chart->histogram().peak == 0) {
    qCritical("canvas or histogram is empty");
    return 8;
  }
  // Display zoom is a real dropdown and stays in the canvas view state.  It must not
  // change the preview image itself or enter the edit document.
  auto* zoom = window.findChild<QComboBox*>(QStringLiteral("refine-zoom-ratio"));
  const int hundred = zoom == nullptr ? -1 : zoom->findData(1.0);
  if (zoom == nullptr || hundred < 0 || zoom->findData(0.0) < 0 ||
      window.findChild<QShortcut*>(QStringLiteral("shortcut-zoom-in")) == nullptr ||
      window.findChild<QShortcut*>(QStringLiteral("shortcut-zoom-out")) == nullptr ||
      window.findChild<QShortcut*>(QStringLiteral("shortcut-zoom-default")) == nullptr ||
      window.findChild<QShortcut*>(QStringLiteral("shortcut-hand-tool")) == nullptr) {
    qCritical("canvas zoom dropdown or shortcuts are missing");
    return 8;
  }
  zoom->setCurrentIndex(hundred);
  emit zoom->activated(hundred);
  const QRectF hundred_rect = view->imageRect();
  if (std::abs(view->zoom() - 1.0) > 1e-6 || std::abs(hundred_rect.width() - view->image().width()) > 1.0 ||
      std::abs(hundred_rect.height() - view->image().height()) > 1.0) {
    qCritical("100%% zoom did not use image pixels: %.3f rect %.1fx%.1f image %dx%d", view->zoom(), hundred_rect.width(),
              hundred_rect.height(), view->image().width(), view->image().height());
    return 8;
  }
  zoom->setCurrentIndex(zoom->findData(0.0));
  emit zoom->activated(zoom->currentIndex());
  if (view->zoom() != 0.0 || view->imageRect().isEmpty()) {
    qCritical("fit zoom did not restore the canvas");
    return 8;
  }
  // Plain wheel input is the canvas zoom gesture; the hand tool reserves
  // panning for a drag.
  view->setZoom(0.5);
  const QPointF wheel_point = view->imageRect().center();
  QWheelEvent wheel_event(wheel_point, wheel_point, QPoint(0, 0), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                          Qt::NoScrollPhase, false);
  QCoreApplication::sendEvent(view, &wheel_event);
  if (std::abs(view->zoom() - 1.0) > 1e-6) {
    qCritical("plain wheel did not zoom the canvas");
    return 8;
  }
  const auto check_sampling = [view](pixcake::shell::PixCakeImageView::SamplingMode expected, const char* label) {
    view->repaint();
    if (view->samplingMode() != expected) {
      qCritical("unexpected canvas sampling mode for %s: got %d expected %d", label,
                static_cast<int>(view->samplingMode()), static_cast<int>(expected));
      return false;
    }
    return true;
  };
  view->setZoom(1.0);
  if (!check_sampling(pixcake::shell::PixCakeImageView::SamplingMode::Direct, "100%%")) return 8;
  view->setZoom(0.05);
  if (!check_sampling(pixcake::shell::PixCakeImageView::SamplingMode::DownsamplePyramid, "5%%")) return 8;
  view->setZoom(2.0);
  if (!check_sampling(pixcake::shell::PixCakeImageView::SamplingMode::SmoothUpscale, "200%%")) return 8;
  view->setZoom(1.0);
  view->setZoom(0.0);
  view->setZoom(-1.0);
  if (std::abs(view->zoom() - 1.0) > 1e-6) {
    qCritical("invalid zoom changed the last valid display");
    return 8;
  }
  std::optional<QColor> hovered;
  view->onHoverPixel([&hovered](std::optional<QColor> color) { hovered = color; });
  const QRectF pixel_rect = view->imageRect();
  const QPointF pixel_point = pixel_rect.center();
  QMouseEvent pixel_move(QEvent::MouseMove, pixel_point, pixel_point, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
  QCoreApplication::sendEvent(view, &pixel_move);
  const QColor expected_pixel = view->image().pixelColor(view->image().width() / 2, view->image().height() / 2);
  if (!hovered || hovered->rgb() != expected_pixel.rgb()) {
    qCritical("zoomed canvas hover did not map to the image pixel");
    return 8;
  }
  view->setFitZoom();
  QFrame* hand_tool = nullptr;
  const QString hand_label = QString::fromStdString(pixcake::ui::shell::page_item("refine-tools", "hand").text);
  for (auto* tool : window.findChildren<QFrame*>(QStringLiteral("workspace-tool-button"))) {
    if (tool->accessibleName() == hand_label) hand_tool = tool;
  }
  if (hand_tool == nullptr) {
    qCritical("hand toolbar tool is missing");
    return 8;
  }
  if (!view->handTool()) {
    qCritical("hand tool is not enabled by default");
    return 8;
  }
  const QPointF hand_click(hand_tool->rect().center());
  QMouseEvent hand_press(QEvent::MouseButtonPress, hand_click, hand_click, Qt::LeftButton, Qt::LeftButton,
                         Qt::NoModifier);
  QMouseEvent hand_release(QEvent::MouseButtonRelease, hand_click, hand_click, Qt::LeftButton, Qt::NoButton,
                           Qt::NoModifier);
  QCoreApplication::sendEvent(hand_tool, &hand_press);
  QCoreApplication::sendEvent(hand_tool, &hand_release);
  if (!view->handTool()) {
    qCritical("hand toolbar click unexpectedly disabled the hand tool");
    return 8;
  }
  // Reference lines are a display overlay: the toolbar click creates one,
  // shortcuts add the other orientation, and hiding keeps the lines intact.
  QFrame* guide_tool = nullptr;
  const QString guide_label = QString::fromStdString(pixcake::ui::shell::page_item("refine-tools", "guides").text);
  for (auto* tool : window.findChildren<QFrame*>(QStringLiteral("workspace-tool-button"))) {
    if (tool->accessibleName() == guide_label) guide_tool = tool;
  }
  if (guide_tool == nullptr) {
    qCritical("reference-line toolbar tool is missing");
    return 8;
  }
  for (const auto* shortcut : {"shortcut-guide-horizontal", "shortcut-guide-vertical", "shortcut-guide-toggle"}) {
    if (window.findChild<QShortcut*>(QString::fromLatin1(shortcut)) == nullptr) {
      qCritical("reference-line shortcut is missing: %s", shortcut);
      return 8;
    }
  }
  bool guide_menu_ok = false;
  QTimer::singleShot(0, [&guide_menu_ok] {
    auto* popup = qobject_cast<QMenu*>(QApplication::activePopupWidget());
    if (popup == nullptr || popup->actions().size() != 4) return;
    const auto actions = popup->actions();
    const QString horizontal = QString::fromStdString(pixcake::ui::shell::page_text("guides.create-horizontal"));
    const QString vertical = QString::fromStdString(pixcake::ui::shell::page_text("guides.create-vertical"));
    const QString hide = QString::fromStdString(pixcake::ui::shell::page_text("guides.hide"));
    const auto native_key = [](const char* id) {
      return QKeySequence(QString::fromStdString(pixcake::ui::shell::shortcut_key(id)))
          .toString(QKeySequence::NativeText);
    };
    const QString horizontal_key = native_key("guide-horizontal");
    const QString vertical_key = native_key("guide-vertical");
    const QString toggle_key = native_key("guide-toggle");
    guide_menu_ok = actions[0]->text().contains(horizontal) &&
                    actions[0]->text().contains(horizontal_key) &&
                    actions[1]->text().contains(vertical) &&
                    actions[1]->text().contains(vertical_key) &&
                    actions[0]->isEnabled() && actions[1]->isEnabled() && actions[2]->isSeparator() &&
                    actions[3]->text().contains(hide) && actions[3]->text().contains(toggle_key) &&
                    actions[3]->isEnabled();
    popup->close();
  });
  const QPointF menu_click(guide_tool->rect().center());
  QMouseEvent menu_press(QEvent::MouseButtonPress, menu_click, menu_click, Qt::LeftButton, Qt::LeftButton,
                         Qt::NoModifier);
  QMouseEvent menu_release(QEvent::MouseButtonRelease, menu_click, menu_click, Qt::LeftButton, Qt::NoButton,
                           Qt::NoModifier);
  QCoreApplication::sendEvent(guide_tool, &menu_press);
  QCoreApplication::sendEvent(guide_tool, &menu_release);
  if (!guide_menu_ok) {
    qCritical("reference-line toolbar menu is missing its three labelled actions");
    return 8;
  }
  const std::size_t guide_count = window.controller().guides().size();
  // The menu itself was verified above; use the controller action for the
  // following state assertions so the smoke run does not leave a second modal
  // menu open without a user selection.
  window.controller().request_create_guide(false);
  if (window.controller().guides().size() != guide_count + 1 || view->guides().size() != guide_count + 1) {
    qCritical("reference-line toolbar click did not create a guide");
    return 8;
  }
  window.controller().request_create_guide(true);
  if (window.controller().guides().size() != guide_count + 2 || !window.controller().guides()[1].vertical) {
    qCritical("vertical reference line was not created");
    return 8;
  }
  window.controller().request_create_guide(false);
  if (window.controller().guides().size() != guide_count + 3 ||
      window.controller().guides()[2].position != window.controller().guides()[0].position) {
    qCritical("overlapping reference lines were not preserved");
    return 8;
  }
  window.controller().request_move_guide(0, 80.0);
  if (window.controller().guides()[0].position != 80.0 || view->guides()[0].position != 80.0) {
    qCritical("reference-line movement was not published to the canvas");
    return 8;
  }
  const QPointF line_point(view->width() * 0.25, view->height() * 0.8);
  QMouseEvent line_press(QEvent::MouseButtonPress, line_point, line_point, Qt::LeftButton, Qt::LeftButton,
                         Qt::NoModifier);
  QMouseEvent line_release(QEvent::MouseButtonRelease, line_point, line_point, Qt::LeftButton, Qt::NoButton,
                           Qt::NoModifier);
  QCoreApplication::sendEvent(view, &line_press);
  QCoreApplication::sendEvent(view, &line_release);
  if (!view->selectedGuide() || *view->selectedGuide() != 0) {
    qCritical("canvas guide click did not select the moved line");
    return 8;
  }
  QKeyEvent delete_press(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
  QKeyEvent delete_release(QEvent::KeyRelease, Qt::Key_Delete, Qt::NoModifier);
  QCoreApplication::sendEvent(view, &delete_press);
  QCoreApplication::sendEvent(view, &delete_release);
  if (window.controller().guides().size() != guide_count + 2 || view->selectedGuide()) {
    qCritical("Delete did not remove the selected reference line");
    return 8;
  }
  window.controller().request_toggle_guides();
  if (view->guidesVisible()) {
    qCritical("reference-line visibility toggle did not hide guides");
    return 8;
  }
  const std::size_t hidden_count = window.controller().guides().size();
  window.controller().request_create_guide(false);
  if (window.controller().guides().size() != hidden_count) {
    qCritical("hidden reference lines still allowed creation");
    return 8;
  }
  window.controller().request_toggle_guides();
  window.controller().request_remove_all_guides();
  if (!window.controller().guides().empty() || !view->guides().empty()) {
    qCritical("reference-line removal did not clear guides");
    return 8;
  }
  const double before = mean_red(view->image());
  const QSize frame_size = view->image().size();  // the page is rebuilt by later steps: keep what the report needs
  // Enabled exactly when renderd draws the parameter and no parameter-mode rule blocks it.
  QSlider* exposure = nullptr;
  QSlider* hsl_red = nullptr;
  int enabled = 0;
  for (auto* slider : window.findChild<QWidget*>(QStringLiteral("refine-color-labels"))->findChildren<QSlider*>()) {
    const std::string name = slider->accessibleName().toStdString();
    const auto label = panels::slider_label_of(name);
    // Unbound rows (the curve's shadows row) share labels with bound ones; their names mark the range pending.
    std::optional<panels::SliderBinding> binding;
    if (!panels::slider_range_pending(name)) {
      binding = panels::refine_color_binding(label);
      if (!binding) binding = panels::refine_subgroup_binding(label);
    }
    const bool expected = binding && pixcake::shell::reference_drawn(*binding) &&
                          !pixcake::shell::parameter_blocked(&window.controller(), *binding);
    if (slider->isEnabled() != expected) {
      qCritical("slider %s is %s", label.c_str(), slider->isEnabled() ? "enabled but not drawable" : "disabled");
      return 8;
    }
    if (!slider->isEnabled()) continue;
    ++enabled;
    if (binding->parameter_id == "exposure") exposure = slider;
    if (binding->parameter_id == "hsl_saturation_red") hsl_red = slider;
  }
  // Standard profile: HSL drawn, the black-and-white mix and the colour editor not; grain amount 0 blocks the rest.
  if (hsl_red == nullptr || enabled != 14 + 5 + 24 + 36 + 13 + 1) {
    qCritical("unexpected enabled sliders: %d", enabled);
    return 8;
  }
  if (exposure == nullptr) {
    qCritical("exposure slider is not enabled (%d enabled)", enabled);
    return 8;
  }
  const auto shown = window.controller().frames_shown();
  exposure->setValue(std::min(exposure->maximum(), exposure->value() + 20));  // +1 EV, committed
  if (!wait_frames(window, shown + 1, 30'000)) {
    qCritical("no frame after moving exposure");
    return 8;
  }
  const double after = mean_red(view->image());
  if (!(after > before + 5)) {
    qCritical("exposure did not brighten the preview: %.1f -> %.1f", before, after);
    return 8;
  }
  // Black-and-white profile: the frame turns grey; HSL is blocked and the mix unblocked in place.
  auto* profile = window.findChild<QComboBox*>(QStringLiteral("refine-choice-profile"));
  const int monochrome = profile != nullptr ? profile->findData(QStringLiteral("monochrome")) : -1;
  if (monochrome < 0 || !profile->isEnabled()) {
    qCritical("profile choice missing or disabled");
    return 8;
  }
  if (!wait_current(window, 30'000)) {
    qCritical("the exposure frames did not finish");
    return 8;
  }
  profile->setCurrentIndex(monochrome);
  emit profile->activated(monochrome);
  if (!wait_current(window, 30'000)) {
    qCritical("no frame after choosing the black-and-white profile");
    return 8;
  }
  const QImage grey = view->image().convertToFormat(QImage::Format_RGBA8888);
  for (int y = 0; y < grey.height(); y += 37) {
    for (int x = 0; x < grey.width(); x += 41) {
      const QRgb pixel = grey.pixel(x, y);
      if (std::abs(qRed(pixel) - qGreen(pixel)) > 1 || std::abs(qGreen(pixel) - qBlue(pixel)) > 1) {
        qCritical("black-and-white profile left colour at %d,%d", x, y);
        return 8;
      }
    }
  }
  QSlider* mix_red = nullptr;
  QSlider* grain_amount = nullptr;
  QSlider* grain_size = nullptr;
  for (auto* slider : window.findChild<QWidget*>(QStringLiteral("refine-color-labels"))->findChildren<QSlider*>()) {
    const auto label = panels::slider_label_of(slider->accessibleName().toStdString());
    if (label == "黑白混合-红色") mix_red = slider;
    if (label == "总滑杆") grain_amount = slider;
    if (label == "单个颗粒度大小") grain_size = slider;
  }
  if (hsl_red->isEnabled() || mix_red == nullptr || !mix_red->isEnabled()) {
    qCritical("profile change did not swap HSL for the black-and-white mix");
    return 8;
  }
  // Grain amount above 0 unblocks the other grain sliders and draws grain.
  if (grain_amount == nullptr || grain_size == nullptr || grain_size->isEnabled()) {
    qCritical("grain sliders missing, or size enabled at amount 0");
    return 8;
  }
  grain_amount->setValue(60);
  if (!wait_current(window, 30'000) || !grain_size->isEnabled()) {
    qCritical("grain amount did not draw or unblock its sliders");
    return 8;
  }
  // The colour panel's sections start folded; the sliders are there but hidden until a section header is clicked.
  if (!show_refine_module(window, "crop")) return 8;
  // Crop and rotate: the live controls change the frame (rotate swaps width and height, a ratio crops, straightening
  // shrinks it); everything is put back before the next step.
  {
    auto& controller = window.controller();
    const auto find_button = [&window](const char* name) {
      QPushButton* found = nullptr;
      for (auto* candidate : window.findChildren<QPushButton*>(QString::fromLatin1(name))) {
        if (candidate->isVisibleTo(&window)) found = candidate;
      }
      return found;
    };
    auto* clockwise = find_button("refine-crop-clockwise");
    auto* counter = find_button("refine-crop-counterclockwise");
    auto* original = find_button("refine-crop-original");
    auto* flip = find_button("refine-crop-flip-horizontal");
    QComboBox* ratio = nullptr;
    for (auto* candidate : window.findChildren<QComboBox*>(QStringLiteral("refine-crop-ratio"))) {
      if (candidate->isVisibleTo(&window)) ratio = candidate;
    }
    QSlider* angle = nullptr;
    for (auto* candidate : window.findChildren<QSlider*>(QStringLiteral("refine-crop-angle"))) {
      if (candidate->isVisibleTo(&window)) angle = candidate;
    }
    if (clockwise == nullptr || counter == nullptr || original == nullptr || flip == nullptr || ratio == nullptr ||
        angle == nullptr || !clockwise->isEnabled() || !ratio->isEnabled() || !angle->isEnabled()) {
      qCritical("crop controls missing or disabled with a photo open");
      return 8;
    }
    const auto settle = [&] { return wait_current(window, 30'000); };
    const auto size_now = [&] { return view->image().size(); };
    const QSize start = size_now();
    clockwise->click();
    if (!settle() || size_now() != QSize(start.height(), start.width())) {
      qCritical("clockwise turn: %dx%d -> %dx%d", start.width(), start.height(), size_now().width(), size_now().height());
      return 8;
    }
    counter->click();
    if (!settle() || size_now() != start) {
      qCritical("counter-clockwise turn did not come back");
      return 8;
    }
    // Flipping keeps the size but changes the picture.
    const QImage before_flip = view->image();
    flip->click();
    if (!settle() || size_now() != start || view->image() == before_flip) {
      qCritical("horizontal flip changed nothing");
      return 8;
    }
    flip->click();
    if (!settle() || view->image() != before_flip) {
      qCritical("flipping twice did not restore the picture");
      return 8;
    }
    const int square = ratio->findData(QStringLiteral("1:1"));
    ratio->setCurrentIndex(square);
    emit ratio->activated(square);
    if (!settle() || size_now().width() != size_now().height() || size_now().width() != std::min(start.width(), start.height())) {
      qCritical("1:1 crop: %dx%d from %dx%d", size_now().width(), size_now().height(), start.width(), start.height());
      return 8;
    }
    original->click();
    if (!settle() || size_now() != start) {
      qCritical("original size did not come back");
      return 8;
    }
    angle->setValue(angle->value() + 10);
    if (!settle() || !(size_now().width() < start.width()) || size_now().width() * start.height() > size_now().height() * start.width() + 2 * start.height() ||
        size_now().width() * start.height() + 2 * start.height() < size_now().height() * start.width()) {
      qCritical("straightening: %dx%d from %dx%d", size_now().width(), size_now().height(), start.width(), start.height());
      return 8;
    }
    angle->setValue(angle->value() - 10);
    if (!settle() || size_now() != start) {
      qCritical("straightening back to 0 did not restore the frame");
      return 8;
    }
    (void)controller;
  }
  if (!show_refine_module(window, "post") || !open_refine_section(window, "post", "影调")) return 8;
  // Post colour: its exposure slider draws and commits to color.post (a step of its own in the history).
  {
    auto& controller = window.controller();
    QSlider* post_exposure = nullptr;
    for (auto* candidate : window.findChildren<QSlider*>(QStringLiteral("refine-post-exposure"))) {
      if (candidate->isVisibleTo(&window)) post_exposure = candidate;
    }
    if (post_exposure == nullptr || !post_exposure->isEnabled()) {
      qCritical("post colour exposure slider missing or disabled");
      return 8;
    }
    const auto steps_before = controller.history().size();
    const auto shown_before = controller.frames_shown();
    const double red_before = mean_red(view->image());
    post_exposure->setValue(std::min(post_exposure->maximum(), post_exposure->value() + 20));
    if (!wait_frames(window, shown_before + 1, 30'000) || !wait_current(window, 30'000)) {
      qCritical("no frame after moving post colour exposure");
      return 8;
    }
    const pixcake::ui::panels::SliderBinding post_binding{"color.post", "exposure"};
    if (controller.history().size() != steps_before + 1 || !controller.parameter_value(post_binding) ||
        !(mean_red(view->image()) > red_before + 1)) {
      qCritical("post colour exposure: %zu -> %zu steps, red %.1f -> %.1f", steps_before, controller.history().size(),
                red_before, mean_red(view->image()));
      return 8;
    }
  }
  if (!show_refine_module(window, "local") || !open_refine_section(window, "local", "影调")) return 8;
  // Local colour (mask list): create a mask, brighten it, give it a linear gradient item (only part of the picture keeps the
  // brightening), cut a radial item out of it, then remove the items and the mask.  Gradients need no segmentation.
  {
    auto& controller = window.controller();
    const auto frames_after = [&](auto&& act) {
      const auto shown = controller.frames_shown();
      act();
      return wait_frames(window, shown + 1, 30'000) && wait_current(window, 30'000);
    };
    const double red_before = mean_red(view->image());
    const auto steps_before = controller.history().size();
    auto* create = visibleChild<QPushButton>(window, "refine-local-new");
    if (create == nullptr || !create->isEnabled() || !controller.local_masks().empty()) {
      qCritical("local mask panel missing or not empty");
      return 8;
    }
    screenshot(window, "local-mask-list-empty");
    create->click();
    if (controller.local_masks() != std::vector<int>{1}) {
      qCritical("creating a mask did not add instance 1");
      return 8;
    }
    // Rename: a valid name is committed as a step of its own; an empty or too long one goes back to the old name.
    {
      auto masks = mask_rows(window);
      if (masks.size() != 1 ||
          visibleChild<QPushButton>(window, "refine-mask-edit") == nullptr) {
        qCritical("the floating mask list should show the new mask with its edit button");
        return 8;
      }
      const auto steps = controller.history().size();
      masks.front()->setText(0, QStringLiteral("test-name"));
      QCoreApplication::processEvents();
      if (controller.parameter_value({"color.local", "m1:name"}) != std::optional<std::string>("test-name") ||
          controller.history().size() != steps + 1) {
        qCritical("renaming the mask did not commit one step");
        return 8;
      }
      masks = mask_rows(window);
      if (masks.size() != 1 || masks.front()->text(0) != QStringLiteral("test-name")) {
        qCritical("the renamed mask is not shown");
        return 8;
      }
      masks.front()->setText(0, QString(21, QLatin1Char('x')));
      QCoreApplication::processEvents();
      masks = mask_rows(window);
      if (controller.parameter_value({"color.local", "m1:name"}) != std::optional<std::string>("test-name") ||
          controller.history().size() != steps + 1 || masks.size() != 1 || masks.front()->text(0) != QStringLiteral("test-name")) {
        qCritical("a too long name should be put back");
        return 8;
      }
    }
    auto* exposure = visibleChild<QSlider>(window, "refine-local-exposure");
    if (exposure == nullptr || !exposure->isEnabled()) {
      qCritical("the new mask has no exposure slider");
      return 8;
    }
    if (!frames_after([&] { exposure->setValue(std::min(exposure->maximum(), exposure->value() + 20)); })) {
      qCritical("no frame after the local exposure");
      return 8;
    }
    const double whole_red = mean_red(view->image());
    if (!(whole_red > red_before + 1) || !controller.parameter_value({"color.local", "m1:exposure"})) {
      qCritical("local exposure on mask 1: red %.1f -> %.1f", red_before, whole_red);
      return 8;
    }
    const auto add_item = [&](const char* region, const char* how) {
      auto* region_box = visibleChild<QComboBox>(window, "refine-local-item-region");
      auto* mode_box = visibleChild<QComboBox>(window, "refine-local-item-mode");
      auto* add = visibleChild<QPushButton>(window, "refine-local-item-add");
      if (region_box == nullptr || mode_box == nullptr || add == nullptr || region_box->findData(QString::fromLatin1(region)) < 0) {
        return false;
      }
      region_box->setCurrentIndex(region_box->findData(QString::fromLatin1(region)));
      mode_box->setCurrentIndex(mode_box->findData(QString::fromLatin1(how)));
      return frames_after([&] { add->click(); });
    };
    if (!add_item("linear", "add")) {
      qCritical("adding the linear item failed");
      return 8;
    }
    const double linear_red = mean_red(view->image());
    if (!(linear_red < whole_red - 1) || !(linear_red > red_before + 1) ||
        visibleChild<QSlider>(window, "refine-local-item-gradient_x") == nullptr) {
      qCritical("linear item: red whole %.1f, linear %.1f, before %.1f", whole_red, linear_red, red_before);
      return 8;
    }
    screenshot(window, "local-mask-list");
    if (!add_item("radial", "subtract")) {
      qCritical("adding the radial subtract item failed");
      return 8;
    }
    const double cut_red = mean_red(view->image());
    if (!(cut_red < linear_red - 0.2)) {
      qCritical("subtracting a radial item changed nothing: %.2f -> %.2f", linear_red, cut_red);
      return 8;
    }
    auto items = selected_mask_items(window);
    auto* drop = visibleChild<QPushButton>(window, "refine-local-item-remove");
    if (!items || items->size() != 2 || drop == nullptr || !drop->isEnabled()) {
      qCritical("the item list should show two items");
      return 8;
    }
    if (!frames_after([&] { drop->click(); }) || !(mean_red(view->image()) > cut_red + 0.2)) {
      qCritical("removing the subtract item did not bring the brightening back");
      return 8;
    }
    // A segmentation part is only recorded here (the default build carries no model).
    const auto steps_mid = controller.history().size();
    auto* remove = visibleChild<QPushButton>(window, "refine-local-delete");
    if (remove == nullptr || !remove->isEnabled() || steps_mid <= steps_before) {
      qCritical("the mask cannot be deleted");
      return 8;
    }
    if (!frames_after([&] { remove->click(); }) || !controller.local_masks().empty() ||
        controller.history().size() != steps_mid + 1 || !(std::abs(mean_red(view->image()) - red_before) < 1.0)) {
      qCritical("deleting the mask: %zu masks, %zu -> %zu steps, red %.1f (was %.1f)", controller.local_masks().size(),
                steps_mid, controller.history().size(), mean_red(view->image()), red_before);
      return 8;
    }
  }
  // Dragging gradients on the canvas: a linear drag creates an item whose parameters follow the two points; a radial
  // drag makes an ellipse from the centre to a corner.  Everything is deleted again at the end.
  {
    auto& controller = window.controller();
    auto* canvas = dynamic_cast<pixcake::shell::PixCakeImageView*>(
        window.findChild<QFrame*>(QStringLiteral("refine-canvas-image")));
    auto* create = visibleChild<QPushButton>(window, "refine-local-new");
    if (canvas == nullptr || create == nullptr) {
      qCritical("canvas or mask panel missing for the drag test");
      return 8;
    }
    create->click();
    auto* draw = visibleChild<QPushButton>(window, "refine-local-draw");
    if (draw == nullptr) {
      qCritical("the draw toggle is missing");
      return 8;
    }
    draw->setChecked(true);
    const auto drag = [&](double x0, double y0, double x1, double y1) {
      const QRectF rect = canvas->imageRect();
      const auto at = [&](double x, double y) { return QPointF(rect.left() + x / 100.0 * rect.width(), rect.top() + y / 100.0 * rect.height()); };
      const auto send = [&](QEvent::Type type, const QPointF& point, Qt::MouseButtons buttons) {
        QMouseEvent event(type, point, canvas->mapToGlobal(point), Qt::LeftButton, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(canvas, &event);
      };
      const auto shown = controller.frames_shown();
      send(QEvent::MouseButtonPress, at(x0, y0), Qt::LeftButton);
      send(QEvent::MouseMove, at((x0 + x1) / 2, (y0 + y1) / 2), Qt::LeftButton);
      send(QEvent::MouseMove, at(x1, y1), Qt::LeftButton);
      send(QEvent::MouseButtonRelease, at(x1, y1), Qt::NoButton);
      return wait_frames(window, shown + 1, 30'000) && wait_current(window, 30'000);
    };
    // The mask needs some brightening to see the gradient: set its exposure first.
    auto* exposure = visibleChild<QSlider>(window, "refine-local-exposure");
    if (exposure == nullptr) {
      qCritical("the new mask has no exposure slider (drag test)");
      return 8;
    }
    exposure->setValue(std::min(exposure->maximum(), exposure->value() + 20));
    wait_current(window, 30'000);
    const double whole_red = mean_red(view->image());
    const auto steps_before_drag = controller.history().size();
    if (!drag(40, 30, 40, 70)) {
      qCritical("no frame after the linear drag");
      return 8;
    }
    if (controller.history().size() != steps_before_drag + 1) {  // creating the item and its shape is one step
      qCritical("a drag should be one history step, got %zu", controller.history().size() - steps_before_drag);
      return 8;
    }
    const auto number = [&](const char* name) {
      const auto value = controller.parameter_value({"color.local", std::string("m") + std::to_string(controller.local_masks().back()) + ":" + name});
      return value ? std::atoi(value->c_str()) : -9999;
    };
    // Straight down from 30% to 70% of the height: centre (40, 50), direction 90 degrees, the length a part of the diagonal.
    const double w = view->image().width();
    const double h = view->image().height();
    const int expected_size = static_cast<int>(std::lround(0.4 * h / std::sqrt(w * w + h * h) * 100.0));
    if (controller.local_masks().empty() || std::abs(number("gradient_x") - 40) > 1 || std::abs(number("gradient_y") - 50) > 1 ||
        std::abs(number("gradient_angle") - 90) > 1 || std::abs(number("gradient_size") - expected_size) > 1) {
      qCritical("linear drag parameters: x %d y %d angle %d size %d (expected size %d)", number("gradient_x"), number("gradient_y"),
                number("gradient_angle"), number("gradient_size"), expected_size);
      return 8;
    }
    // The top is brightened (alpha 1 at the start), the bottom is not: the picture is darker than the whole-image result.
    if (!(mean_red(view->image()) < whole_red - 0.5)) {
      qCritical("the dragged linear gradient did not limit the brightening: %.2f vs %.2f", mean_red(view->image()), whole_red);
      return 8;
    }
    // Drag again: it edits the same item, again as one step (flipped direction = 270 degrees... start at the bottom).
    const auto steps_before_second = controller.history().size();
    if (!drag(40, 70, 40, 30)) {
      qCritical("no frame after the second drag");
      return 8;
    }
    if (controller.history().size() != steps_before_second + 1) {
      qCritical("the second drag should be one history step too");
      return 8;
    }
    if (std::abs(std::abs(number("gradient_angle")) - 90) > 1 || number("gradient_angle") == 90) {
      qCritical("dragging the other way should flip the angle, got %d", number("gradient_angle"));
      return 8;
    }
    auto items = selected_mask_items(window);
    if (!items || items->size() != 1) {
      qCritical("a second drag should edit the item, not add one");
      return 8;
    }
    // Radial: remove the linear item, pick the radial part, drag from the centre to a corner.
    auto* drop = visibleChild<QPushButton>(window, "refine-local-item-remove");
    if (drop == nullptr) {
      qCritical("item remove missing");
      return 8;
    }
    drop->click();
    wait_current(window, 30'000);
    auto* region_box = visibleChild<QComboBox>(window, "refine-local-item-region");
    region_box->setCurrentIndex(region_box->findData(QStringLiteral("radial")));
    draw = visibleChild<QPushButton>(window, "refine-local-draw");
    draw->setChecked(true);
    if (!drag(50, 50, 70, 60)) {
      qCritical("no frame after the radial drag");
      return 8;
    }
    const double rx = 0.2 * w;
    const double ry = 0.1 * h;
    if (std::abs(number("gradient_x") - 50) > 1 || std::abs(number("gradient_y") - 50) > 1 ||
        std::abs(number("gradient_ratio") - static_cast<int>(std::lround(ry / rx * 100.0))) > 2 ||
        std::abs(number("gradient_size") - static_cast<int>(std::lround(2.0 * rx / std::sqrt(w * w + h * h) * 100.0))) > 1) {
      qCritical("radial drag parameters: x %d y %d size %d ratio %d", number("gradient_x"), number("gradient_y"), number("gradient_size"),
                number("gradient_ratio"));
      return 8;
    }
    // Colour range: a click samples the picture (7x7 mean) and commits region + RGB as one step.
    drop = visibleChild<QPushButton>(window, "refine-local-item-remove");
    drop->click();
    wait_current(window, 30'000);
    region_box = visibleChild<QComboBox>(window, "refine-local-item-region");
    region_box->setCurrentIndex(region_box->findData(QStringLiteral("color_range")));
    draw = visibleChild<QPushButton>(window, "refine-local-draw");
    draw->setChecked(true);
    const auto text_of = [&](const char* name) {
      const auto value = controller.parameter_value({"color.local", std::string("m") + std::to_string(controller.local_masks().back()) + ":" + name});
      return value ? *value : std::string();
    };
    const auto steps_before_sample = controller.history().size();
    if (!drag(50, 50, 50, 50)) {
      qCritical("no frame after the colour range click");
      return 8;
    }
    if (controller.history().size() != steps_before_sample + 1 || text_of("region") != "color_range" ||
        text_of("range_r").empty() || text_of("range_g").empty() || text_of("range_b").empty()) {
      qCritical("the colour range click did not commit region + sample as one step (%zu steps, region '%s')",
                controller.history().size() - steps_before_sample, text_of("region").c_str());
      return 8;
    }
    {
      // The sample is the shown colour at the middle of the frame.
      const QImage& shown = view->image();
      const QColor middle = shown.pixelColor(shown.width() / 2, shown.height() / 2);
      if (std::abs(number("range_r") - middle.red()) > 12 || std::abs(number("range_g") - middle.green()) > 12 ||
          std::abs(number("range_b") - middle.blue()) > 12) {
        qCritical("colour range sample %d/%d/%d is far from the picture's %d/%d/%d", number("range_r"), number("range_g"),
                  number("range_b"), middle.red(), middle.green(), middle.blue());
        return 8;
      }
    }
    items = selected_mask_items(window);
    if (!items || items->size() != 1 || visibleChild<QSlider>(window, "refine-local-item-range_adjust") == nullptr) {
      qCritical("the colour range item or its adjust slider is missing");
      return 8;
    }
    // Luminance range: click = full strength around the clicked luminance, a wider ramp outside it.
    drop = visibleChild<QPushButton>(window, "refine-local-item-remove");
    drop->click();
    wait_current(window, 30'000);
    region_box = visibleChild<QComboBox>(window, "refine-local-item-region");
    region_box->setCurrentIndex(region_box->findData(QStringLiteral("luminance_range")));
    draw = visibleChild<QPushButton>(window, "refine-local-draw");
    draw->setChecked(true);
    if (!drag(50, 50, 50, 50)) {
      qCritical("no frame after the luminance range click");
      return 8;
    }
    if (text_of("region") != "luminance_range" || !(number("lum_a") <= number("lum_b") && number("lum_b") <= number("lum_c") &&
                                                   number("lum_c") <= number("lum_d")) ||
        number("lum_c") - number("lum_b") > 20 || number("lum_d") - number("lum_c") > 25) {
      qCritical("luminance range values %d/%d/%d/%d", number("lum_a"), number("lum_b"), number("lum_c"), number("lum_d"));
      return 8;
    }
    // Invert: a checkbox on the selected item; one history step, shown in the item's row.
    {
      auto* invert = visibleChild<QCheckBox>(window, "refine-local-item-invert");
      if (invert == nullptr || invert->isChecked()) {
        qCritical("the invert checkbox is missing or already on");
        return 8;
      }
      const auto steps_before_invert = controller.history().size();
      invert->setChecked(true);
      wait_current(window, 30'000);
      if (text_of("invert") != "on" || controller.history().size() != steps_before_invert + 1) {
        qCritical("inverting the item did not commit one step");
        return 8;
      }
      items = selected_mask_items(window);
      if (!items || items->size() != 1 || !items->front().contains(QStringLiteral("反相"))) {
        qCritical("the inverted item is not marked in the list");
        return 8;
      }
      invert = visibleChild<QCheckBox>(window, "refine-local-item-invert");
      if (invert == nullptr || !invert->isChecked()) {
        qCritical("the invert checkbox lost its state after the rebuild");
        return 8;
      }
    }
    // Brush: pick it in the part list, set the brush, paint two strokes (each one history step), erase one.
    drop = visibleChild<QPushButton>(window, "refine-local-item-remove");
    drop->click();
    wait_current(window, 30'000);
    region_box = visibleChild<QComboBox>(window, "refine-local-item-region");
    region_box->setCurrentIndex(region_box->findData(QStringLiteral("brush")));
    {
      auto* size = visibleChild<QSlider>(window, "refine-local-brush-size");
      auto* feather = visibleChild<QSlider>(window, "refine-local-brush-feather");
      if (size == nullptr || feather == nullptr || visibleChild<QSlider>(window, "refine-local-brush-flow") == nullptr ||
          visibleChild<QSlider>(window, "refine-local-brush-density") == nullptr ||
          visibleChild<QPushButton>(window, "refine-local-brush-erase") == nullptr) {
        qCritical("the brush settings are missing after choosing the brush");
        return 8;
      }
      size->setValue(60);
      feather->setValue(0);
    }
    draw = visibleChild<QPushButton>(window, "refine-local-draw");
    draw->setChecked(true);
    const auto steps_before_stroke = controller.history().size();
    if (!drag(20, 50, 80, 50)) {
      qCritical("no frame after the first brush stroke");
      return 8;
    }
    std::vector<pixcake::mask::BrushStroke> painted;
    if (controller.history().size() != steps_before_stroke + 1 || text_of("region") != "brush" ||
        !pixcake::mask::decode_brush_strokes(text_of("strokes"), painted) || painted.size() != 1 || painted[0].erase ||
        painted[0].feather != 0 || painted[0].points.size() < 2 || std::abs(painted[0].points.front().first - 2000) > 60 ||
        std::abs(painted[0].points.back().first - 8000) > 60) {
      qCritical("the first brush stroke was not committed as one step (%zu steps, strokes '%s')",
                controller.history().size() - steps_before_stroke, text_of("strokes").c_str());
      return 8;
    }
    {
      // 60 image pixels of the preview's long edge, as a fraction (the preview is the picture here).
      const QImage& shown = view->image();
      const int expected = static_cast<int>(std::lround(60.0 / std::max(shown.width(), shown.height()) * 10000.0));
      if (std::abs(painted[0].size - expected) > 2) {
        qCritical("brush size %d, expected about %d", painted[0].size, expected);
        return 8;
      }
    }
    draw = visibleChild<QPushButton>(window, "refine-local-draw");
    draw->setChecked(true);
    if (!drag(20, 30, 80, 30)) {
      qCritical("no frame after the second brush stroke");
      return 8;
    }
    items = selected_mask_items(window);
    if (!pixcake::mask::decode_brush_strokes(text_of("strokes"), painted) || painted.size() != 2 || !items ||
        items->size() != 1) {
      qCritical("the second stroke should extend the same item (%zu strokes)", painted.size());
      return 8;
    }
    auto* erase_tool = visibleChild<QPushButton>(window, "refine-local-brush-erase");
    erase_tool->setChecked(true);
    draw = visibleChild<QPushButton>(window, "refine-local-draw");
    draw->setChecked(true);
    if (!drag(40, 50, 60, 50)) {
      qCritical("no frame after the erasing stroke");
      return 8;
    }
    if (!pixcake::mask::decode_brush_strokes(text_of("strokes"), painted) || painted.size() != 3 || !painted[2].erase) {
      qCritical("the eraser stroke is missing");
      return 8;
    }
    // Undo takes the last stroke back as one step; the history names it without the stroke text.
    const auto rows_now = controller.history();
    if (rows_now.empty() || rows_now.back().text.contains(QLatin1Char(',')) || rows_now.back().text.contains(QLatin1Char(':'))) {
      qCritical("the history step carries the stroke text");
      return 8;
    }
    // Brush shortcuts from the table: ] and [ change the size by 10, the others change their slider; K selects the brush.
    {
      const auto fire_brush = [&window](const char* id) {
        QShortcut* found = nullptr;
        for (auto* candidate : window.findChildren<QShortcut*>(QStringLiteral("shortcut-") + QString::fromLatin1(id))) {
          const auto* owner = qobject_cast<QWidget*>(candidate->parent());
          if (owner != nullptr && owner->isVisibleTo(&window)) found = candidate;
        }
        if (found == nullptr) return false;
        emit found->activated();
        return true;
      };
      const auto value_of = [&](const char* name) {
        auto* slider = visibleChild<QSlider>(window, name);
        return slider != nullptr ? slider->value() : -1;
      };
      const int size_before = value_of("refine-local-brush-size");
      const int feather_before = value_of("refine-local-brush-feather");
      const int flow_before = value_of("refine-local-brush-flow");
      const int density_before = value_of("refine-local-brush-density");
      if (!fire_brush("brush-size-up") || !fire_brush("brush-feather-up") || !fire_brush("brush-flow-down") ||
          !fire_brush("brush-density-down") || value_of("refine-local-brush-size") != size_before + 10 ||
          value_of("refine-local-brush-feather") != feather_before + 10 || value_of("refine-local-brush-flow") != flow_before - 10 ||
          value_of("refine-local-brush-density") != density_before - 10) {
        qCritical("the brush setting shortcuts did not move the sliders");
        return 8;
      }
      fire_brush("brush-size-down");
      if (value_of("refine-local-brush-size") != size_before) {
        qCritical("the size-down shortcut did not undo the size-up");
        return 8;
      }
      // K selects the brush in the part list and turns the canvas drag on.
      region_box = visibleChild<QComboBox>(window, "refine-local-item-region");
      region_box->setCurrentIndex(region_box->findData(QStringLiteral("radial")));
      draw = visibleChild<QPushButton>(window, "refine-local-draw");
      draw->setChecked(false);
      if (!fire_brush("brush-tool") || region_box->currentData().toString() != QStringLiteral("brush") ||
          !visibleChild<QPushButton>(window, "refine-local-draw")->isChecked()) {
        qCritical("the brush tool shortcut did not select the brush");
        return 8;
      }
    }
    // Clean up: delete the mask.
    auto* remove = visibleChild<QPushButton>(window, "refine-local-delete");
    if (remove == nullptr) {
      qCritical("mask delete missing");
      return 8;
    }
    remove->click();
    wait_current(window, 30'000);
    if (!controller.local_masks().empty()) {
      qCritical("the dragged mask was not deleted");
      return 8;
    }
  }
  // Presets: save the photo's colour as a preset through the dialog, apply it (a history step), see it listed.
  // (Needs a project: the development page that opens a bare photo has none.)
  if (window.controller().current_project()) {
    auto& controller = window.controller();
    pixcake::shell::openSavePresetDialog(&window, &controller);
    auto* dialog = window.findChild<QDialog*>(QStringLiteral("preset-save-dialog"));
    auto* name = dialog != nullptr ? dialog->findChild<QLineEdit*>(QStringLiteral("preset-name")) : nullptr;
    auto* tree = dialog != nullptr ? dialog->findChild<QTreeWidget*>(QStringLiteral("preset-tree")) : nullptr;
    auto* save = dialog != nullptr ? dialog->findChild<QPushButton*>(QStringLiteral("preset-save")) : nullptr;
    if (name == nullptr || tree == nullptr || save == nullptr || !save->isEnabled() || tree->topLevelItemCount() == 0) {
      qCritical("preset save dialog missing, or nothing offered to save");
      return 8;
    }
    // A name longer than 20 characters is refused and the dialog stays open with the reason.
    name->setText(QString(21, QLatin1Char('x')));
    save->click();
    auto* problem = dialog->findChild<QLabel*>(QStringLiteral("preset-problem"));
    if (!controller.presets().empty() || problem == nullptr || problem->text().isEmpty()) {
      qCritical("a 21-character preset name was accepted");
      return 8;
    }
    name->setText(QStringLiteral("smoke warm"));
    save->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    const auto presets = controller.presets();
    // The photo also carries a crop fragment now (the crop checks above): colour and crop make a combination preset.
    if (presets.size() != 1 || presets[0].name != "smoke warm" || presets[0].type != pixcake::kPresetCombo ||
        window.findChild<QDialog*>(QStringLiteral("preset-save-dialog")) != nullptr) {
      qCritical("preset not saved: %zu presets", presets.size());
      return 8;
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QListWidget* list = nullptr;
    for (auto* candidate : window.findChildren<QListWidget*>(QStringLiteral("refine-preset-list"))) {
      if (candidate->isVisibleTo(&window)) list = candidate;
    }
    if (list == nullptr || list->count() != 1 || list->item(0)->text() != QStringLiteral("smoke warm")) {
      qCritical("the preset list does not show the saved preset");
      return 8;
    }
    const auto steps_before = controller.history().size();
    controller.request_apply_preset(presets[0].id);
    const auto steps = controller.history();
    if (steps.size() != steps_before + 1 || !steps.back().current ||
        steps.back().text != qtext_for_smoke("history.preset").arg(QStringLiteral("smoke warm")) ||
        !controller.preset_overwrites_color(presets[0].id)) {
      qCritical("applying the preset: %zu -> %zu steps", steps_before, steps.size());
      return 8;
    }
    if (!wait_current(window, 30'000)) {
      qCritical("no frame after applying the preset");
      return 8;
    }
  }
  if (!show_refine_module(window, "history")) return 8;
  // History: original, exposure, profile, grain; the list shows them; undo, redo and a click walk the document.
  {
    auto& controller = window.controller();
    const auto rows = controller.history();
    QListWidget* steps = nullptr;
    for (auto* candidate : window.findChildren<QListWidget*>(QStringLiteral("refine-history-list"))) {
      if (candidate->isVisibleTo(&window)) steps = candidate;
    }
    // (The page was rebuilt by the preset steps above: the old slider pointers are gone.)
    const auto exposure_binding = std::optional<panels::SliderBinding>(panels::SliderBinding{"color.global", "exposure"});
    if (rows.size() < 4 || !rows.back().current || steps == nullptr || steps->count() != static_cast<int>(rows.size()) ||
        !exposure_binding) {
      qCritical("history: %zu rows (last current %d), list %s with %d items", rows.size(), rows.back().current,
                steps == nullptr ? "missing" : "present", steps != nullptr ? steps->count() : -1);
      return 8;
    }
    const auto latest = controller.parameter_value(*exposure_binding);
    controller.request_undo();
    const auto undone = controller.history();
    if (undone.size() != rows.size() || undone[undone.size() - 2].current == false || undone.back().current ||
        controller.parameter_value(*exposure_binding) != latest) {  // exposure was committed before the last step
      qCritical("history: undo did not move back one step");
      return 8;
    }
    controller.request_redo();
    if (!controller.history().back().current) {
      qCritical("history: redo did not return to the last step");
      return 8;
    }
    controller.request_history_step(0);  // the original: exposure is not set
    if (!controller.history().front().current ||
        controller.parameter_value(*exposure_binding) == latest) {
      qCritical("history: the original step still carries the exposure");
      return 8;
    }
    controller.request_history_step(rows.size() - 1);
    if (!controller.history().back().current || controller.parameter_value(*exposure_binding) != latest) {
      qCritical("history: could not return to the last step");
      return 8;
    }
    if (!wait_current(window, 30'000)) {
      qCritical("history: no frame after the jumps");
      return 8;
    }
  }
  // Development aid: PIXCAKE_SMOKE_SCREENSHOT=<file.png> saves the window as rendered offscreen.
  if (const char* shot = std::getenv("PIXCAKE_SMOKE_SCREENSHOT"); shot != nullptr && *shot != '\0') {
    window.resize(1386, 800);
    QCoreApplication::processEvents();
    window.grab().save(QString::fromLocal8Bit(shot));
  }
  std::printf("open photo ok: %dx%d, %d sliders enabled, red %.1f -> %.1f, black-and-white and grain drawn\n",
              frame_size.width(), frame_size.height(), enabled, before, after);
  return 0;
}
#endif

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
  for (const auto& page : {QStringLiteral("home"), QStringLiteral("local-projects"), QStringLiteral("library"),
                           QStringLiteral("refine"), QStringLiteral("creative")}) {
    if (executable.endsWith(QStringLiteral("-") + page)) default_page = page;
  }
  QCommandLineOption page_option(QStringList() << QStringLiteral("page"),
                                 QStringLiteral("Development build only: display a read-only shell page: login, "
                                                "home, local-projects, library, refine, creative."),
                                 QStringLiteral("page"), default_page);
  parser.addOption(page_option);
  QCommandLineOption dev_sign_in_option(QStringList() << QStringLiteral("dev-sign-in"),
                                       QStringLiteral("Development build only: treat the session as signed in."));
  parser.addOption(dev_sign_in_option);
  QCommandLineOption data_option(QStringList() << QStringLiteral("data-dir"),
                                 QStringLiteral("Development build only: project library directory."),
                                 QStringLiteral("dir"));
  parser.addOption(data_option);
  QCommandLineOption import_option(QStringList() << QStringLiteral("import"),
                                   QStringLiteral("Development build only: import these photos/folders from home "
                                                  "(as the import dialog would); with --smoke, check the flow."),
                                   QStringLiteral("path"));
  parser.addOption(import_option);
  QCommandLineOption open_option(QStringList() << QStringLiteral("open"),
                                 QStringLiteral("Development build only: open a photo in the refine page."),
                                 QStringLiteral("photo"));
  parser.addOption(open_option);
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
  pixcake::platform::MacSourceAccess source_access;
  QString data_directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#ifdef PIXCAKE_DEV_PAGES
  if (parser.isSet(dev_sign_in_option) && account.sign_in(dev_account()) != pixcake::ResultCode::Ok) {
    std::fprintf(stderr, "dev sign-in failed\n");
    return 9;
  }
  if (parser.isSet(data_option)) data_directory = QFileInfo(parser.value(data_option)).absoluteFilePath();
  const bool open_photo = parser.isSet(open_option);
  const Page first_page = open_photo ? Page::Refine : parse_page(parser.value(page_option));
  pixcake::shell::ShellWindow window(first_page == Page::Login ? ShellDisplay::for_session(account)
                                                               : ShellDisplay::dev_preview(first_page));
  if (open_photo) {
    std::string diagnostic;
    const auto photo = QFileInfo(parser.value(open_option)).absoluteFilePath().toStdString();
    auto options = preview_options();
    // The smoke check edits a throwaway document, never the user's.
    QTemporaryDir smoke_documents;
    if (parser.isSet(smoke_option)) options.document_directory = smoke_documents.path().toStdString();
    QDir().mkpath(QString::fromStdString(options.document_directory));
    if (window.openPhoto(photo, options, diagnostic) != pixcake::ResultCode::Ok) {
      std::fprintf(stderr, "cannot open %s: %s\n", photo.c_str(), diagnostic.c_str());
      if (parser.isSet(smoke_option)) return 8;
    }
    if (parser.isSet(smoke_option)) return smoke_open_photo(window);
  }
  // Smoke checks never open the user's project library; they pass --data-dir.
  if (!parser.isSet(smoke_option) || parser.isSet(data_option)) {
    attach_projects(window, account, source_access, data_directory);
  }
  if (parser.isSet(import_option)) {
    std::vector<std::string> paths;
    for (const auto& value : parser.values(import_option)) paths.push_back(QFileInfo(value).absoluteFilePath().toStdString());
    if (parser.isSet(smoke_option)) {
      return smoke_import_flow(window, paths, pixcake::project_session::project_name_for(paths));
    }
    window.controller().request_import(paths);
  }
#else
  const Page first_page = Page::Login;
  pixcake::shell::ShellWindow window(ShellDisplay::for_session(account));
  if (!parser.isSet(smoke_option)) attach_projects(window, account, source_access, data_directory);
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
