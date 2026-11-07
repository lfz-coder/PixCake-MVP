#include "export_dialog.hpp"

#include "pixcake_widgets.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QMessageBox>
#include <QPointer>
#include <QRadioButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QTabBar>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace pixcake::shell {
namespace {

constexpr int kDefaultQuality = 12;  // 实测默认 12，显示为“最佳”

// 档位与数值：只实测到 12 =“最佳”；低 3、中 6、高 9 是本项目暂定的（证据文档“没测”一节）。
constexpr int kLevelValues[] = {3, 6, 9, 12};
int levelOf(int quality) {
  if (quality >= 12) return 3;
  if (quality >= 9) return 2;
  if (quality >= 6) return 1;
  return 0;
}

ExportFormat formatOf(const std::string& id) {
  if (id == "jpg") return ExportFormat::Jpg;
  if (id == "tiff") return ExportFormat::Tiff;
  if (id == "tiff16") return ExportFormat::Tiff16;
  return ExportFormat::Original;
}

std::string formatId(ExportFormat format) {
  switch (format) {
    case ExportFormat::Jpg: return "jpg";
    case ExportFormat::Tiff: return "tiff";
    case ExportFormat::Tiff16: return "tiff16";
    case ExportFormat::Original: break;
  }
  return "original";
}

// 还没接入的项：控件在、置灰，悬停和辅助功能说明给出原因。
void unavailable(QWidget* widget, const char* why) {
  widget->setEnabled(false);
  widget->setToolTip(qtext(why));
  widget->setAccessibleDescription(qtext(why));
}

QLabel* sectionLabel(const QString& value) {
  auto* label = new QLabel(value);
  label->setProperty("muted", true);
  return label;
}

// 默认导出文件夹：原图文件夹旁边的“项目名_导出”（实测名字是“项目名_导出”；放在哪里没核实）。
QString defaultFolder(const ExportTarget& target) {
  if (!target.defaults.folder.empty()) return qs(target.defaults.folder);
  if (target.original_folder.empty()) return QDir::homePath();
  const QFileInfo original(qs(target.original_folder));
  const QString name = target.project_name.empty() ? original.fileName() : qs(target.project_name);
  return original.absolutePath() + QLatin1Char('/') + name + qtext("export.folder.suffix");
}

class ExportDialog final : public QDialog {
 public:
  ExportDialog(QWidget* parent, PageActions* actions, ExportTarget target)
      : QDialog(parent), actions_(actions), target_(std::move(target)) {
    setObjectName(QStringLiteral("export-dialog"));
    setAccessibleName(qs(copyItem("library-toolbar", "export").text));
    setWindowTitle(accessibleName());
    setModal(true);
    auto* outer = new QVBoxLayout(this);

    tabs_ = new QTabBar(this);
    tabs_->setObjectName(QStringLiteral("export-tabs"));
    for (const auto& entry : ui::shell::page_list("export-tabs")) tabs_->addTab(qs(entry.text));
    tabs_->setTabEnabled(2, false);
    tabs_->setTabToolTip(2, qtext("export.why.print"));
    tabs_->setAccessibleDescription(qtext("export.why.print"));
    outer->addWidget(tabs_);

    auto* body = new QHBoxLayout;
    preview_ = new QLabel(this);
    preview_->setObjectName(QStringLiteral("export-preview"));
    preview_->setFixedSize(320, 320);
    preview_->setAlignment(Qt::AlignCenter);
    const QImage thumb = target_.photo_ids.empty() ? QImage() : actions_->thumbnail(target_.photo_ids.front());
    if (!thumb.isNull()) preview_->setPixmap(QPixmap::fromImage(thumb).scaled(320, 320, Qt::KeepAspectRatio));
    body->addWidget(preview_, 0, Qt::AlignTop);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* form = new QWidget(scroll);
    form->setObjectName(QStringLiteral("export-form"));
    auto* column = new QVBoxLayout(form);
    buildCommon(column);
    advanced_ = buildAdvanced();
    column->addWidget(advanced_);
    watermark_ = buildWatermark();
    column->addWidget(watermark_);
    column->addStretch();
    scroll->setWidget(form);
    body->addWidget(scroll, 1);
    outer->addLayout(body, 1);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* cancel = new PixCakeButton(qtext("export.cancel"), this);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    auto* submit = new PixCakeButton(qtext("export.submit"), this, QStringLiteral("primary"));
    submit->setDefault(true);
    connect(submit, &QPushButton::clicked, this, [this] { submit_(); });
    buttons->addWidget(cancel);
    buttons->addWidget(submit);
    outer->addLayout(buttons);

    connect(tabs_, &QTabBar::currentChanged, this, [this](int) { showTab(); });
    tabs_->setCurrentIndex(0);
    showTab();
  }

 private:
  void buildCommon(QVBoxLayout* column) {
    column->addWidget(text(qtext("export.heading").arg(target_.photo_ids.size())));
    auto* form = new QFormLayout;
    preset_ = new QComboBox(this);
    preset_->addItem(qtext("export.preset.placeholder"));
    preset_->setAccessibleName(qtext("export.preset"));
    unavailable(preset_, "export.why.preset");
    form->addRow(qtext("export.preset"), preset_);

    destination_ = new QComboBox(this);
    destination_->setObjectName(QStringLiteral("export-destination"));
    destination_->setAccessibleName(qtext("export.to"));
    for (const auto& entry : ui::shell::page_list("export-destinations")) {
      destination_->addItem(qs(entry.text), qs(entry.id));
    }
    form->addRow(qtext("export.to"), destination_);
    column->addLayout(form);

    auto* path_row = new QHBoxLayout;
    folder_ = new PixCakeField(qtext("export.to"), this);
    folder_->setObjectName(QStringLiteral("export-folder"));
    folder_->setText(defaultFolder(target_));
    choose_ = new PixCakeButton(qtext("export.choose"), this);
    connect(choose_, &QPushButton::clicked, this, [this] {
      const QString chosen = QFileDialog::getExistingDirectory(this, qtext("export.choose"), folder_->text());
      if (!chosen.isEmpty()) folder_->setText(chosen);
    });
    path_row->addWidget(folder_, 1);
    path_row->addWidget(choose_);
    column->addLayout(path_row);
    connect(destination_, &QComboBox::currentIndexChanged, this, [this] { showDestination(); });

    upload_ = new QCheckBox(qtext("export.upload"), this);
    unavailable(upload_, "export.why.upload");
    column->addWidget(upload_);
  }

  QWidget* buildAdvanced() {
    auto* frame = new QWidget(this);
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(0, 8, 0, 0);
    auto* toggle = new QToolButton(frame);
    toggle->setObjectName(QStringLiteral("export-advanced"));
    toggle->setText(qtext("export.advanced"));
    toggle->setAccessibleName(qtext("export.advanced"));
    toggle->setCheckable(true);
    toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toggle->setArrowType(Qt::RightArrow);
    layout->addWidget(toggle);
    auto* content = new QWidget(frame);
    auto* form = new QFormLayout(content);
    content->setVisible(false);  // 实测默认收起
    connect(toggle, &QToolButton::toggled, content, [toggle, content](bool open) {
      content->setVisible(open);
      toggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    });
    layout->addWidget(content);

    // 导出位置：设为默认导出路径、新建项目文件夹存储默认勾选，保留导入时的完整路径不勾（实测）。
    auto* location = new QVBoxLayout;
    remember_folder_ = new QCheckBox(qtext("export.location.default"), content);
    remember_folder_->setChecked(true);
    project_folder_ = new QCheckBox(qtext("export.location.project"), content);
    project_folder_->setChecked(true);
    auto* keep = new QCheckBox(qtext("export.location.keep"), content);
    unavailable(keep, "export.why.keep_path");
    location->addWidget(remember_folder_);
    location->addWidget(project_folder_);
    location->addWidget(keep);
    form->addRow(qtext("export.location"), location);

    auto* format_row = new QHBoxLayout;
    format_ = new QComboBox(content);
    format_->setObjectName(QStringLiteral("export-format"));
    format_->setAccessibleName(qtext("export.format"));
    for (const auto& entry : ui::shell::page_list("export-formats")) format_->addItem(qs(entry.text), qs(entry.id));
    const std::string remembered = formatId(target_.defaults.format.value_or(ExportFormat::Original));
    format_->setCurrentIndex(std::max(0, format_->findData(qs(remembered))));
    remember_format_ = new QCheckBox(qtext("export.format.default"), content);
    format_row->addWidget(format_);
    format_row->addWidget(remember_format_);
    form->addRow(qtext("export.format"), format_row);

    quality_group_ = new QGroupBox(qtext("export.quality.group"), content);
    auto* quality_layout = new QVBoxLayout(quality_group_);
    auto* choice = new QButtonGroup(quality_group_);
    auto* size_row = new QHBoxLayout;
    auto* by_size = new QRadioButton(qtext("export.size_limit"), quality_group_);
    auto* size = new QDoubleSpinBox(quality_group_);
    size->setSuffix(QStringLiteral(" MB"));
    size->setValue(10.0);  // 实测默认 10.00 MB
    unavailable(by_size, "export.why.size_limit");
    unavailable(size, "export.why.size_limit");
    choice->addButton(by_size);
    size_row->addWidget(by_size);
    size_row->addWidget(size);
    size_row->addStretch();
    quality_layout->addLayout(size_row);

    auto* value_row = new QHBoxLayout;
    auto* by_quality = new QRadioButton(qtext("export.quality"), quality_group_);
    by_quality->setChecked(true);
    choice->addButton(by_quality);
    quality_ = new QSpinBox(quality_group_);
    quality_->setObjectName(QStringLiteral("export-quality"));
    quality_->setAccessibleName(qtext("export.quality"));
    quality_->setRange(0, 14);
    quality_->setValue(kDefaultQuality);
    level_ = new QComboBox(quality_group_);
    level_->setAccessibleName(qtext("export.quality"));
    for (const auto& entry : ui::shell::page_list("export-quality-levels")) level_->addItem(qs(entry.text));
    value_row->addWidget(by_quality);
    value_row->addWidget(quality_);
    value_row->addWidget(level_);
    value_row->addStretch();
    quality_layout->addLayout(value_row);

    auto* slider_row = new QHBoxLayout;
    slider_ = new QSlider(Qt::Horizontal, quality_group_);
    slider_->setRange(0, 14);
    slider_->setAccessibleName(qtext("export.quality"));
    slider_row->addWidget(sectionLabel(qtext("export.quality.small")));
    slider_row->addWidget(slider_, 1);
    slider_row->addWidget(sectionLabel(qtext("export.quality.large")));
    quality_layout->addLayout(slider_row);
    form->addRow(quality_group_);
    // 数值框、档位、滑杆三者联动。
    connect(quality_, &QSpinBox::valueChanged, this, [this](int value) { setQuality(value); });
    connect(slider_, &QSlider::valueChanged, this, [this](int value) { setQuality(value); });
    connect(level_, &QComboBox::activated, this, [this](int index) { setQuality(kLevelValues[index]); });
    setQuality(kDefaultQuality);
    connect(format_, &QComboBox::currentIndexChanged, this, [this] { showFormat(); });
    showFormat();

    srgb_ = new QCheckBox(qtext("export.srgb"), content);
    srgb_->setObjectName(QStringLiteral("export-srgb"));
    form->addRow(qtext("export.color"), srgb_);
    auto* overlay = new QCheckBox(qtext("export.overlay"), content);
    unavailable(overlay, "export.why.overlay");
    form->addRow(overlay);
    return frame;
  }

  QWidget* buildWatermark() {
    auto* group = new QGroupBox(qtext("export.watermark"), this);
    group->setObjectName(QStringLiteral("export-watermark"));
    group->setCheckable(true);
    group->setChecked(false);  // 实测默认关
    unavailable(group, "export.why.watermark");
    return group;
  }

  void setQuality(int value) {
    if (updating_) return;
    updating_ = true;
    quality_->setValue(value);
    slider_->setValue(value);
    level_->setCurrentIndex(levelOf(value));
    updating_ = false;
  }

  void showFormat() {
    // 品质只对 JPG 有意义；原始格式下这一组也显示（实测），TIFF 下置灰。
    const ExportFormat format = formatOf(format_->currentData().toString().toStdString());
    quality_group_->setEnabled(format == ExportFormat::Jpg || format == ExportFormat::Original);
  }

  void showDestination() {
    const bool original = destination_->currentData().toString() == QStringLiteral("original");
    folder_->setEnabled(!original);
    choose_->setEnabled(!original);
    if (original) {
      folder_->setText(qs(target_.original_folder));
    } else if (folder_->text() == qs(target_.original_folder)) {
      folder_->setText(defaultFolder(target_));
    }
  }

  void showTab() {
    const bool custom = tabs_->currentIndex() == 1;
    preview_->setVisible(custom);
    advanced_->setVisible(custom);
    watermark_->setVisible(custom);  // 快速导出页没有水印（实测）
    resize(custom ? QSize(902, 650) : QSize(480, 449));  // 实测两页的窗口大小
  }

  void submit_() {
    ExportRequest request;
    request.photo_ids = target_.photo_ids;
    request.original_folder = destination_->currentData().toString() == QStringLiteral("original");
    if (!request.original_folder) {
      const QString folder = QDir::cleanPath(folder_->text().trimmed());
      if (!QDir::isAbsolutePath(folder) || !QDir().mkpath(folder)) {
        QMessageBox::warning(this, windowTitle(), qtext("export.queue.unavailable"));
        return;
      }
      request.folder = folder.toStdString();
    }
    if (project_folder_->isChecked()) request.subfolder = target_.project_name;
    request.settings.format = formatOf(format_->currentData().toString().toStdString());
    if (quality_group_->isEnabled()) request.settings.quality = quality_->value();
    request.settings.convert_to_srgb = srgb_->isChecked();

    ExportDefaults defaults = target_.defaults;
    if (remember_folder_->isChecked() && !request.original_folder) defaults.folder = request.folder;
    if (remember_format_->isChecked()) defaults.format = request.settings.format;
    bool ok = false;
    const auto notices = actions_->request_export(request, defaults, ok);
    if (!notices.empty()) {
      QStringList lines;
      for (const auto& notice : notices) lines << qs(notice);
      QMessageBox::information(this, windowTitle(), qtext("export.refused") + QStringLiteral("\n") + lines.join('\n'));
    } else if (!ok) {
      QMessageBox::warning(this, windowTitle(), qtext("export.queue.unavailable"));
      return;
    }
    accept();
    if (ok) openExportList(parentWidget(), actions_);
  }

  PageActions* actions_;
  ExportTarget target_;
  bool updating_ = false;
  QTabBar* tabs_ = nullptr;
  QLabel* preview_ = nullptr;
  QWidget* advanced_ = nullptr;
  QWidget* watermark_ = nullptr;
  QComboBox* preset_ = nullptr;
  QComboBox* destination_ = nullptr;
  PixCakeField* folder_ = nullptr;
  QPushButton* choose_ = nullptr;
  QCheckBox* upload_ = nullptr;
  QCheckBox* remember_folder_ = nullptr;
  QCheckBox* project_folder_ = nullptr;
  QComboBox* format_ = nullptr;
  QCheckBox* remember_format_ = nullptr;
  QGroupBox* quality_group_ = nullptr;
  QSpinBox* quality_ = nullptr;
  QComboBox* level_ = nullptr;
  QSlider* slider_ = nullptr;
  QCheckBox* srgb_ = nullptr;
};

class ExportList final : public QDialog {
 public:
  ExportList(QWidget* parent, PageActions* actions) : QDialog(parent), actions_(actions) {
    setObjectName(QStringLiteral("export-list-dialog"));
    setAccessibleName(qs(copyItem("global", "export-list").text));
    setWindowTitle(accessibleName());
    setAttribute(Qt::WA_DeleteOnClose);
    resize(760, 420);
    auto* layout = new QVBoxLayout(this);
    empty_ = text(qtext("export.list.empty"), true);
    layout->addWidget(empty_);
    table_ = new QTableWidget(this);
    table_->setObjectName(QStringLiteral("export-list"));
    table_->setAccessibleName(accessibleName());
    const auto& columns = ui::shell::page_list("export-list-columns");
    table_->setColumnCount(static_cast<int>(columns.size()) + 1);
    QStringList headers;
    for (const auto& entry : columns) headers << qs(entry.text);
    headers << QString();
    table_->setHorizontalHeaderLabels(headers);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    // Cell widgets do not size their column: fixed room for the longest action, rows tall enough for a button.
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    table_->setColumnWidth(4, 140);
    table_->verticalHeader()->setDefaultSectionSize(44);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(table_, 1);
    actions_->watch_exports(this, [this] { refresh(); });
    // 导出进程每走一步都写导出日志，进度消息之外也定时读一次。
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] { refresh(); });
    timer->start(500);
    refresh();
  }

 private:
  void refresh() {
    const auto rows = actions_->export_rows();
    empty_->setVisible(rows.empty());
    table_->setVisible(!rows.empty());
    table_->setRowCount(static_cast<int>(rows.size()));
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
      const auto& row = rows[static_cast<std::size_t>(i)];
      const QStringList cells = {qs(row.file_name), qs(row.status), qs(row.reason), qs(row.destination_path)};
      for (int column = 0; column < cells.size(); ++column) {
        auto* item = table_->item(i, column);
        if (item == nullptr) {
          item = new QTableWidgetItem;
          table_->setItem(i, column, item);
        }
        if (item->text() != cells[column]) item->setText(cells[column]);
        item->setToolTip(cells[column]);
      }
      // Replace the button only when the row's action changes: the list refreshes every half second.
      const QString key = qs(row.export_id) + QLatin1Char('/') + actionKind(row);
      QWidget* current = table_->cellWidget(i, 4);
      if (current == nullptr ? !actionKind(row).isEmpty() : current->property("action-key").toString() != key) {
        QWidget* action = actionFor(row);
        if (action != nullptr) action->setProperty("action-key", key);
        table_->setCellWidget(i, 4, action);
      }
    }
  }

  static QString actionKind(const ExportRow& row) {
    if (row.can_retry) return QStringLiteral("retry");
    if (row.can_cancel) return QStringLiteral("cancel");
    if (row.succeeded) return QStringLiteral("open");
    return {};
  }

  QWidget* actionFor(const ExportRow& row) {
    const std::string id = row.export_id;
    PageActions* actions = actions_;
    if (row.can_retry) {
      auto* retry = new PixCakeButton(qtext("export.list.retry"));
      connect(retry, &QPushButton::clicked, this, [actions, id] { actions->request_export_retry(id); });
      return retry;
    }
    if (row.can_cancel) {
      auto* cancel = new PixCakeButton(qtext("export.list.cancel"));
      connect(cancel, &QPushButton::clicked, this, [actions, id] { actions->request_export_cancel(id); });
      return cancel;
    }
    if (row.succeeded) {
      auto* open = new PixCakeButton(qtext("export.list.open"));
      const QString folder = QFileInfo(qs(row.destination_path)).absolutePath();
      connect(open, &QPushButton::clicked, this, [folder] { QDesktopServices::openUrl(QUrl::fromLocalFile(folder)); });
      return open;
    }
    return nullptr;
  }

  PageActions* actions_;
  QLabel* empty_ = nullptr;
  QTableWidget* table_ = nullptr;
};

}  // namespace

void openExportDialog(QWidget* parent, PageActions* actions) {
  const auto target = actions->export_target();
  if (!target) return;
  ExportDialog dialog(parent, actions, *target);
  dialog.exec();
}

void openExportList(QWidget* parent, PageActions* actions) {
  if (!actions->has_exports()) return;
  QWidget* window = parent != nullptr ? parent->window() : nullptr;
  if (window != nullptr) {
    if (auto* open = window->findChild<QDialog*>(QStringLiteral("export-list-dialog"))) {
      open->raise();
      open->activateWindow();
      return;
    }
  }
  auto* list = new ExportList(window, actions);
  list->show();
}

QPushButton* toolbarAction(std::string_view list, const CopyItem& item, QWidget* parent, PageActions* actions) {
  if (item.id == "export") {
    auto* button = itemAction(list, item, parent, actions->export_target().has_value());
    QObject::connect(button, &QPushButton::clicked, button, [button, actions] { openExportDialog(button->window(), actions); });
    return button;
  }
  if (item.id == "export-list") {
    auto* button = itemAction(list, item, parent, actions->has_exports());
    QObject::connect(button, &QPushButton::clicked, button, [button, actions] { openExportList(button->window(), actions); });
    return button;
  }
  return itemAction(list, item, parent);
}

}  // namespace pixcake::shell
