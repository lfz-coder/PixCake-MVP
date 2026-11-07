#include "pixcake/account/account_session.hpp"
#include "pixcake/instance/single_instance.hpp"
#include "pixcake/logging/file_log_sink.hpp"
#include "pixcake/platform/mac_source_access.hpp"
#include "pixcake/project_session/project_session.hpp"
#include "pixcake/ui/panels/slider_spec.hpp"
#include "pixcake/ui/shell/page_copy.hpp"
#include "pixcake/ui/shell/shell_display.hpp"
#include "pixcake_theme.hpp"
#include "pixcake_widgets.hpp"
#include "shell_window.hpp"

#include <QApplication>
#include <QDialog>
#include <QScrollArea>
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
#include <QMenu>
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
    if (tabs == nullptr || tabs->count() != 3 || tabs->isTabEnabled(2) || tabs->tabToolTip(2).isEmpty() ||
        watermark == nullptr || !watermark->isEnabled() || !watermark->isChecked() || material_button == nullptr ||
        material_button->toolTip() != qtext_for_smoke("export.wm.replace") || portrait == nullptr ||
        !portrait->isChecked() || centre == nullptr || top_left == nullptr || x == nullptr || y == nullptr ||
        format == nullptr || format->count() != 4 || path == nullptr) {
      problem = QStringLiteral("export dialog controls differ from the observed dialog");
      dialog->reject();
      return;
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
      window.findChild<QFrame*>(QStringLiteral("library-single-image")));
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
  const auto* view =
      dynamic_cast<const pixcake::shell::PixCakeImageView*>(window.findChild<QFrame*>(QStringLiteral("refine-canvas-image")));
  const auto* chart =
      dynamic_cast<const pixcake::shell::PixCakeHistogram*>(window.findChild<QFrame*>(QStringLiteral("refine-histogram")));
  if (view == nullptr || view->image().isNull() || chart == nullptr || chart->histogram().peak == 0) {
    qCritical("canvas or histogram is empty");
    return 8;
  }
  const double before = mean_red(view->image());
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
  // Development aid: PIXCAKE_SMOKE_SCREENSHOT=<file.png> saves the window as rendered offscreen.
  if (const char* shot = std::getenv("PIXCAKE_SMOKE_SCREENSHOT"); shot != nullptr && *shot != '\0') {
    window.resize(1386, 800);
    QCoreApplication::processEvents();
    window.grab().save(QString::fromLocal8Bit(shot));
  }
  std::printf("open photo ok: %dx%d, %d sliders enabled, red %.1f -> %.1f, black-and-white and grain drawn\n",
              view->image().width(), view->image().height(), enabled, before, after);
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
  for (const auto& page : {QStringLiteral("home"), QStringLiteral("library"), QStringLiteral("refine"),
                           QStringLiteral("creative")}) {
    if (executable.endsWith(QStringLiteral("-") + page)) default_page = page;
  }
  QCommandLineOption page_option(QStringList() << QStringLiteral("page"),
                                 QStringLiteral("Development build only: display a read-only shell page: login, "
                                                "home, library, refine, creative."),
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
