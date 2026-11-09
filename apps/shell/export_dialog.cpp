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
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
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

#include <algorithm>
#include <array>
#include <cmath>

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
    // 数量统计、预设、导出至那一块；排版印刷页签右侧只有排版面板（实测），整块藏起来，导出仍用这里的路径。
    common_ = new QWidget(form);
    common_->setObjectName(QStringLiteral("export-common"));
    auto* common_column = new QVBoxLayout(common_);
    common_column->setContentsMargins(0, 0, 0, 0);
    buildCommon(common_column);
    column->addWidget(common_);
    advanced_ = buildAdvanced();
    column->addWidget(advanced_);
    watermark_ = buildWatermark();
    column->addWidget(watermark_);
    layout_ = buildLayout();
    column->addWidget(layout_);
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
    updatePreview();
  }

 private:
  void buildCommon(QVBoxLayout* column) {
    column->addWidget(text(qtext("export.heading").arg(target_.photo_ids.size())));
    // 三类统计和计费提示（test 版快速导出页、自定设置页都有；张数为 0 的置灰）。
    auto* counts = new QHBoxLayout;
    const auto& estimate = target_.estimate;
    const std::pair<const char*, int> kinds[] = {
        {"refined", estimate.refined}, {"free", estimate.free}, {"original", estimate.original}};
    for (const auto& [kind, count] : kinds) {
      QLabel* stat = text(qtext((std::string("export.count.") + kind).c_str()).arg(count), count == 0);
      stat->setObjectName(QStringLiteral("export-count-") + QLatin1String(kind));
      counts->addWidget(stat);
    }
    counts->addStretch();
    column->addLayout(counts);
    column->addWidget(text(qtext("export.estimate.note"), true));
    QLabel* sheets = text(qtext("export.sheets").arg(estimate.sheets).arg(target_.remaining_sheets),
                          estimate.sheets <= target_.remaining_sheets);
    sheets->setObjectName(QStringLiteral("export-sheets"));
    if (estimate.sheets > target_.remaining_sheets) sheets->setProperty("warning", true);
    column->addWidget(sheets);
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
    by_size_ = new QRadioButton(qtext("export.size_limit"), quality_group_);
    by_size_->setObjectName(QStringLiteral("export-by-size"));
    size_ = new QDoubleSpinBox(quality_group_);
    size_->setObjectName(QStringLiteral("export-size-limit"));
    size_->setAccessibleName(qtext("export.size_limit"));
    size_->setSuffix(QStringLiteral(" MB"));
    size_->setDecimals(2);
    size_->setRange(0.01, 1000.0);  // 1 MB = 1024 KB；上限是本项目定的
    size_->setValue(10.0);           // 实测默认 10.00 MB
    choice->addButton(by_size_);
    size_row->addWidget(by_size_);
    size_row->addWidget(size_);
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
    // 二选一：没选中的一边置灰（实测：未选时文件大小数值框置灰）。
    connect(by_size_, &QRadioButton::toggled, this, [this] { showLimitChoice(); });
    showLimitChoice();
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

  // 一行“名称 + 滑杆 + 数值框”，两者联动。
  struct SliderRow {
    QSlider* slider = nullptr;
    QSpinBox* spin = nullptr;
  };
  SliderRow sliderRow(QFormLayout* layout, QWidget* parent, const char* name, const char* object, int low, int high) {
    SliderRow row;
    auto* line = new QHBoxLayout;
    row.slider = new QSlider(Qt::Horizontal, parent);
    row.slider->setRange(low, high);
    row.slider->setAccessibleName(qtext(name));
    row.spin = new QSpinBox(parent);
    row.spin->setRange(low, high);
    row.spin->setObjectName(QString::fromLatin1(object));
    row.spin->setAccessibleName(qtext(name));
    connect(row.slider, &QSlider::valueChanged, row.spin, &QSpinBox::setValue);
    connect(row.spin, &QSpinBox::valueChanged, row.slider, &QSlider::setValue);
    connect(row.spin, &QSpinBox::valueChanged, this, [this] { storePlacement(); });
    line->addWidget(row.slider, 1);
    line->addWidget(row.spin);
    layout->addRow(qtext(name), line);
    return row;
  }

  // 水印（test 版实测，docs/superpowers/evidence/2026-11-07-watermark-observation.md）：标题上的勾选就是开关；
  // 横图 / 竖图 / 方图三套参数，素材共用；九宫格选位置时两个位置值按格子重置。
  QWidget* buildWatermark() {
    auto* group = new QGroupBox(qtext("export.watermark"), this);
    group->setObjectName(QStringLiteral("export-watermark"));
    group->setAccessibleName(qtext("export.watermark"));
    group->setCheckable(true);
    group->setChecked(target_.defaults.watermark_on);  // 实测默认关；之后记住上次的
    if (target_.defaults.watermark) {
      material_ = target_.defaults.watermark->material;
      placements_ = target_.defaults.watermark->placements;
    }
    auto* layout = new QFormLayout(group);

    auto* shapes_row = new QHBoxLayout;
    shapes_ = new QButtonGroup(group);
    int index = 0;
    for (const auto& entry : ui::shell::page_list("export-wm-shapes")) {
      auto* button = new QToolButton(group);
      button->setObjectName(QStringLiteral("export-wm-shape-") + qs(entry.id));
      button->setText(qs(entry.text));
      button->setAccessibleName(qs(entry.text));
      button->setCheckable(true);
      shapes_->addButton(button, index++);
      shapes_row->addWidget(button);
    }
    shapes_row->addStretch();
    // 水印同步（test 版实测）：把当前方向的全部摆放抄给另外两个方向，没有确认框。
    sync_ = new PixCakeButton(qtext("export.wm.sync"), group);
    sync_->setObjectName(QStringLiteral("export-wm-sync"));
    connect(sync_, &QPushButton::clicked, this, [this] {
      const auto source = placements_[static_cast<std::size_t>(photo_shape_)];
      for (auto& placement : placements_) placement = source;
      loadPlacement();
    });
    shapes_row->addWidget(sync_);
    layout->addRow(shapes_row);

    material_button_ = new QToolButton(group);
    material_button_->setObjectName(QStringLiteral("export-wm-material"));
    material_button_->setAccessibleName(qtext("export.wm.material"));
    material_button_->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    material_button_->setIconSize(QSize(120, 60));
    material_button_->setMinimumWidth(200);
    connect(material_button_, &QToolButton::clicked, this, [this] { chooseMaterial(); });
    layout->addRow(qtext("export.wm.material"), material_button_);

    auto* rotate = new QHBoxLayout;
    for (const bool clockwise : {false, true}) {
      auto* button = new QToolButton(group);
      const QString name = qtext(clockwise ? "export.wm.cw" : "export.wm.ccw");
      button->setObjectName(clockwise ? QStringLiteral("export-wm-cw") : QStringLiteral("export-wm-ccw"));
      button->setText(clockwise ? QStringLiteral("\u21bb") : QStringLiteral("\u21ba"));
      button->setToolTip(name);
      button->setAccessibleName(name);
      connect(button, &QToolButton::clicked, this, [this, clockwise] {
        auto& placement = placements_[static_cast<std::size_t>(shape_)];
        placement.quarter_turns = (placement.quarter_turns + (clockwise ? 1 : 3)) % 4;
        updatePreview();
      });
      rotate->addWidget(button);
    }
    rotate->addStretch();
    layout->addRow(qtext("export.wm.rotate"), rotate);

    auto* grid = new QGridLayout;
    grid->setSpacing(2);
    anchors_ = new QButtonGroup(group);
    const auto& anchor_names = ui::shell::page_list("export-wm-anchors");
    for (int cell = 0; cell < 9; ++cell) {
      auto* button = new QToolButton(group);
      button->setObjectName(QStringLiteral("export-wm-anchor-%1").arg(cell));
      button->setAccessibleName(qs(anchor_names[static_cast<std::size_t>(cell)].text));
      button->setToolTip(button->accessibleName());
      button->setCheckable(true);
      button->setFixedSize(20, 20);
      button->setProperty("anchor", true);
      anchors_->addButton(button, cell);
      grid->addWidget(button, cell / 3, cell % 3);
    }
    auto* grid_row = new QHBoxLayout;
    grid_row->addLayout(grid);
    grid_row->addStretch();
    layout->addRow(qtext("export.wm.anchor"), grid_row);
    connect(anchors_, &QButtonGroup::idClicked, this, [this](int cell) {
      // 实测：角上 3 / 3，左中右中 3 / 0，中间 0 / 0；上中下中 0 / 3 按对称推断。
      const bool middle_column = cell % 3 == 1;
      const bool middle_row = cell / 3 == 1;
      loading_ = true;
      x_.spin->setValue(middle_column ? 0 : 3);
      y_.spin->setValue(middle_row ? 0 : 3);
      loading_ = false;
      storePlacement();
    });

    wm_size_ = sliderRow(layout, group, "export.wm.size", "export-wm-size", 0, 100);
    fit_ = new QCheckBox(qtext("export.wm.fit"), group);
    fit_->setObjectName(QStringLiteral("export-wm-fit"));
    connect(fit_, &QCheckBox::toggled, this, [this] { storePlacement(); });
    layout->addRow(fit_);
    opacity_ = sliderRow(layout, group, "export.wm.opacity", "export-wm-opacity", 0, 100);
    x_ = sliderRow(layout, group, "export.wm.x", "export-wm-x", -100, 100);
    y_ = sliderRow(layout, group, "export.wm.y", "export-wm-y", -100, 100);
    plain_ = new QCheckBox(qtext("export.wm.plain"), group);
    plain_->setObjectName(QStringLiteral("export-wm-plain"));
    plain_->setToolTip(qtext("export.wm.plain.tip"));
    plain_->setAccessibleDescription(qtext("export.wm.plain.tip"));
    plain_->setChecked(target_.defaults.watermark_free_copy);  // 实测记住上次的勾选
    layout->addRow(plain_);

    connect(shapes_, &QButtonGroup::idClicked, this, [this](int shape) {
      shape_ = shape;
      loadPlacement();
    });
    connect(group, &QGroupBox::toggled, this, [this] { updatePreview(); });
    // 打开时停在当前照片的方向（按缩略图的比例）。
    const QImage thumb = target_.photo_ids.empty() ? QImage() : actions_->thumbnail(target_.photo_ids.front());
    photo_shape_ = thumb.isNull() ? 0
                                  : static_cast<int>(watermark_shape(static_cast<std::uint32_t>(thumb.width()),
                                                                     static_cast<std::uint32_t>(thumb.height())));
    shape_ = photo_shape_;
    shapes_->button(shape_)->setChecked(true);
    // 其余两个方向只能看，不能改（test 版：参数区置灰，水印同步仍可用）。
    for (auto* child : group->findChildren<QWidget*>()) {
      if (child->parentWidget() == nullptr || qobject_cast<QToolButton*>(child) != nullptr ||
          qobject_cast<QPushButton*>(child) != nullptr || child == plain_ ||
          child->objectName().startsWith(QStringLiteral("qt_"))) {
        continue;
      }
      edit_widgets_.push_back(child);
    }
    for (auto* button : group->findChildren<QToolButton*>()) {
      if (shapes_->id(button) < 0 && button->objectName() != QStringLiteral("export-wm-material")) {
        edit_widgets_.push_back(button);
      }
    }
    loadMaterial();
    loadPlacement();
    return group;
  }

  // 当前页签的参数写进控件（不回写）。
  void loadPlacement() {
    const auto& placement = placements_[static_cast<std::size_t>(shape_)];
    loading_ = true;
    wm_size_.spin->setValue(placement.size);
    fit_->setChecked(placement.fit);
    opacity_.spin->setValue(placement.opacity);
    x_.spin->setValue(placement.x);
    y_.spin->setValue(placement.y);
    anchors_->button(placement.anchor)->setChecked(true);
    loading_ = false;
    const bool editable = shape_ == photo_shape_;
    for (auto* widget : edit_widgets_) widget->setEnabled(editable);
    wm_size_.slider->setEnabled(editable && !placement.fit);  // 合适时大小置灰（实测）
    wm_size_.spin->setEnabled(editable && !placement.fit);
    updatePreview();
  }

  void storePlacement() {
    if (loading_ || anchors_ == nullptr) return;
    auto& placement = placements_[static_cast<std::size_t>(shape_)];
    placement.size = wm_size_.spin->value();
    placement.fit = fit_->isChecked();
    placement.opacity = opacity_.spin->value();
    placement.x = x_.spin->value();
    placement.y = y_.spin->value();
    placement.anchor = std::max(0, anchors_->checkedId());
    wm_size_.slider->setEnabled(!placement.fit);
    wm_size_.spin->setEnabled(!placement.fit);
    updatePreview();
  }

  void loadMaterial() {
    material_image_ = material_.empty() ? QImage() : QImage(qs(actions_->watermark_file(material_)));
    if (material_image_.isNull()) {
      material_button_->setIcon(QIcon());
      material_button_->setText(qtext("export.wm.upload") + QStringLiteral("\n") + qtext("export.wm.upload.hint"));
      material_button_->setToolTip(QString());
    } else {
      material_button_->setIcon(QIcon(QPixmap::fromImage(material_image_)));
      material_button_->setText(QString());
      material_button_->setToolTip(qtext("export.wm.replace"));
    }
    material_button_->setAccessibleDescription(material_button_->toolTip());
  }

  void chooseMaterial() {
    const QString path = QFileDialog::getOpenFileName(this, qtext("export.wm.choose"), QDir::homePath(),
                                                      QStringLiteral("JPG/PNG (*.png *.jpg *.jpeg)"));
    if (path.isEmpty()) return;
    std::string material;
    std::string reason;
    if (!actions_->store_watermark(path.toStdString(), material, reason)) {
      QMessageBox::warning(this, windowTitle(), qtext("export.wm.not_saved") + qs(reason));
      return;
    }
    material_ = material;
    loadMaterial();
    updatePreview();
  }

  // 左侧预览：缩略图上按当前照片方向那一套画出水印（与 exportd 同一套摆放规则）。
  void updatePreview() {
    if (preview_ == nullptr) return;
    const QImage thumb = target_.photo_ids.empty() ? QImage() : actions_->thumbnail(target_.photo_ids.front());
    if (thumb.isNull()) return;
    QImage frame = thumb.scaled(320, 320, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                       .convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (watermark_ != nullptr && static_cast<QGroupBox*>(watermark_)->isChecked() && !material_image_.isNull()) {
      const auto& placement = placements_[static_cast<std::size_t>(photo_shape_)];
      const WatermarkRect rect = place_watermark(static_cast<std::uint32_t>(material_image_.width()),
                                                 static_cast<std::uint32_t>(material_image_.height()), placement,
                                                 static_cast<std::uint32_t>(frame.width()),
                                                 static_cast<std::uint32_t>(frame.height()));
      if (rect.width > 0 && rect.height > 0) {
        QPainter painter(&frame);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.setOpacity(placement.opacity / 100.0);
        painter.translate(rect.left + rect.width / 2.0, rect.top + rect.height / 2.0);
        painter.rotate(90.0 * placement.quarter_turns);
        const bool swap = placement.quarter_turns % 2 == 1;
        const double w = swap ? rect.height : rect.width;
        const double h = swap ? rect.width : rect.height;
        painter.drawImage(QRectF(-w / 2.0, -h / 2.0, w, h), material_image_);
      }
    }
    if (layout_ != nullptr && tabs_ != nullptr && tabs_->currentIndex() == 2 &&
        static_cast<QGroupBox*>(layout_)->isChecked()) {
      frame = paperPreview(frame);
    }
    preview_->setPixmap(QPixmap::fromImage(frame));
  }

  // 排版印刷页的预览：把（已带水印的）缩略图按真实排版规则画到相纸上（kernel 的 layout_geometry，
  // 与 exportd 同一份），整张相纸缩到预览框里；边距窗口外是白的，相纸四周画一圈灰线。
  QImage paperPreview(const QImage& photo) const {
    // 成品的像素大小：原图文件头的大小；裁剪或旋转让缩略图的长宽比对不上时，按缩略图的长宽比、
    // 取原图长边估（官方也是按成品算，这里没有裁剪后的尺寸）。
    int pw = target_.product_width, ph = target_.product_height;
    if (pw <= 0 || ph <= 0) {
      pw = 3000;
      ph = std::max(1, static_cast<int>(std::lround(3000.0 * photo.height() / photo.width())));
    } else if (std::abs(static_cast<double>(pw) / ph - static_cast<double>(photo.width()) / photo.height()) > 0.02 *
                                                                                                    pw / ph) {
      const double longer = std::max(pw, ph);
      if (photo.width() >= photo.height()) {
        pw = static_cast<int>(longer);
        ph = std::max(1, static_cast<int>(std::lround(longer * photo.height() / photo.width())));
      } else {
        ph = static_cast<int>(longer);
        pw = std::max(1, static_cast<int>(std::lround(longer * photo.width() / photo.height())));
      }
    }
    const ExportLayout layout = currentLayout();
    LayoutGeometry geometry;
    if (!layout_geometry(layout, static_cast<std::uint32_t>(pw), static_cast<std::uint32_t>(ph), geometry)) return photo;
    const double scale = std::min(320.0 / geometry.page_width, 320.0 / geometry.page_height);
    QImage paper(std::max(1, static_cast<int>(std::lround(geometry.page_width * scale))),
                 std::max(1, static_cast<int>(std::lround(geometry.page_height * scale))),
                 QImage::Format_ARGB32_Premultiplied);
    paper.fill(Qt::white);
    QPainter painter(&paper);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.scale(scale, scale);
    // 裁切线（先画，照片盖在上面）；预览缩小后至少画 1 个屏幕像素宽。
    painter.setPen(QPen(Qt::black, 0));
    for (const std::int64_t y : geometry.cut_rows) {  // 照片那几列（连格缝）不画
      if (geometry.cut_skip_left > 0) painter.drawLine(QPointF(0, y + 0.5), QPointF(geometry.cut_skip_left, y + 0.5));
      if (geometry.cut_skip_right < geometry.page_width) {
        painter.drawLine(QPointF(geometry.cut_skip_right, y + 0.5), QPointF(geometry.page_width, y + 0.5));
      }
    }
    painter.setClipRect(QRectF(geometry.window_left, geometry.window_top, geometry.window_right - geometry.window_left,
                               geometry.window_bottom - geometry.window_top));
    for (const LayoutRect& at : geometry.photos) {
      painter.drawImage(QRectF(at.left, at.top, at.width, at.height), photo);
    }
    painter.setClipping(false);
    painter.resetTransform();
    painter.setPen(QColor(0x80, 0x80, 0x80));
    painter.drawRect(paper.rect().adjusted(0, 0, -1, -1));
    return paper;
  }

  // 记住水印开关和参数（导出时随默认值一起写；取消时单独写）。
  ExportDefaults withWatermark(ExportDefaults defaults) const {
    defaults.watermark_on = static_cast<QGroupBox*>(watermark_)->isChecked();
    defaults.watermark_free_copy = plain_->isChecked();
    if (!material_.empty()) {
      defaults.watermark = ExportWatermark{material_, placements_};
    }
    defaults.layout_on = static_cast<QGroupBox*>(layout_)->isChecked();
    defaults.layout = currentLayout();  // 开关关着也记参数
    defaults.layout_unit = layout_unit_->currentData().toString().toStdString();
    return defaults;
  }

  void done(int result) override {
    if (result == QDialog::Rejected) actions_->remember_export_defaults(withWatermark(target_.defaults));
    QDialog::done(result);
  }

  void setQuality(int value) {
    if (updating_) return;
    updating_ = true;
    quality_->setValue(value);
    slider_->setValue(value);
    level_->setCurrentIndex(levelOf(value));
    updating_ = false;
  }

  void showLimitChoice() {
    const bool by_size = by_size_->isChecked();
    size_->setEnabled(by_size);
    quality_->setEnabled(!by_size);
    level_->setEnabled(!by_size);
    slider_->setEnabled(!by_size);
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

  // 排版印刷页（实测规则见 docs/superpowers/evidence/2026-11-07-print-layout-observation.md）。
  QWidget* buildLayout() {
    auto* group = new QGroupBox(qtext("export.layout.group"), this);
    group->setObjectName(QStringLiteral("export-layout"));
    group->setCheckable(true);
    group->setChecked(target_.defaults.layout_on);  // 实测默认关；之后记住上次的
    auto* form = new QFormLayout(group);
    form->addRow(sectionLabel(qtext("export.layout.note")));
    auto* kinds = new QHBoxLayout;
    layout_id_ = new QRadioButton(qtext("export.layout.id"), group);
    layout_id_->setObjectName(QStringLiteral("export-layout-id"));
    layout_id_->setChecked(true);
    layout_plain_ = new QRadioButton(qtext("export.layout.plain"), group);
    layout_plain_->setObjectName(QStringLiteral("export-layout-plain"));
    // 同一个父控件下的单选钮默认互斥，类型和方向要各自成组。
    auto* kind_group = new QButtonGroup(group);
    kind_group->addButton(layout_id_);
    kind_group->addButton(layout_plain_);
    kinds->addWidget(layout_id_);
    kinds->addWidget(layout_plain_);
    kinds->addStretch();
    form->addRow(kinds);
    auto* turn = new QHBoxLayout;
    layout_landscape_ = new QRadioButton(qtext("export.layout.landscape"), group);
    layout_landscape_->setObjectName(QStringLiteral("export-layout-landscape"));
    layout_landscape_->setChecked(true);  // 实测默认横
    layout_portrait_ = new QRadioButton(qtext("export.layout.portrait"), group);
    auto* portrait = layout_portrait_;
    portrait->setObjectName(QStringLiteral("export-layout-portrait"));
    auto* turn_group = new QButtonGroup(group);
    turn_group->addButton(layout_landscape_);
    turn_group->addButton(portrait);
    turn->addWidget(layout_landscape_);
    turn->addWidget(portrait);
    turn->addStretch();
    form->addRow(qtext("export.layout.paper"), turn);
    layout_paper_ = new QComboBox(group);
    layout_paper_->setObjectName(QStringLiteral("export-layout-paper"));
    layout_paper_->setAccessibleName(qtext("export.layout.paper"));
    for (const auto& entry : ui::shell::page_list("export-papers")) layout_paper_->addItem(qs(entry.text), qs(entry.id));
    layout_paper_->setCurrentIndex(std::max(0, layout_paper_->findData(QStringLiteral("152x102"))));  // 实测默认 6寸
    form->addRow(layout_paper_);
    const auto spin = [&](const char* name, const char* label, int low, int high, int value) {
      auto* box = new QSpinBox(group);
      box->setObjectName(QString::fromLatin1(name));
      box->setAccessibleName(qtext(label));
      box->setRange(low, high);
      box->setValue(value);
      form->addRow(qtext(label), box);
      return box;
    };
    layout_dpi_ = spin("export-layout-dpi", "export.layout.dpi", 72, 1200, 300);
    layout_rows_ = spin("export-layout-rows", "export.layout.rows", 1, 20, 1);
    layout_cols_ = spin("export-layout-cols", "export.layout.cols", 1, 20, 1);
    layout_gap_ = spin("export-layout-gap", "export.layout.gap", 0, 200, 10);
    // 边距：标尺单位毫米（默认）、厘米、英寸；内部一律存整毫米。
    layout_unit_ = new QComboBox(group);
    layout_unit_->setObjectName(QStringLiteral("export-layout-unit"));
    layout_unit_->setAccessibleName(qtext("export.layout.unit"));
    for (const char* unit : {"mm", "cm", "in"}) {
      layout_unit_->addItem(qtext((std::string("export.layout.unit.") + unit).c_str()), QString::fromLatin1(unit));
    }
    const int remembered_unit = layout_unit_->findData(qs(target_.defaults.layout_unit));
    layout_unit_->setCurrentIndex(std::max(0, remembered_unit));
    form->addRow(qtext("export.layout.unit"), layout_unit_);
    const auto margin = [&](const char* name, const char* label) {
      auto* box = new QDoubleSpinBox(group);
      box->setObjectName(QString::fromLatin1(name));
      box->setAccessibleName(qtext(label));
      form->addRow(qtext(label), box);
      return box;
    };
    layout_top_ = margin("export-layout-margin-top", "export.layout.margin_top");
    layout_bottom_ = margin("export-layout-margin-bottom", "export.layout.margin_bottom");
    layout_left_ = margin("export-layout-margin-left", "export.layout.margin_left");
    layout_right_ = margin("export-layout-margin-right", "export.layout.margin_right");
    configureMarginUnit({0, 0, 0, 0});
    connect(layout_unit_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
      const ExportLayout before = marginsInMm();  // 单位换了，毫米值不变，只换显示
      configureMarginUnit({before.margin_top_mm, before.margin_bottom_mm, before.margin_left_mm, before.margin_right_mm});
      updatePreview();
    });
    // 行数、列数、间隔只在证件照排版里出现（需求 §19）。
    const auto grid_visible = [this, form] {
      const bool id = layout_id_->isChecked();
      for (QSpinBox* box : {layout_rows_, layout_cols_, layout_gap_}) {
        box->setVisible(id);
        if (auto* label = form->labelForField(box)) label->setVisible(id);
      }
    };
    if (target_.defaults.layout) applyLayout(*target_.defaults.layout);  // 实测记住上次的全部参数
    connect(layout_id_, &QRadioButton::toggled, this, grid_visible);
    grid_visible();
    // 预览跟着排版参数走。
    connect(group, &QGroupBox::toggled, this, [this] { updatePreview(); });
    for (QRadioButton* radio : {layout_id_, layout_plain_, layout_landscape_, layout_portrait_}) {
      connect(radio, &QRadioButton::toggled, this, [this] { updatePreview(); });
    }
    connect(layout_paper_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { updatePreview(); });
    for (QSpinBox* box : {layout_dpi_, layout_rows_, layout_cols_, layout_gap_}) {
      connect(box, qOverload<int>(&QSpinBox::valueChanged), this, [this] { updatePreview(); });
    }
    for (QDoubleSpinBox* box : {layout_top_, layout_bottom_, layout_left_, layout_right_}) {
      connect(box, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] { updatePreview(); });
    }
    return group;
  }

  // 把记住的排版参数放回控件（值已经过 parse 的范围检查；纸张不在列表里就留默认）。
  void applyLayout(const ExportLayout& layout) {
    layout_id_->setChecked(layout.id_photo);
    layout_plain_->setChecked(!layout.id_photo);
    const bool landscape = layout.paper_width_mm >= layout.paper_height_mm;
    layout_landscape_->setChecked(landscape);
    layout_portrait_->setChecked(!landscape);
    const int wide = std::max(layout.paper_width_mm, layout.paper_height_mm);
    const int tall = std::min(layout.paper_width_mm, layout.paper_height_mm);
    int paper = layout_paper_->findData(QStringLiteral("%1x%2").arg(wide).arg(tall));
    if (paper < 0) paper = layout_paper_->findData(QStringLiteral("%1x%2").arg(tall).arg(wide));  // 5寸的编号是窄边在前
    if (paper >= 0) layout_paper_->setCurrentIndex(paper);
    layout_dpi_->setValue(layout.dpi);
    layout_rows_->setValue(layout.rows);
    layout_cols_->setValue(layout.cols);
    layout_gap_->setValue(layout.gap_px);
    configureMarginUnit({layout.margin_top_mm, layout.margin_bottom_mm, layout.margin_left_mm, layout.margin_right_mm});
  }

  // 一个毫米等于多少当前单位的显示值；当前单位的小数位数和步长。
  double unitPerMm() const {
    const QString unit = layout_unit_->currentData().toString();
    return unit == QLatin1String("cm") ? 0.1 : unit == QLatin1String("in") ? 1.0 / 25.4 : 1.0;
  }

  // 按当前单位重设四个边距框的范围、小数位、步长，并放入这四个毫米值（不触发预览重画）。
  void configureMarginUnit(const std::array<int, 4>& mm) {
    const QString unit = layout_unit_->currentData().toString();
    const int decimals = unit == QLatin1String("cm") ? 1 : unit == QLatin1String("in") ? 2 : 0;
    const double step = unit == QLatin1String("cm") ? 0.1 : unit == QLatin1String("in") ? 0.01 : 1.0;
    const double factor = unitPerMm();
    shown_per_mm_ = factor;
    QDoubleSpinBox* boxes[4] = {layout_top_, layout_bottom_, layout_left_, layout_right_};
    for (int i = 0; i < 4; ++i) {
      const QSignalBlocker blocker(boxes[i]);
      boxes[i]->setDecimals(decimals);
      boxes[i]->setRange(0, 400 * factor);
      boxes[i]->setSingleStep(step);
      boxes[i]->setValue(mm[static_cast<std::size_t>(i)] * factor);
    }
  }

  // 四个边距框里的值换成整毫米。
  ExportLayout marginsInMm() const {
    ExportLayout layout;
    const double to_mm = 1.0 / shown_per_mm_;  // 框里现在显示的单位（不是下拉框刚选的）
    layout.margin_top_mm = static_cast<int>(std::lround(layout_top_->value() * to_mm));
    layout.margin_bottom_mm = static_cast<int>(std::lround(layout_bottom_->value() * to_mm));
    layout.margin_left_mm = static_cast<int>(std::lround(layout_left_->value() * to_mm));
    layout.margin_right_mm = static_cast<int>(std::lround(layout_right_->value() * to_mm));
    return layout;
  }

  ExportLayout currentLayout() const {
    ExportLayout layout;
    layout.id_photo = layout_id_->isChecked();
    const QStringList size = layout_paper_->currentData().toString().split(QLatin1Char('x'));
    // 列表里 5寸 写作 89×127（窄边在前），其余宽边在前；方向才决定相纸的宽高。
    const int wide = std::max(size.value(0).toInt(), size.value(1).toInt());
    const int tall = std::min(size.value(0).toInt(), size.value(1).toInt());
    layout.paper_width_mm = layout_landscape_->isChecked() ? wide : tall;
    layout.paper_height_mm = layout_landscape_->isChecked() ? tall : wide;
    layout.dpi = layout_dpi_->value();
    layout.rows = layout_rows_->value();
    layout.cols = layout_cols_->value();
    layout.gap_px = layout_gap_->value();
    const ExportLayout margins = marginsInMm();
    layout.margin_top_mm = margins.margin_top_mm;
    layout.margin_bottom_mm = margins.margin_bottom_mm;
    layout.margin_left_mm = margins.margin_left_mm;
    layout.margin_right_mm = margins.margin_right_mm;
    return layout;
  }

  void showTab() {
    const int tab = tabs_->currentIndex();
    const bool custom = tab == 1;
    const bool print = tab == 2;
    preview_->setVisible(custom || print);
    common_->setVisible(!print);
    advanced_->setVisible(custom);
    watermark_->setVisible(custom);  // 快速导出页没有水印（实测）
    layout_->setVisible(print);
    resize(custom || print ? QSize(902, 650) : QSize(480, 449));  // 实测页面的窗口大小
    updatePreview();
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
    if (quality_group_->isEnabled() && by_size_->isChecked()) {
      // Rounded down to whole KB: the product never exceeds what the user typed.
      request.settings.size_limit_kb =
          std::max<std::uint64_t>(1, static_cast<std::uint64_t>(std::floor(size_->value() * 1024.0)));
    } else if (quality_group_->isEnabled()) {
      request.settings.quality = quality_->value();
    }
    request.settings.convert_to_srgb = srgb_->isChecked();
    if (static_cast<QGroupBox*>(watermark_)->isChecked()) {
      if (material_.empty()) {
        QMessageBox::warning(this, windowTitle(), qtext("export.wm.need_material"));
        return;
      }
      request.settings.watermark = ExportWatermark{material_, placements_};
      request.watermark_free_copy = plain_->isChecked();
    }

    if (static_cast<QGroupBox*>(layout_)->isChecked()) request.settings.layout = currentLayout();

    ExportDefaults defaults = withWatermark(target_.defaults);
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
  QWidget* layout_ = nullptr;
  QWidget* common_ = nullptr;
  QRadioButton* layout_id_ = nullptr;
  QRadioButton* layout_plain_ = nullptr;
  QRadioButton* layout_landscape_ = nullptr;
  QRadioButton* layout_portrait_ = nullptr;
  QComboBox* layout_paper_ = nullptr;
  QSpinBox* layout_dpi_ = nullptr;
  QSpinBox* layout_rows_ = nullptr;
  QSpinBox* layout_cols_ = nullptr;
  QSpinBox* layout_gap_ = nullptr;
  QComboBox* layout_unit_ = nullptr;
  double shown_per_mm_ = 1.0;  // 边距框当前显示的单位：一毫米等于几个单位
  QDoubleSpinBox* layout_top_ = nullptr;
  QDoubleSpinBox* layout_bottom_ = nullptr;
  QDoubleSpinBox* layout_left_ = nullptr;
  QDoubleSpinBox* layout_right_ = nullptr;
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
  QRadioButton* by_size_ = nullptr;
  QDoubleSpinBox* size_ = nullptr;
  QComboBox* level_ = nullptr;
  QSlider* slider_ = nullptr;
  QCheckBox* srgb_ = nullptr;
  // 水印
  std::string material_;
  QImage material_image_;
  std::array<WatermarkPlacement, 3> placements_{};
  int shape_ = 0;        // 正在编辑的页签
  int photo_shape_ = 0;  // 当前照片的方向（预览用这一套）
  bool loading_ = false;
  QButtonGroup* shapes_ = nullptr;
  QButtonGroup* anchors_ = nullptr;
  QToolButton* material_button_ = nullptr;
  SliderRow wm_size_;
  QCheckBox* fit_ = nullptr;
  QCheckBox* plain_ = nullptr;
  QPushButton* sync_ = nullptr;
  std::vector<QWidget*> edit_widgets_;
  SliderRow opacity_;
  SliderRow x_;
  SliderRow y_;
};

class ExportList final : public QDialog {
 public:
  ExportList(QWidget* parent, PageActions* actions) : QDialog(parent), actions_(actions) {
    setObjectName(QStringLiteral("export-list-dialog"));
    setAccessibleName(qs(copyItem("global", "export-list").text));
    setWindowTitle(accessibleName());
    setAttribute(Qt::WA_DeleteOnClose);
    resize(900, 420);
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
    action_column_ = static_cast<int>(columns.size());
    table_->horizontalHeader()->setSectionResizeMode(action_column_ - 1, QHeaderView::Stretch);
    for (int column = 0; column < action_column_; ++column) {
      if (columns[static_cast<std::size_t>(column)].id == "charge") {
        table_->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
      }
    }
    // Cell widgets do not size their column: fixed room for the longest action, rows tall enough for a button.
    table_->horizontalHeader()->setSectionResizeMode(action_column_, QHeaderView::Fixed);
    table_->setColumnWidth(action_column_, 140);
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
      const QStringList cells = {qs(row.file_name), qs(row.status), qs(row.reason), chargeText(row.charge),
                                 qs(row.destination_path)};
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
      QWidget* current = table_->cellWidget(i, action_column_);
      if (current == nullptr ? !actionKind(row).isEmpty() : current->property("action-key").toString() != key) {
        QWidget* action = actionFor(row);
        if (action != nullptr) action->setProperty("action-key", key);
        table_->setCellWidget(i, action_column_, action);
      }
    }
  }

  static QString chargeText(ExportCharge charge) {
    switch (charge) {
      case ExportCharge::Pending: return qtext("export.charge.pending");
      case ExportCharge::Charged: return qtext("export.charge.charged");
      case ExportCharge::Repeat: return qtext("export.charge.repeat");
      case ExportCharge::Free: return qtext("export.charge.free");
      case ExportCharge::Original: return qtext("export.charge.original");
      case ExportCharge::FreeCopy: return qtext("export.charge.copy");
      case ExportCharge::None: break;
    }
    return {};
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
  int action_column_ = 0;
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

void bindProjectShortcuts(QWidget* page, PageActions* actions) {
  bindShortcut(page, "go-home", [actions] { actions->request_page(Page::Home); });
  bindShortcut(page, "go-library", [actions] { actions->request_page(Page::Library); });
  bindShortcut(page, "go-refine", [actions] { actions->request_page(Page::Refine); });
  bindShortcut(page, "export-settings", [page, actions] {
    if (actions->export_target()) openExportDialog(page, actions);
  });
  bindShortcut(page, "export-list", [page, actions] { openExportList(page, actions); });
}

QPushButton* toolbarAction(std::string_view list, const CopyItem& item, QWidget* parent, PageActions* actions) {
  const bool labeled = item.id == "export" || item.id == "membership";
  if (item.id == "export") {
    auto* button = itemAction(list, item, parent, actions->export_target().has_value());
    // 选了照片才可用；图库里的选择变了跟着变。
    actions->watch_selection(button, [button, actions] { button->setEnabled(actions->export_target().has_value()); });
    QObject::connect(button, &QPushButton::clicked, button, [button, actions] { openExportDialog(button->window(), actions); });
    return button;
  }
  if (item.id == "account") {
    // 个人中心：第一项显示本机账本的剩余张数，点开导出列表（计费一栏即导出明细）；
    // 其余项需要账号服务，禁用并说明。
    auto* button = itemAction(list, item, parent, actions->remaining_sheets().has_value());
    QObject::connect(button, &QPushButton::clicked, button, [button, actions] {
      const auto sheets = actions->remaining_sheets();
      if (!sheets) return;
      QMenu menu(button);
      menu.setObjectName(QStringLiteral("account-menu"));
      for (const auto& entry : ui::shell::page_list("account-actions")) {
        if (entry.id == "sheets") {
          QAction* action = menu.addAction(qtext("account.sheets").arg(*sheets));
          action->setObjectName(QStringLiteral("account-sheets"));
          action->setToolTip(qtext("account.sheets.note"));
          QObject::connect(action, &QAction::triggered, button,
                           [button, actions] { openExportList(button->window(), actions); });
        } else {
          QAction* action = menu.addAction(qs(entry.text));
          action->setEnabled(false);
          action->setToolTip(qtext("account.why"));
        }
      }
      menu.setToolTipsVisible(true);
      menu.exec(button->mapToGlobal(QPoint(0, button->height())));
    });
    if (!labeled) showNameOnHover(button);
    return button;
  }
  if (item.id == "export-list") {
    auto* button = itemAction(list, item, parent, actions->has_exports());
    QObject::connect(button, &QPushButton::clicked, button, [button, actions] { openExportList(button->window(), actions); });
    if (!labeled) showNameOnHover(button);
    return button;
  }
  auto* button = itemAction(list, item, parent);
  if (!labeled) showNameOnHover(button);
  return button;
}

}  // namespace pixcake::shell
