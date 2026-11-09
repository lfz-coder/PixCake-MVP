#include "export_dialog.hpp"
#include "pages.hpp"
#include "pixcake_theme.hpp"
#include "pixcake_widgets.hpp"
#include "preset_dialog.hpp"
#include "shell_icons.hpp"
#include "sync_dialog.hpp"

#include "pixcake/capability/capability_registry.hpp"
#include "pixcake/mask/brush_mask.hpp"
#include "pixcake/mask/semantic_parts.hpp"
#include "pixcake/ui/panels/module_panels.hpp"
#include "pixcake/ui/panels/slider_spec.hpp"

#include <QAbstractScrollArea>
#include <QCheckBox>
#include <QToolButton>
#include <QStyle>
#include <QStackedWidget>
#include <QGridLayout>
#include <QFileInfo>
#include <QButtonGroup>
#include <QComboBox>
#include <QListWidget>
#include <QTreeWidget>
#include <QSignalBlocker>
#include <QMenu>
#include <QKeySequence>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSlider>
#include <QStandardItemModel>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <array>
#include <set>
#include <string>
#include <cmath>
#include <vector>
#include <memory>
#include <functional>

namespace pixcake::shell {
namespace {

namespace panels = ui::panels;

QString nativeShortcut(std::string_view id) {
  return QKeySequence(qs(ui::shell::shortcut_key(id))).toString(QKeySequence::NativeText);
}

// 照片打开时可拖动的滑杆：renderd 的参考绘制画得出的参数（reference_drawn）。其余保持禁用，
// 不让滑杆动了画面却不变。是否可用还要看参数模式的禁用规则，见 EnableRefresh。
bool previewable(const PageActions* actions, const panels::SliderSpec& spec,
                 const std::optional<panels::SliderBinding>& binding) {
  return actions != nullptr && actions->has_photo() && spec.bound && binding && reference_drawn(*binding);
}

// 禁用规则依赖其他参数（配置文件、颗粒效果），提交后就地重算各控件的可用状态，不重建页面。
struct EnableRefresh {
  std::vector<std::function<void()>> updates;
  void run() const {
    for (const auto& update : updates) update();
  }
};
using Refresh = std::shared_ptr<EnableRefresh>;

// 刻度 → 参数模式的显示值（按步进推出的小数位；-0 写成 0）。
std::string display_of(const panels::SliderSpec& spec, int tick) {
  double value = spec.minimum + spec.step * tick;
  if (std::abs(value) < spec.step / 2) value = 0.0;
  return QString::number(value, 'f', spec.decimals).toStdString();
}

int tick_of(const panels::SliderSpec& spec, const std::string& display) {
  bool ok = false;
  const double value = QString::fromStdString(display).toDouble(&ok);
  if (!ok || spec.step <= 0) return spec.default_tick;
  return std::clamp(static_cast<int>(std::lround((value - spec.minimum) / spec.step)), 0, spec.tick_count);
}

// 面板能滚动：滚轮经过滑杆时只滚面板，不改值（点过、键盘聚焦的滑杆才吃滚轮）。
class WheelToScroll final : public QObject {
 public:
  using QObject::QObject;

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    auto* widget = qobject_cast<QWidget*>(watched);
    if (event->type() != QEvent::Wheel || widget == nullptr || widget->hasFocus()) return QObject::eventFilter(watched, event);
    for (QWidget* up = widget->parentWidget(); up != nullptr; up = up->parentWidget()) {
      if (auto* area = qobject_cast<QAbstractScrollArea*>(up)) {
        QCoreApplication::sendEvent(area->viewport(), event);
        return true;
      }
    }
    event->ignore();
    return true;
  }
};

void scrollPastOnWheel(QSlider* slider) {
  slider->setFocusPolicy(Qt::StrongFocus);
  slider->installEventFilter(new WheelToScroll(slider));
}

// 范围、步进、默认值只来自内核参数模式（ui/panels 的 slider_spec）。照片打开时，绑定全图色彩的
// 滑杆可用：拖动发预览，松手（或键盘改值）经命令入口提交。
// 名字后面的修饰（官方画面）：问号说明、New/Beta 角标。
void addNameDecor(QHBoxLayout* row, QWidget* parent, const panels::PanelRow* decor) {
  if (decor == nullptr) return;
  if (decor->help) {
    auto* help = new QLabel(QStringLiteral("?"), parent);
    help->setObjectName(QStringLiteral("refine-help"));
    help->setAlignment(Qt::AlignCenter);
    help->setFixedSize(16, 16);
    row->addWidget(help);
  }
  if (!decor->badge.empty()) {
    auto* badge = new QLabel(qs(decor->badge), parent);
    badge->setObjectName(QStringLiteral("refine-badge"));
    badge->setFixedHeight(16);
    badge->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    row->addWidget(badge, 0, Qt::AlignVCenter);
  }
}

// 名字一格：名字加修饰。
QWidget* decoratedName(QWidget* parent, const QString& label, const panels::PanelRow* decor, QLabel** name_out = nullptr,
                       bool enabled = false) {
  auto* box = new QWidget(parent);
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(6);
  auto* name = text(label);
  name->setWordWrap(false);  // 官方的名字一行显示（如“景物色彩（偏向天空）”）
  name->setEnabled(enabled);
  row->addWidget(name);
  addNameDecor(row, box, decor);
  row->addStretch(1);
  if (name_out != nullptr) *name_out = name;
  return box;
}

// 数值右边的 ▸：带子项的行点它展开子项框。没有子项（还没核对）时禁用。
QToolButton* expanderButton(QWidget* parent) {
  auto* button = new QToolButton(parent);
  button->setObjectName(QStringLiteral("refine-expander"));
  button->setCheckable(true);
  button->setIcon(PixCakeFoldSection::triangleIcon(false));
  button->setIconSize(QSize(8, 8));
  QObject::connect(button, &QToolButton::toggled, button,
                   [button](bool open) { button->setIcon(PixCakeFoldSection::triangleIcon(open)); });
  return button;
}

// 没有参数模式的滑杆：截图里滑块在中间的（双向）放在中间，档位滑杆按档数；都禁用，无障碍名写“范围待确认”。
void shapeUnboundSlider(QSlider* slider, const panels::PanelRow* decor) {
  if (decor == nullptr) return;
  if (decor->ticks > 0) {
    slider->setRange(0, decor->ticks - 1);
  } else if (decor->at_maximum) {
    slider->setRange(0, 1);
    slider->setValue(1);
  } else if (decor->centered) {
    slider->setRange(0, 2);
    slider->setValue(1);
  }
}

void addDisabledSlider(QWidget* parent, QLayout* layout, const panels::SliderSpec& spec, const QString& object_name,
                       PageActions* actions = nullptr, const std::optional<panels::SliderBinding>& binding = {},
                       const Refresh& refresh = nullptr, const panels::PanelRow* decor = nullptr,
                       QToolButton** expander_out = nullptr) {
  // 官方的滑杆行：上面左边是名字、右边是数值，下面是整行宽的滑杆（白平衡的色温、色调画成色带）。
  auto* row = new QWidget(parent);
  auto* row_layout = new QGridLayout(row);
  row_layout->setContentsMargins(0, 0, 0, 0);
  row_layout->setHorizontalSpacing(6);
  row_layout->setVerticalSpacing(4);
  QLabel* name = nullptr;
  auto* name_box = decoratedName(row, qs(spec.label), decor, &name);
  auto* slider = new QSlider(Qt::Horizontal, row);
  if (!object_name.isEmpty()) slider->setObjectName(object_name);
  if (binding && (binding->parameter_id == "temperature" || binding->parameter_id == "tint")) {
    slider->setProperty("track", qs(binding->parameter_id));
  } else if (decor != nullptr && !decor->track.empty()) {
    slider->setProperty("track", qs(decor->track));
  }
  slider->setRange(0, spec.tick_count);
  slider->setSingleStep(1);
  slider->setValue(spec.default_tick);
  if (!spec.bound) shapeUnboundSlider(slider, decor);
  slider->setEnabled(false);
  slider->setAccessibleName(qs(spec.accessible_name));
  scrollPastOnWheel(slider);
  auto* reading = text(qs(spec.default_text));
  reading->setEnabled(false);
  reading->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  row_layout->addWidget(name_box, 0, 0);
  row_layout->addWidget(reading, 0, 1);
  int columns = 2;
  if (decor != nullptr && decor->expandable) {
    auto* expander = expanderButton(row);
    row_layout->addWidget(expander, 0, 2);
    if (expander_out != nullptr) *expander_out = expander;
    columns = 3;
  }
  row_layout->addWidget(slider, 1, 0, 1, columns);
  layout->addWidget(row);
  if (!previewable(actions, spec, binding)) return;

  const panels::SliderBinding bound = *binding;
  if (const auto current = actions->parameter_value(bound)) {
    slider->setValue(tick_of(spec, *current));
    reading->setText(qs(*current));
  }
  const auto apply = [actions, bound, name = QPointer<QLabel>(name), slider = QPointer<QSlider>(slider),
                      reading = QPointer<QLabel>(reading)] {
    if (!slider) return;
    const auto blocked = parameter_blocked(actions, bound);
    for (QWidget* widget : {static_cast<QWidget*>(name), static_cast<QWidget*>(slider), static_cast<QWidget*>(reading)}) {
      if (widget != nullptr) widget->setEnabled(!blocked);
    }
    slider->setToolTip(blocked ? qs(*blocked) : QString());
  };
  apply();
  if (refresh) refresh->updates.push_back(apply);
  QObject::connect(slider, &QSlider::valueChanged, slider, [=](int tick) {
    const std::string value = display_of(spec, tick);
    reading->setText(qs(value));
    // Dragging previews; a keyboard or click change is final at once.
    const bool commit = !slider->isSliderDown();
    actions->request_parameter(bound, value, commit);
    if (commit && refresh) refresh->run();
  });
  QObject::connect(slider, &QSlider::sliderReleased, slider, [=] {
    actions->request_parameter(bound, display_of(spec, slider->value()), true);
    if (refresh) refresh->run();
  });
}

// 枚举参数（配置文件、可选颜色调整方式）用下拉框（Qt 组件）。参考绘制画不出的可选值不可选：配置文件的
// 质感、基础要配置文件资源，还没有接入。
void addChoice(QWidget* parent, QLayout* layout, const panels::PanelRow& row, PageActions* actions,
               const Refresh& refresh) {
  auto* line = new QWidget(parent);
  auto* line_layout = new QHBoxLayout(line);
  line_layout->setContentsMargins(0, 0, 0, 0);
  line_layout->setSpacing(6);
  auto* name = text(qs(row.label));
  auto* box = new QComboBox(line);
  box->setObjectName(QStringLiteral("refine-choice-") + qs(row.binding->parameter_id));
  box->setAccessibleName(qs(row.label));
  const auto options = panels::choice_options(*row.binding);
  const auto current = actions != nullptr ? actions->parameter_value(*row.binding) : std::nullopt;
  for (const auto& option : options) {
    box->addItem(qs(option.label), qs(option.id));
    if ((current && *current == option.id) || (!current && option.is_default)) box->setCurrentIndex(box->count() - 1);
    const bool drawn = row.binding->parameter_id != "profile" || option.id == "standard" || option.id == "monochrome";
    if (!drawn) {
      if (auto* model = qobject_cast<QStandardItemModel*>(box->model())) {
        model->item(box->count() - 1)->setEnabled(false);
        model->item(box->count() - 1)->setToolTip(qtext("refine.profile.pending"));
      }
    }
  }
  line_layout->addWidget(name);
  line_layout->addWidget(box, 1);
  layout->addWidget(line);
  const bool usable = actions != nullptr && actions->has_photo() && reference_drawn(*row.binding);
  name->setEnabled(usable);
  box->setEnabled(usable);
  if (!usable) return;
  const panels::SliderBinding bound = *row.binding;
  QObject::connect(box, QOverload<int>::of(&QComboBox::activated), box, [=](int index) {
    actions->request_parameter(bound, box->itemData(index).toString().toStdString(), true);
    if (refresh) refresh->run();
  });
}

QLabel* disabledLabel(const QString& value, const QString& accessible = QString()) {
  auto* name = text(value);
  if (!accessible.isEmpty()) name->setAccessibleName(accessible);
  name->setEnabled(false);
  return name;
}

QVBoxLayout* labelPanel(QWidget* modules, QVBoxLayout* modules_layout, const char* object_name, const QString& accessible,
                        QFrame** frame_out = nullptr) {
  auto* frame = new QFrame(modules);
  frame->setObjectName(QString::fromLatin1(object_name));
  frame->setAccessibleName(accessible);
  auto* layout = new QVBoxLayout(frame);
  layout->setContentsMargins(0, 0, 0, 0);
  modules_layout->addWidget(frame);
  if (frame_out != nullptr) *frame_out = frame;
  return layout;
}

QString moduleDescription(const char* id) {
  if (std::string_view(id) == "color") return QString::fromUtf8(panels::refine_color_panel_name());
  return qs(copyItem("refine-modules", id).description);
}

// 精修右侧界面状态（选中的模块、展开的分组、预设栏开关）。页面每次重画都会重建，状态留在这里，重建后照旧。
struct RefineUiState {
  std::string module = "color";
  std::set<std::string> open_sections;  // “<页>/<分组名>”
  std::string local_tab = "subject";    // 局部调色的蒙版页签
  bool presets = true;
  bool mask_list = true;  // 局部调色的蒙版列表浮窗开着（官方进局部调色时是开的）
};
RefineUiState& uiState() {
  static RefineUiState state;
  return state;
}

// 当前照片的格式（扩展名大写，直方图卡片右下角的小标签）；没有照片时为空。
QString currentFormat(const PageActions* actions) {
  if (actions == nullptr || !actions->has_photo()) return {};
  const auto current = actions->current_photo();
  if (!current) return {};
  for (const auto& photo : actions->photos()) {
    if (photo.id == *current) return QFileInfo(qs(photo.file_name)).suffix().toUpper();
  }
  return {};
}

// 直方图卡片：上面是直方图，下面一行平时是相机参数（这一版还读不到 EXIF，和官方打开非 RAW 图时一样显示“ISO -”
// “- mm”“F/-”“-秒”）加格式标签；鼠标在画面上时这一行换成指针下像素的 R、G、B。
QFrame* histogramCard(QWidget* parent, PageActions* actions, PixCakeImageView* canvas, PixCakeHistogram** histogram_out) {
  auto* card = new QFrame(parent);
  card->setObjectName(QStringLiteral("refine-histogram-card"));
  auto* column = new QVBoxLayout(card);
  column->setContentsMargins(0, 0, 0, 0);
  column->setSpacing(0);
  auto* histogram = new PixCakeHistogram(card);
  histogram->setFixedHeight(104);
  *histogram_out = histogram;
  column->addWidget(histogram);

  const QString none = qtext("refine.exif.none");
  auto* camera = new QWidget(card);
  camera->setObjectName(QStringLiteral("refine-camera-readout"));
  auto* camera_row = new QHBoxLayout(camera);
  camera_row->setContentsMargins(12, 8, 12, 8);
  for (const QString& value : {qtext("refine.exif.iso").arg(none), qtext("refine.exif.focal").arg(none),
                               qtext("refine.exif.aperture").arg(none), qtext("refine.exif.shutter").arg(none)}) {
    auto* label = text(value);
    label->setObjectName(QStringLiteral("refine-readout"));
    label->setAccessibleName(value);
    camera_row->addWidget(label);
    camera_row->addStretch(1);
  }
  auto* badge = text(currentFormat(actions));
  badge->setObjectName(QStringLiteral("refine-format-badge"));
  badge->setVisible(!badge->text().isEmpty());
  camera_row->addWidget(badge);
  column->addWidget(camera);

  auto* pixel = new QWidget(card);
  pixel->setObjectName(QStringLiteral("refine-pixel-readout"));
  pixel->setAccessibleName(qtext("refine.readout.name"));
  auto* pixel_row = new QHBoxLayout(pixel);
  pixel_row->setContentsMargins(12, 8, 12, 8);
  std::array<QPointer<QLabel>, 3> values;
  int index = 0;
  for (const char* channel : {"R", "G", "B"}) {
    auto* key = text(QString::fromLatin1(channel));
    key->setObjectName(QStringLiteral("refine-readout-key"));
    auto* value = text(none);
    value->setObjectName(QStringLiteral("refine-readout"));
    value->setMinimumWidth(28);
    values[static_cast<std::size_t>(index++)] = value;
    pixel_row->addWidget(key);
    pixel_row->addWidget(value);
    pixel_row->addStretch(1);
  }
  pixel->setVisible(false);
  column->addWidget(pixel);
  if (canvas != nullptr) {
    canvas->onHoverPixel([camera = QPointer<QWidget>(camera), pixel = QPointer<QWidget>(pixel), values](std::optional<QColor> color) {
      if (!camera || !pixel) return;
      if (color) {
        const int channels[3] = {color->red(), color->green(), color->blue()};
        for (std::size_t i = 0; i < 3; ++i) {
          if (values[i]) values[i]->setText(QString::number(channels[i]));
        }
      }
      camera->setVisible(!color);
      pixel->setVisible(color.has_value());
    });
  }
  return card;
}

// 分组标题栏右侧的小按钮（AI校准白平衡、AI校准曝光）。图标文件还没有，按资源约定画占位；功能没接，禁用。
QWidget* sectionAction(QWidget* parent, const QString& label) {
  auto* icon = pendingIcon(parent, 18, label);
  icon->setToolTip(label);
  icon->setEnabled(false);
  return icon;
}

// 白平衡分组里的“白平衡 [自定义 ▾] [吸管]”一行：预设和吸管还没接，禁用。
QWidget* whiteBalancePicker(QWidget* parent, const QString& picker_label) {
  auto* line = new QWidget(parent);
  auto* row = new QHBoxLayout(line);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(8);
  auto* name = text(qtext("refine.wb.preset"));
  name->setEnabled(false);
  auto* box = new QComboBox(line);
  box->setObjectName(QStringLiteral("refine-wb-preset"));
  box->setAccessibleName(qtext("refine.wb.preset"));
  box->addItem(qtext("refine.wb.custom"));
  box->setEnabled(false);
  row->addWidget(name);
  row->addStretch(1);
  row->addWidget(box, 2);
  auto* picker = pendingIcon(line, 20, picker_label);
  picker->setToolTip(picker_label);
  picker->setEnabled(false);
  row->addWidget(picker);
  return line;
}

// 色彩类页面（色彩调节、局部调色）的顶部：直方图卡片和“AI 校准”“多图色彩统一”两个按钮，下面一条分隔线。
// 放在模块页的滚动区外面，固定不动。
void addColorPageTop(QWidget* parent, QVBoxLayout* layout, PageActions* actions, PixCakeImageView* canvas,
                     std::vector<PixCakeHistogram*>* histograms) {
  auto* top = new QWidget(parent);
  top->setObjectName(QStringLiteral("refine-color-top"));
  auto* top_layout = new QVBoxLayout(top);
  top_layout->setContentsMargins(12, 12, 12, 12);
  top_layout->setSpacing(12);
  PixCakeHistogram* histogram = nullptr;
  top_layout->addWidget(histogramCard(top, actions, canvas, &histogram));
  if (histograms != nullptr) histograms->push_back(histogram);
  auto* pills = new QHBoxLayout;
  pills->setSpacing(8);
  for (const char* key : {"refine.ai-calibrate", "refine.color-unify"}) {
    auto* pill = action(qtext(key), top, false);
    pill->setObjectName(QStringLiteral("refine-pill"));
    pills->addWidget(pill);
  }
  pills->addStretch(1);
  top_layout->addLayout(pills);
  layout->addWidget(top);
  auto* rule = new QFrame(parent);
  rule->setObjectName(QStringLiteral("refine-section-rule"));
  rule->setFixedHeight(1);
  layout->addWidget(rule);
}

// 下面是分组里各种行的画法（官方 9.3.0 的控件）。还没有效果的行都禁用。

QWidget* toggleRow(QWidget* parent, const panels::PanelRow& row) {
  auto* line = new QWidget(parent);
  auto* layout = new QHBoxLayout(line);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(decoratedName(line, qs(row.label), &row), 1);
  auto* toggle = new QCheckBox(line);
  toggle->setObjectName(QStringLiteral("refine-switch"));
  toggle->setAccessibleName(qs(row.label));
  toggle->setEnabled(false);
  layout->addWidget(toggle);
  return line;
}

// 左右联动滑杆：名字一行，下面“左 数值 | 右 数值”，再下面左滑杆、联动按钮、右滑杆。
QWidget* pairSliderRow(QWidget* parent, const panels::PanelRow& row, QToolButton** expander_out) {
  auto* box = new QWidget(parent);
  auto* grid = new QGridLayout(box);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(8);
  grid->setVerticalSpacing(4);
  auto* title = new QWidget(box);
  auto* title_row = new QHBoxLayout(title);
  title_row->setContentsMargins(0, 0, 0, 0);
  title_row->addWidget(decoratedName(title, qs(row.label), &row), 1);
  if (row.expandable) {
    auto* expander = expanderButton(title);
    title_row->addWidget(expander);
    if (expander_out != nullptr) *expander_out = expander;
  }
  grid->addWidget(title, 0, 0, 1, 3);
  int column = 0;
  for (const char* side : {"refine.side.left", "refine.side.right"}) {
    auto* head = new QWidget(box);
    auto* head_row = new QHBoxLayout(head);
    head_row->setContentsMargins(0, 0, 0, 0);
    const auto spec = panels::slider_spec(row.label + "-" + qtext(side).toStdString(), std::nullopt);
    auto* side_name = text(qtext(side));
    side_name->setEnabled(false);
    auto* reading = text(qs(spec.default_text));
    reading->setEnabled(false);
    head_row->addWidget(side_name);
    head_row->addStretch(1);
    head_row->addWidget(reading);
    grid->addWidget(head, 1, column);
    auto* slider = new QSlider(Qt::Horizontal, box);
    slider->setRange(0, 0);
    shapeUnboundSlider(slider, &row);
    slider->setEnabled(false);
    slider->setAccessibleName(qs(spec.accessible_name));
    scrollPastOnWheel(slider);
    grid->addWidget(slider, 2, column);
    column += 2;
  }
  auto* link = pendingIcon(box, 24, qtext("refine.side.link"));
  link->setToolTip(qtext("refine.side.link"));
  link->setEnabled(false);
  grid->addWidget(link, 2, 1);
  grid->setColumnStretch(0, 1);
  grid->setColumnStretch(2, 1);
  return box;
}

// 卡片单选（脸型预设、发色选择……）：四列，第一张“无”默认选中。卡片图片没有素材，只画名字。
QWidget* cardsRow(QWidget* parent, const panels::PanelRow& row) {
  auto* box = new QWidget(parent);
  box->setAccessibleName(qs(row.label));
  auto* grid = new QGridLayout(box);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setSpacing(8);
  auto* group = new QButtonGroup(box);
  int index = 0;
  for (const auto& option : row.options) {
    auto* card = new QToolButton(box);
    card->setObjectName(QStringLiteral("refine-card"));
    card->setText(qs(option));
    card->setAccessibleName(qs(option));
    card->setCheckable(true);
    card->setToolButtonStyle(Qt::ToolButtonTextOnly);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    card->setFixedHeight(72);
    card->setChecked(index == 0 && option == qtext("refine.card.none").toStdString());
    card->setEnabled(false);
    group->addButton(card);
    grid->addWidget(card, index / 4, index % 4);
    ++index;
  }
  for (int column = 0; column < 4; ++column) grid->setColumnStretch(column, 1);
  return box;
}

// 色块单选（肤色选择）：颜色来自表；空串是“无”。
QWidget* swatchesRow(QWidget* parent, const panels::PanelRow& row) {
  auto* box = new QWidget(parent);
  box->setAccessibleName(qs(row.label));
  auto* grid = new QGridLayout(box);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setSpacing(6);
  int index = 0;
  for (const auto& color : row.options) {
    auto* swatch = new QToolButton(box);
    swatch->setObjectName(QStringLiteral("refine-swatch"));
    swatch->setCheckable(true);
    swatch->setFixedSize(28, 28);
    swatch->setEnabled(false);
    if (color.empty()) {
      swatch->setChecked(true);
      swatch->setAccessibleName(qtext("refine.card.none"));
    } else {
      swatch->setAccessibleName(qs(color));
      swatch->setProperty("swatch", qs(color));
      swatch->setStyleSheet(QStringLiteral("QToolButton#refine-swatch { background: %1; }").arg(qs(color)));
    }
    grid->addWidget(swatch, index / 8, index % 8);
    ++index;
  }
  return box;
}

QWidget* tabsRow(QWidget* parent, const panels::PanelRow& row) {
  auto* box = new QWidget(parent);
  box->setAccessibleName(qs(row.label));
  auto* column = new QVBoxLayout(box);
  column->setContentsMargins(0, 0, 0, 0);
  column->setSpacing(8);
  QHBoxLayout* line = nullptr;
  // 一行放不下就换行：按按钮实际宽度算，可用宽度是分组内容区（面板宽减两边留白）。
  const int available = PixCakeTheme::kWorkspaceSideWidth + 14 - 24;
  int used = 0;
  int index = 0;
  for (const auto& option : row.options) {
    auto* tab = new QPushButton(qs(option), box);
    tab->setObjectName(QStringLiteral("refine-tab"));
    tab->setCheckable(true);
    tab->setChecked(index == 0);
    tab->setEnabled(false);  // 只看到第一页的内容；第一页也只是显示
    tab->ensurePolished();
    const int width = tab->sizeHint().width();
    if (line == nullptr || used + width > available) {
      if (line != nullptr) line->addStretch(1);
      line = new QHBoxLayout;
      line->setSpacing(8);
      column->addLayout(line);
      used = 0;
    }
    line->addWidget(tab);
    used += width + 8;
    ++index;
  }
  if (line != nullptr) line->addStretch(1);
  return box;
}

QWidget* aiButtonRow(QWidget* parent, const panels::PanelRow& row) {
  auto* button = action(qs(row.label), parent, false);
  button->setObjectName(QStringLiteral("refine-ai-button"));
  return button;
}

QWidget* toolRow(QWidget* parent, const panels::PanelRow& row) {
  auto* line = new QWidget(parent);
  auto* layout = new QHBoxLayout(line);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(decoratedName(line, qs(row.label), &row), 1);
  const std::vector<std::string> icons = row.options.empty() ? std::vector<std::string>{std::string()} : row.options;
  for (const auto& tip : icons) {
    const QString name = tip.empty() ? qs(row.label) : qs(tip);
    auto* icon = pendingIcon(line, 20, name);
    icon->setToolTip(name);
    icon->setEnabled(false);
    layout->addWidget(icon);
  }
  return line;
}

// 名字在上、下拉框在下；可选值只知道截图里显示的那个。
QWidget* dropdownRow(QWidget* parent, const panels::PanelRow& row) {
  auto* box = new QWidget(parent);
  auto* column = new QVBoxLayout(box);
  column->setContentsMargins(0, 0, 0, 0);
  column->setSpacing(6);
  column->addWidget(decoratedName(box, qs(row.label), &row));
  auto* combo = new QComboBox(box);
  combo->setAccessibleName(qs(row.label));
  for (const auto& option : row.options) combo->addItem(qs(option));
  combo->setEnabled(false);
  column->addWidget(combo);
  return box;
}

// 占满一行的两段切换（推荐 / 我的）。
QWidget* segmentsRow(QWidget* parent, const panels::PanelRow& row) {
  auto* box = new QWidget(parent);
  box->setAccessibleName(qs(row.label));
  auto* line = new QHBoxLayout(box);
  line->setContentsMargins(0, 0, 0, 0);
  line->setSpacing(2);
  int index = 0;
  for (const auto& option : row.options) {
    auto* segment = new QPushButton(qs(option), box);
    segment->setObjectName(QStringLiteral("refine-segment"));
    segment->setCheckable(true);
    segment->setChecked(index++ == 0);
    segment->setEnabled(false);
    line->addWidget(segment, 1);
  }
  return box;
}

QWidget* noticeRow(QWidget* parent, const panels::PanelRow& row) {
  auto* notice = new QLabel(qs(row.label), parent);
  notice->setObjectName(QStringLiteral("refine-notice"));
  notice->setAlignment(Qt::AlignCenter);
  notice->setMinimumHeight(120);
  notice->setEnabled(false);
  return notice;
}

QWidget* iconRow(QWidget* parent, const panels::PanelRow& row) {
  auto* box = new QWidget(parent);
  auto* line = new QHBoxLayout(box);
  line->setContentsMargins(0, 0, 0, 0);
  for (std::size_t i = 0; i < row.options.size(); ++i) {
    if (i > 0) line->addStretch(1);
    auto* icon = pendingIcon(box, 22, qs(row.options[i]));
    icon->setToolTip(qs(row.options[i]));
    icon->setEnabled(false);
    line->addWidget(icon);
  }
  return box;
}

// 子项框（可展开行的子项、“瘦腿”这种带标题的框）。
QFrame* childBox(QWidget* parent, QVBoxLayout** layout_out) {
  auto* box = new QFrame(parent);
  box->setObjectName(QStringLiteral("refine-child-box"));
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(10, 8, 10, 8);
  layout->setSpacing(10);
  *layout_out = layout;
  return box;
}

// 一列可折叠分组（色彩调节、局部调色、后期调色、人像美化共用）。行来自 ui/panels 的表；page 区分各页的展开状态
// （“<page>/<分组名>”）。rebind 把行里的绑定换成实际要写的参数（局部调色是带蒙版前缀的参数）；actions 为空时
// 控件全部禁用（例如局部调色还没有选中蒙版）。
using Rebind = std::function<panels::SliderBinding(const panels::SliderBinding&)>;
void addSections(QWidget* parent, QVBoxLayout* layout, const std::vector<panels::PanelRow>& rows, PageActions* actions,
                 const std::string& page, const Rebind& rebind = {}) {
  const Refresh refresh = std::make_shared<EnableRefresh>();
  PixCakeFoldSection* section = nullptr;
  QVBoxLayout* children = nullptr;  // 当前子项框
  for (std::size_t index = 0; index < rows.size(); ++index) {
    const auto& row = rows[index];
    if (row.kind == panels::PanelRowKind::Section) {
      const std::string key = page + "/" + row.label;
      section = new PixCakeFoldSection(qs(row.label), QStringLiteral("refine-section"), parent);
      section->setProperty("page", qs(page));
      section->toggle()->setAccessibleDescription(qtext("refine.section.toggle").arg(qs(row.label)));
      section->setExpanded(uiState().open_sections.count(key) != 0);
      section->onToggled([key](bool open) {
        if (open) uiState().open_sections.insert(key); else uiState().open_sections.erase(key);
      });
      addNameDecor(section->titleDecor(), section, &row);
      if (row.reset) section->actions()->addWidget(sectionAction(section, qtext("refine.section.reset")));
      layout->addWidget(section);
      children = nullptr;
      continue;
    }
    if (section == nullptr) continue;  // 表里每一行都在某个分组下（test_slider_spec 检查）
    QWidget* body = section->bodyWidget();
    QVBoxLayout* target = row.child && children != nullptr ? children : section->body();
    if (!row.child) children = nullptr;
    const bool has_children = index + 1 < rows.size() && rows[index + 1].child;
    QToolButton* expander = nullptr;
    std::optional<panels::SliderBinding> binding = row.binding;
    if (binding && rebind) binding = rebind(*binding);
    switch (row.kind) {
      case panels::PanelRowKind::HeaderAction:
        section->actions()->addWidget(sectionAction(section, qs(row.label)));
        break;
      case panels::PanelRowKind::Picker:
        target->addWidget(whiteBalancePicker(body, qs(row.label)));
        break;
      case panels::PanelRowKind::Title:
        target->addWidget(disabledLabel(qs(row.label)));
        break;
      case panels::PanelRowKind::Subheading: {
        QLabel* name = nullptr;
        auto* heading = decoratedName(body, qs(row.label), &row, &name, true);
        name->setObjectName(QStringLiteral("refine-subheading"));
        target->addWidget(heading);
        break;
      }
      case panels::PanelRowKind::Separator: {
        auto* line = new QFrame(body);
        line->setObjectName(QStringLiteral("refine-section-rule"));
        line->setFixedHeight(1);
        target->addWidget(line);
        break;
      }
      case panels::PanelRowKind::Choice: {
        panels::PanelRow bound = row;
        bound.binding = binding;
        addChoice(body, target, bound, actions, refresh);
        break;
      }
      case panels::PanelRowKind::Toggle:
        target->addWidget(toggleRow(body, row));
        break;
      case panels::PanelRowKind::Checkbox: {
        auto* check = new QCheckBox(qs(row.label), body);
        check->setEnabled(false);
        target->addWidget(check, 0, Qt::AlignRight);
        break;
      }
      case panels::PanelRowKind::PairSlider:
        target->addWidget(pairSliderRow(body, row, &expander));
        break;
      case panels::PanelRowKind::Cards:
        target->addWidget(cardsRow(body, row));
        break;
      case panels::PanelRowKind::Swatches:
        target->addWidget(swatchesRow(body, row));
        break;
      case panels::PanelRowKind::Tabs:
        target->addWidget(tabsRow(body, row));
        break;
      case panels::PanelRowKind::AiButton:
        target->addWidget(aiButtonRow(body, row));
        break;
      case panels::PanelRowKind::Tool:
        target->addWidget(toolRow(body, row));
        break;
      case panels::PanelRowKind::Dropdown:
        target->addWidget(dropdownRow(body, row));
        break;
      case panels::PanelRowKind::Segments:
        target->addWidget(segmentsRow(body, row));
        break;
      case panels::PanelRowKind::Notice:
        target->addWidget(noticeRow(body, row));
        break;
      case panels::PanelRowKind::IconRow:
        target->addWidget(iconRow(body, row));
        break;
      case panels::PanelRowKind::Group: {
        QVBoxLayout* group_layout = nullptr;
        auto* box = childBox(body, &group_layout);
        auto* head = new QWidget(box);
        auto* head_row = new QHBoxLayout(head);
        head_row->setContentsMargins(0, 0, 0, 0);
        head_row->addWidget(decoratedName(head, qs(row.label), &row), 1);
        auto* link = pendingIcon(head, 20, qtext("refine.side.link"));
        link->setEnabled(false);
        head_row->addWidget(link);
        group_layout->addWidget(head);
        target->addWidget(box);
        children = group_layout;
        break;
      }
      case panels::PanelRowKind::Section:
        break;
      case panels::PanelRowKind::Slider:
        addDisabledSlider(body, target, panels::slider_spec(row.label, binding), qs(row.object_name), actions, binding,
                          refresh, &row, &expander);
        break;
    }
    if (expander != nullptr) {
      // 带子项的行：子项放进一个默认收起的框，点 ▸ 展开。子项没核对过的行，▸ 禁用。
      expander->setEnabled(has_children);
      if (has_children) {
        QVBoxLayout* child_layout = nullptr;
        auto* box = childBox(body, &child_layout);
        box->setVisible(false);
        target->addWidget(box);
        QObject::connect(expander, &QToolButton::toggled, box, [box](bool open) { box->setVisible(open); });
        children = child_layout;
      }
    }
  }
}

// 色彩调节页（右侧图标栏“调色”）的滚动内容：色彩面板的全部分组（顶部固定，见 modulePage）。
void addColorPanel(QWidget* parent, QVBoxLayout* parent_layout, PageActions* actions) {
  QFrame* frame = nullptr;
  auto* layout = labelPanel(parent, parent_layout, "refine-color-labels", moduleDescription("color"), &frame);
  layout->setSpacing(0);
  addSections(frame, layout, panels::refine_color_rows(), actions, "color");
}


// 裁剪旋转（需求 §12.6）：固定比例（居中裁）、原始尺寸、逆时针、顺时针、水平翻转、垂直翻转、角度滑杆是真的，
// 参数写进 crop 能力，预览、缩略图和导出都按它画；其余条目（原照设置、证件照尺寸、AI 智能裁剪、网格等）还是灰的标签。
void addCropPanel(QWidget* modules, QVBoxLayout* layout, PageActions* actions) {
  const bool live = actions != nullptr && actions->has_photo();
  const panels::SliderBinding rotate{"crop", "rotate"};
  const panels::SliderBinding flip_h{"crop", "flip_horizontal"};
  const panels::SliderBinding flip_v{"crop", "flip_vertical"};
  const panels::SliderBinding ratio{"crop", "ratio"};
  const Refresh refresh = std::make_shared<EnableRefresh>();
  const auto button_row = [&](const QString& label, const char* object_name) {
    auto* button = action(label, modules, live);
    button->setObjectName(QString::fromLatin1(object_name));
    layout->addWidget(button);
    return button;
  };
  for (const auto& entry : ui::shell::page_list("refine-crop")) {
    const QString label = qs(entry.text);
    if (entry.id == "angle") {
      addDisabledSlider(modules, layout, panels::slider_spec(entry.text, panels::refine_crop_angle_binding()),
                        QStringLiteral("refine-crop-angle"), actions, panels::refine_crop_angle_binding(), refresh);
    } else if (entry.id == "crop-2") {  // 原始尺寸
      auto* button = button_row(label, "refine-crop-original");
      if (live) QObject::connect(button, &QPushButton::clicked, button, [actions, ratio] {
        actions->request_parameter(ratio, "original", true);
      });
    } else if (entry.id == "crop-4") {  // 固定比例
      auto* line = new QWidget(modules);
      auto* line_layout = new QHBoxLayout(line);
      line_layout->setContentsMargins(0, 0, 0, 0);
      auto* name = text(label);
      auto* box = new QComboBox(line);
      box->setObjectName(QStringLiteral("refine-crop-ratio"));
      box->setAccessibleName(label);
      const auto current = live ? actions->parameter_value(ratio) : std::nullopt;
      for (const auto& option : panels::choice_options(ratio)) {
        if (option.id == "original") continue;  // “原始尺寸”是上面的按钮
        box->addItem(qs(option.label), qs(option.id));
      }
      box->insertItem(0, QString(), QString());  // 空白：没有选固定比例
      box->setCurrentIndex(0);
      if (current) {
        const int found = box->findData(qs(*current));
        if (found >= 0) box->setCurrentIndex(found);
      }
      box->setEnabled(live);
      name->setEnabled(live);
      line_layout->addWidget(name);
      line_layout->addWidget(box, 1);
      layout->addWidget(line);
      if (live) QObject::connect(box, QOverload<int>::of(&QComboBox::activated), box, [actions, ratio, box](int index) {
        const QString id = box->itemData(index).toString();
        actions->request_parameter(ratio, id.isEmpty() ? std::string("original") : id.toStdString(), true);
      });
    } else if (entry.id == "crop-14" || entry.id == "crop-15") {  // 逆时针、顺时针
      const int step = entry.id == "crop-14" ? 3 : 1;             // 顺时针转几个 90°，逆时针 = 加 3
      auto* button = button_row(label, step == 3 ? "refine-crop-counterclockwise" : "refine-crop-clockwise");
      if (live) QObject::connect(button, &QPushButton::clicked, button, [actions, rotate, step] {
        const auto current = actions->parameter_value(rotate);
        const int turns = current ? std::atoi(current->c_str()) : 0;
        actions->request_parameter(rotate, std::to_string((turns + step) % 4), true);
      });
    } else if (entry.id == "crop-16" || entry.id == "crop-17") {  // 水平翻转、垂直翻转
      const bool horizontal = entry.id == "crop-16";
      const panels::SliderBinding flip = horizontal ? flip_h : flip_v;
      auto* button = button_row(label, horizontal ? "refine-crop-flip-horizontal" : "refine-crop-flip-vertical");
      button->setCheckable(true);
      if (live) {
        button->setChecked(actions->parameter_value(flip) == std::optional<std::string>("on"));
        QObject::connect(button, &QPushButton::clicked, button, [actions, flip](bool on) {
          actions->request_parameter(flip, on ? "on" : "off", true);
        });
      }
    } else {
      layout->addWidget(disabledLabel(label));
    }
  }
}

// 局部调色（官方的“蒙版列表”）：每个蒙版有自己的子项（语义部位或渐变，添加或减去）和一组调整滑杆。蒙版是 color.local
// 里带 "m<N>:" 前缀的参数，子项再带 "s<K>:"；新建只是写第一个参数，删除走命令入口。结构变了（新建、删除、
// 加减子项、撤销重做）就按当前文档重建这一块，滑杆提交不重建。
struct LocalState {
  int mask = -1;  // 选中的蒙版（实例编号），-1 没有
  int item = -1;  // 选中的子项，-1 没有
  bool draw = false;     // “在画布上拖动”打开：画布接管鼠标
  int drag_item = -1;    // 正在拖动的子项
  bool drag_radial = false;
  QPointF drag_start;    // 拖动起点（图像百分比）
  bool drag_sample = false;  // 这次拖动是取样（颜色范围、亮度范围），不是渐变
  std::string drag_region;   // 取样的是 color_range 还是 luminance_range
  double sample_min = 100.0, sample_max = 0.0;  // 亮度范围：拖动经过的亮度（0..100）的最小、最大
  bool drag_new = false;  // 这次拖动要新建子项（区域和叠加方式到第一次有效移动时才写预览）
  std::string drag_mode;  // 新建子项的叠加方式
  std::vector<std::pair<panels::SliderBinding, std::string>> drag_values;  // 松开时一次提交的全部参数
  // 画笔：工具设置（官方：大小 10..300 图像像素，羽化、流畅度、密度 0..100）和正在画的一笔。
  int brush_size = 90;
  int brush_feather = 100;
  int brush_flow = 100;
  int brush_density = 100;
  bool brush_erase = false;
  bool drag_brush = false;
  bool brush_on = false;  // 画笔设置显示着（选中画笔子项，或部位下拉选了画笔）
  std::vector<std::pair<int, int>> stroke_points;  // 正在画的一笔，位置是图像百分比的百倍（0..10000）
  std::string pending_region;  // 自定义页签点了画笔、渐变、取样：下一次在画布上拖动按它新建子项
  bool whole = false;     // 蒙版列表里选的是“全图”（不选任何蒙版）
  bool editing = false;   // “编辑蒙版”打开：右侧显示选中蒙版的子项编辑（部位、叠加方式、画笔、渐变位置）
  std::set<int> collapsed;  // 蒙版列表里收起子项的蒙版
  QString signature;
};

// 蒙版列表（画布上的浮出面板）里每一行的数据：编号、种类（mask 蒙版、item 子项、edit “编辑蒙版”按钮行、whole 全图）、
// 是否选中。冒烟测试按这几个角色找行。
constexpr int kMaskRowId = Qt::UserRole;
constexpr int kMaskRowKind = Qt::UserRole + 1;
constexpr int kMaskRowSelected = Qt::UserRole + 2;

// 蒙版列表的小图（官方 9.3.0 的样子；蒙版缩略图还没有渲染，先画底色）。图都画在 40×32 里，靠左。
QPixmap maskListCanvas() {
  QPixmap pixmap(80, 64);
  pixmap.setDevicePixelRatio(2.0);
  pixmap.fill(Qt::transparent);
  return pixmap;
}
QIcon maskThumbIcon(bool selected) {
  auto pixmap = maskListCanvas();
  QPainter painter(&pixmap);
  painter.fillRect(QRectF(0, 0, 32, 32), QColor(0x28, 0x29, 0x27));
  if (selected) {
    painter.setPen(QPen(QColor(0xff, 0xd5, 0x34), 1.0));
    painter.drawRect(QRectF(0.5, 0.5, 31, 31));
  }
  painter.end();
  return QIcon(pixmap);
}
QIcon wholeThumbIcon(const QImage& image) {
  auto pixmap = maskListCanvas();
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::SmoothPixmapTransform);
  painter.fillRect(QRectF(0, 0, 32, 32), QColor(0x0d, 0x0d, 0x0d));
  if (!image.isNull()) {
    QSizeF size = image.size();
    size.scale(30, 30, Qt::KeepAspectRatio);
    painter.drawImage(QRectF((32 - size.width()) / 2, (32 - size.height()) / 2, size.width(), size.height()), image);
  }
  painter.setPen(QPen(QColor(0x5a, 0x5c, 0x60), 1.0));
  painter.drawRect(QRectF(0.5, 0.5, 31, 31));
  painter.end();
  return QIcon(pixmap);
}
// 子项：左边一个“↳”折角，右边一块 24×24 的底（官方是部位类型的图标，图标文件还没有，只画底）。
QIcon maskItemIcon() {
  auto pixmap = maskListCanvas();
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QPen(QColor(0xb4, 0xb6, 0xb8), 1.0));
  painter.drawLine(QPointF(1.5, 6), QPointF(1.5, 11.5));
  painter.drawLine(QPointF(1.5, 11.5), QPointF(6, 11.5));
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(0x2d, 0x2f, 0x31));
  painter.drawRect(QRectF(9, 4, 24, 24));
  painter.end();
  return QIcon(pixmap);
}
// “创建新蒙版”前面的浅灰圆加号。
QIcon plusCircleIcon() {
  QPixmap pixmap(76, 48);  // 圆 24 宽，右边空 14 和文字隔开
  pixmap.setDevicePixelRatio(2.0);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(0xb5, 0xb5, 0xb7));
  painter.drawEllipse(QRectF(0, 0, 24, 24));
  painter.setPen(QPen(QColor(0x2a, 0x2c, 0x31), 1.6));
  painter.drawLine(QPointF(12, 7), QPointF(12, 17));
  painter.drawLine(QPointF(7, 12), QPointF(17, 12));
  painter.end();
  return QIcon(pixmap);
}

// 主体页签的“常用蒙版”（官方 9.3.0 的顺序）。名字来自 kernel/mask 的部位表；其余部位在蒙版列表的部位下拉里。
constexpr const char* kCommonParts[] = {"person.all",       "clothes.all",     "person.skin.all", "person.face_skin",
                                        "person.body_skin", "person.hair",     "person.eyebrow",  "person.eye_white",
                                        "person.iris",      "person.lips",     "person.teeth",    "person.oral_cavity",
                                        "clothes.other_objects"};
// 自定义页签里已经接上的选区（作用部位的参数值）；智能、自由、快速选区还没有。
bool customRegionLive(const std::string& id) {
  return id == "brush" || id == "linear" || id == "radial" || id == "color_range" || id == "luminance_range";
}
QString partLabel(const std::string& id) {
  for (const auto& part : mask::semantic_parts()) {
    if (id == part.id) return QString::fromUtf8(part.label);
  }
  return qs(id);
}

// 在画布上拖出渐变（官方的做法）：线性从起点拖到终点，起点一侧强度 1，终点一侧 0；径向从中心拖到外缘一个角，得到
// 椭圆。位置按图像百分比，换算到参数：线性的中心取两点中点、角度取方向、范围取两点距离占对角线的百分比；径向的
// 中心取起点、横向半径取水平距离、椭圆比例取纵横距离比。每个参数经同一条请求路径：拖动中是预览，松开提交。
struct GradientParams {
  int x = 50, y = 50, angle = 0, size = 50, ratio = 100;
  bool valid = false;
};

GradientParams gradient_from_drag(bool radial, const QPointF& start, const QPointF& end, const QSize& image) {
  GradientParams out;
  const double w = image.width();
  const double h = image.height();
  if (w <= 0 || h <= 0) return out;
  const double dx = (end.x() - start.x()) / 100.0 * w;  // 像素
  const double dy = (end.y() - start.y()) / 100.0 * h;
  const double diagonal = std::sqrt(w * w + h * h);
  if (!radial) {
    const double length = std::sqrt(dx * dx + dy * dy);
    if (length < 1.0) return out;
    out.x = static_cast<int>(std::lround((start.x() + end.x()) / 2.0));
    out.y = static_cast<int>(std::lround((start.y() + end.y()) / 2.0));
    out.angle = static_cast<int>(std::lround(std::atan2(dy, dx) * 180.0 / 3.14159265358979323846));
    out.size = std::clamp(static_cast<int>(std::lround(length / diagonal * 100.0)), 0, 100);
  } else {
    const double rx = std::abs(dx);
    const double ry = std::abs(dy);
    if (rx < 1.0 || ry < 1.0) return out;  // 纵或横为 0 得到空蒙版
    out.x = static_cast<int>(std::lround(start.x()));
    out.y = static_cast<int>(std::lround(start.y()));
    out.size = std::clamp(static_cast<int>(std::lround(2.0 * rx / diagonal * 100.0)), 0, 100);
    out.ratio = std::clamp(static_cast<int>(std::lround(ry / rx * 100.0)), 5, 400);
  }
  out.valid = true;
  return out;
}

std::shared_ptr<CanvasHooks> addLocalPanel(QWidget* modules, QVBoxLayout* layout, PageActions* actions,
                                           PixCakeImageView* canvas) {
  auto* body = new QWidget(modules);
  body->setObjectName(QStringLiteral("refine-local-body"));
  auto* body_layout = new QVBoxLayout(body);
  body_layout->setContentsMargins(0, 0, 0, 0);
  body_layout->setSpacing(0);
  layout->addWidget(body);
  const auto local_rows = panels::color_section_rows("color.local", panels::refine_local_sections(), "refine-local-");
  if (actions == nullptr || !actions->has_photo()) {
    addSections(body, body_layout, local_rows, nullptr, "local");
    return nullptr;
  }
  const auto state = std::make_shared<LocalState>();
  const auto rebuild = std::make_shared<std::function<void()>>();
  // 蒙版列表：画布区上浮出的面板（官方 9.3.0），只在局部调色页、开关打开时显示（开关和显示见 modulePage）。
  QPointer<PixCakeFloatingPanel> mask_panel;
  if (canvas != nullptr && canvas->parentWidget() != nullptr) {
    mask_panel = new PixCakeFloatingPanel(qtext("local.mask.list"), canvas->parentWidget());
    mask_panel->setObjectName(QStringLiteral("refine-mask-panel"));
    mask_panel->hide();
  }
  // 全图的缩略图跟着预览帧换（树每次重建都换新的，这里记着当前那棵）。
  const auto current_tree = std::make_shared<QPointer<PixCakeMaskTree>>();
  const auto current_fit = std::make_shared<std::function<void()>>();  // 画布区变大小时按新高度重算树高
  const auto fitted = std::make_shared<std::atomic_bool>(false);
  if (canvas != nullptr) {
    canvas->onImageChanged([current_tree, view = QPointer<PixCakeImageView>(canvas)] {
      const QPointer<PixCakeMaskTree> tree = *current_tree;
      if (!tree || !view) return;
      for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        auto* row = tree->topLevelItem(i);
        if (row->data(0, kMaskRowKind).toString() != QStringLiteral("whole")) continue;
        const QSignalBlocker quiet(tree);
        row->setIcon(0, wholeThumbIcon(view->image()));
      }
    });
  }
  const auto binding_of = [](int mask, int item, const std::string& parameter) {
    return panels::SliderBinding{"color.local", local_instance_prefix(mask) + local_item_prefix(item) + parameter};
  };
  const auto items_of = [actions, binding_of](int mask) {
    std::vector<std::pair<int, std::string>> items;  // 子项编号和它的部位
    for (int item = 0; item <= kMaxLocalItems; ++item) {
      const auto region = actions->parameter_value(binding_of(mask, item, "region"));
      if (region && *region != "all") items.emplace_back(item, *region);
    }
    return items;
  };
  const auto signature = [actions, state, items_of] {
    QString text = QString::number(state->mask) + QLatin1Char('/') + QString::number(state->item);
    for (const int mask : actions->local_masks()) {
      text += QStringLiteral("|m%1").arg(mask);
      for (const auto& [item, region] : items_of(mask)) text += QStringLiteral(",%1:%2").arg(item).arg(qs(region));
    }
    return text;
  };
  *rebuild = [=, weak = std::weak_ptr<std::function<void()>>(rebuild)] {
    while (auto* entry = body_layout->takeAt(0)) {
      if (auto* widget = entry->widget()) {
        widget->hide();
        widget->deleteLater();
      }
      delete entry;
    }
    const auto masks = actions->local_masks();
    if (state->whole) {
      state->mask = -1;
      state->item = -1;
    } else if (std::find(masks.begin(), masks.end(), state->mask) == masks.end()) {
      state->mask = masks.empty() ? -1 : masks.front();
      state->item = -1;
    }
    const auto region_label = [](const std::string& id) {
      for (const auto& option : panels::choice_options({"color.local", "region"})) {
        if (option.id == id) return qs(option.label);
      }
      return qs(id);
    };

    auto* area_widget = new QWidget(body);
    area_widget->setObjectName(QStringLiteral("refine-local-masks-area"));
    auto* area = new QVBoxLayout(area_widget);
    area->setContentsMargins(12, 12, 12, 12);
    area->setSpacing(8);
    body_layout->addWidget(area_widget);
    const auto again = [state, weak, signature] {
      state->signature = signature();
      if (const auto run = weak.lock()) (*run)();
    };
    // 调整分组：和色彩调节同一套（ui/panels 的同一张表），写到选中蒙版自己的参数（m<N>: 前缀）；没有蒙版时禁用。
    const auto add_adjustments = [=] {
      const bool live = state->mask >= 0;
      const int mask = state->mask;
      addSections(body, body_layout, local_rows, live ? actions : nullptr, "local",
                  [binding_of, mask, live](const panels::SliderBinding& binding) {
                    return live ? binding_of(mask, 0, binding.parameter_id) : binding;
                  });
    };
    const auto fresh_mask = [actions] {
      const auto existing = actions->local_masks();
      int fresh = 1;
      while (fresh <= kMaxLocalInstances && std::find(existing.begin(), existing.end(), fresh) != existing.end()) ++fresh;
      return fresh > kMaxLocalInstances ? -1 : fresh;
    };
    const auto lone_region = [items_of](int mask) {
      const auto items = items_of(mask);
      return items.size() == 1 && items.front().first == 0 ? items.front().second : std::string();
    };

    // 蒙版区（官方顺序）：主体/宠物/背景/自定义页签、创建蒙版、常用蒙版、羽化。
    auto* tabs = new QWidget(body);
    auto* tabs_layout = new QHBoxLayout(tabs);
    tabs_layout->setContentsMargins(0, 0, 0, 0);
    tabs_layout->setSpacing(8);
    auto* tab_group = new QButtonGroup(tabs);
    auto* pages = new QStackedWidget(body);
    pages->setObjectName(QStringLiteral("refine-local-parts"));
    const auto grid_page = [pages] {
      auto* page = new QWidget(pages);
      auto* grid = new QGridLayout(page);
      grid->setContentsMargins(0, 0, 0, 0);
      grid->setHorizontalSpacing(8);
      grid->setVerticalSpacing(8);
      pages->addWidget(page);
      return grid;
    };
    const auto part_button = [](QGridLayout* grid, const QString& label, const QString& id, bool live, bool checked) {
      auto* button = new QPushButton(label, grid->parentWidget());
      button->setObjectName(QStringLiteral("refine-local-part"));
      button->setProperty("part", id);
      button->setAccessibleName(label);
      button->setCheckable(true);
      button->setChecked(checked);
      button->setEnabled(live);
      if (!live) button->setToolTip(qtext("local.part.pending"));
      const int index = grid->count();
      grid->addWidget(button, index / 2, index % 2);
      return button;
    };
    const std::string lone_selected = state->mask >= 0 ? lone_region(state->mask) : std::string();
    int tab_index = 0;
    for (const auto& tab : ui::shell::page_list("refine-local-tabs")) {
      auto* tab_button = new QPushButton(qs(tab.text), tabs);
      tab_button->setObjectName(QStringLiteral("refine-local-tab"));
      tab_button->setCheckable(true);
      tab_group->addButton(tab_button, tab_index);
      tabs_layout->addWidget(tab_button);
      auto* grid = grid_page();
      if (tab.id == "subject") {
        for (const char* id : kCommonParts) {
          auto* button = part_button(grid, partLabel(id), QString::fromLatin1(id), true, lone_selected == id);
          const std::string part = id;
          QObject::connect(button, &QPushButton::clicked, button, [=] {
            // 已有只含这个部位的蒙版就选中它，否则新建一个（名字是部位名，一步历史）。
            for (const int mask : actions->local_masks()) {
              if (lone_region(mask) == part) {
                state->mask = mask;
                state->item = 0;
                state->whole = false;
                again();
                return;
              }
            }
            const int fresh = fresh_mask();
            if (fresh < 0) return;
            actions->request_parameters({{binding_of(fresh, 0, "name"), partLabel(part).toStdString()},
                                         {binding_of(fresh, 0, "region"), part}});
            state->mask = fresh;
            state->item = 0;
            state->whole = false;
            again();
          });
        }
      } else {
        for (const auto& entry : ui::shell::page_list("refine-local-" + tab.id)) {
          const bool live = tab.id == "custom" && customRegionLive(entry.id);
          auto* button = part_button(grid, qs(entry.text), qs(entry.id), live,
                                     live && (state->pending_region == entry.id || lone_selected == entry.id));
          if (!live) continue;
          const std::string region = entry.id;
          QObject::connect(button, &QPushButton::clicked, button, [=] {
            // 画笔、渐变、取样要在画布上拖：没有蒙版先建一个空蒙版，然后打开“在画布上拖动”，下一次拖动新建这个子项。
            if (state->mask < 0) {
              const int fresh = fresh_mask();
              if (fresh < 0) return;
              actions->request_parameter(binding_of(fresh, 0, "region"), "all", true);
              state->mask = fresh;
            }
            state->item = -1;
            state->whole = false;
            state->pending_region = region;
            state->draw = true;
            state->editing = true;  // 画笔设置、拖动开关在“编辑蒙版”里
            again();
          });
        }
      }
      if (tab.id == uiState().local_tab) {
        tab_button->setChecked(true);
        pages->setCurrentIndex(tab_index);
      }
      const std::string tab_id = tab.id;
      QObject::connect(tab_button, &QPushButton::toggled, pages, [pages, tab_index, tab_id](bool on) {
        if (!on) return;
        pages->setCurrentIndex(tab_index);
        uiState().local_tab = tab_id;
      });
      ++tab_index;
    }
    tabs_layout->addStretch(1);
    area->addWidget(tabs);
    auto* create = action(qtext("local.mask.new"), body, true);
    create->setObjectName(QStringLiteral("refine-local-new"));
    area->addWidget(create);
    auto* common = new QWidget(body);
    auto* common_layout = new QHBoxLayout(common);
    common_layout->setContentsMargins(0, 4, 0, 0);
    auto* common_title = text(qtext("local.common"));
    common_title->setObjectName(QStringLiteral("refine-local-common"));
    common_layout->addWidget(common_title);
    common_layout->addStretch(1);
    for (const char* key : {"local.manage", "local.linked"}) {
      auto* link = text(qtext(key));
      link->setEnabled(false);
      common_layout->addWidget(link);
    }
    area->addWidget(common);
    area->addWidget(pages);
    const char* feather = panels::refine_local_feather_label();
    addDisabledSlider(body, area, panels::slider_spec(feather, panels::refine_local_binding(feather)), QString());
    // 新建空蒙版（右侧“创建蒙版”和蒙版列表的“创建新蒙版”）：没有子项，直接打开“编辑蒙版”去加。
    const auto create_empty = [=] {
      const int fresh = fresh_mask();
      if (fresh < 0) return;
      actions->request_parameter(binding_of(fresh, 0, "region"), "all", true);
      state->mask = fresh;
      state->item = -1;
      state->whole = false;
      state->editing = true;
      again();
    };
    QObject::connect(create, &QPushButton::clicked, create, create_empty);
    const auto items = state->mask >= 0 ? items_of(state->mask) : std::vector<std::pair<int, std::string>>{};
    if (std::none_of(items.begin(), items.end(), [&](const auto& pair) { return pair.first == state->item; })) {
      // 自定义页签点了画笔、渐变、取样：不选已有子项，下一次拖动新建。
      state->item = items.empty() || !state->pending_region.empty() ? -1 : items.front().first;
    }
    const auto item_text = [=](int mask, int item, const std::string& region) {
      const auto how = item == 0 ? std::optional<std::string>("add") : actions->parameter_value(binding_of(mask, item, "mode"));
      const bool inverted = actions->parameter_value(binding_of(mask, item, "invert")) == std::optional<std::string>("on");
      return qtext("local.item.text")
                 .arg(region_label(region), qtext(how == std::optional<std::string>("subtract") ? "local.item.mode.subtract"
                                                                                                 : "local.item.mode.add")) +
             (inverted ? qtext("local.item.text.invert") : QString());
    };

    // 蒙版列表（浮出面板）：创建新蒙版；每个蒙版一行（缩略图、名字，双击改名，右键改名或删除），展开是它的子项，选中的
    // 蒙版下面是“编辑蒙版”；最后一行全图。选中的蒙版连同子项放在一块深色底上，缩略图描黄边（官方样子）。
    if (mask_panel) {
      auto* panel_body = mask_panel->body();
      while (auto* entry = panel_body->takeAt(0)) {
        if (auto* widget = entry->widget()) {
          widget->hide();
          widget->deleteLater();
        }
        delete entry;
      }
      auto* create_new = new QToolButton(mask_panel);
      create_new->setObjectName(QStringLiteral("refine-mask-new"));
      create_new->setText(qtext("local.mask.create-new"));
      create_new->setIcon(plusCircleIcon());
      create_new->setIconSize(QSize(38, 24));
      create_new->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
      create_new->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
      QObject::connect(create_new, &QToolButton::clicked, create_new, create_empty);
      panel_body->addWidget(create_new);
      auto* tree = new PixCakeMaskTree;
      tree->setObjectName(QStringLiteral("refine-mask-tree"));
      tree->setAccessibleName(qtext("local.mask.list"));
      tree->setContextMenuPolicy(Qt::CustomContextMenu);
      tree->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
      const QColor group(0x18, 0x19, 0x1c);
      const QColor picked(0x26, 0x28, 0x2c);
      QPushButton* edit_button = nullptr;
      QTreeWidgetItem* edit_row = nullptr;
      for (const int mask : masks) {
        const bool selected = mask == state->mask;
        const auto name = actions->parameter_value(binding_of(mask, 0, "name"));
        auto* row = new QTreeWidgetItem(tree);
        row->setText(0, name ? qs(*name) : qtext("local.mask.name").arg(mask));
        row->setData(0, kMaskRowId, mask);
        row->setData(0, kMaskRowKind, QStringLiteral("mask"));
        row->setData(0, kMaskRowSelected, selected);
        row->setIcon(0, maskThumbIcon(selected));
        row->setFlags(row->flags() | Qt::ItemIsEditable);
        row->setSizeHint(0, QSize(0, 40));
        if (selected) row->setBackground(0, group);
        for (const auto& [item, region] : items_of(mask)) {
          auto* child = new QTreeWidgetItem(row);
          child->setText(0, item_text(mask, item, region));
          child->setData(0, kMaskRowId, item);
          child->setData(0, kMaskRowKind, QStringLiteral("item"));
          child->setData(0, kMaskRowSelected, selected && item == state->item);
          child->setIcon(0, maskItemIcon());
          child->setSizeHint(0, QSize(0, 32));
          if (selected) child->setBackground(0, selected && item == state->item ? picked : group);
        }
        if (selected) {
          edit_row = new QTreeWidgetItem(row);
          edit_row->setData(0, kMaskRowKind, QStringLiteral("edit"));
          edit_row->setFlags(Qt::ItemIsEnabled);
          edit_row->setSizeHint(0, QSize(0, 34));
          edit_row->setBackground(0, group);
        }
        row->setExpanded(state->collapsed.count(mask) == 0);
      }
      auto* whole = new QTreeWidgetItem(tree);
      whole->setText(0, qtext("local.mask.whole"));
      whole->setData(0, kMaskRowId, -1);
      whole->setData(0, kMaskRowKind, QStringLiteral("whole"));
      whole->setData(0, kMaskRowSelected, state->mask < 0);
      whole->setIcon(0, wholeThumbIcon(canvas != nullptr ? canvas->image() : QImage()));
      whole->setSizeHint(0, QSize(0, 40));
      if (edit_row != nullptr) {
        edit_row->setFirstColumnSpanned(true);
        edit_button = new QPushButton(qtext("local.mask.edit"), tree);
        edit_button->setObjectName(QStringLiteral("refine-mask-edit"));
        edit_button->setCheckable(true);
        edit_button->setChecked(state->editing);
        tree->setItemWidget(edit_row, 0, edit_button);
        // 行高取按钮自己要的高度（树按两者较大的排，算总高时要一致）。
        edit_button->ensurePolished();
        edit_row->setSizeHint(0, QSize(0, std::max(34, edit_button->sizeHint().height())));
        QObject::connect(edit_button, &QPushButton::toggled, edit_button, [state, again](bool on) {
          state->editing = on;
          if (!on) state->draw = false;
          again();
        });
      }
      auto* tree_box = new QWidget(mask_panel);  // 树两边、下边留 6（官方的内边距）
      auto* tree_layout = new QVBoxLayout(tree_box);
      tree_layout->setContentsMargins(6, 0, 6, 6);
      tree_layout->addWidget(tree);
      panel_body->addWidget(tree_box);
      // 树的高度按内容；画布区放不下时（面板头和“创建新蒙版”约 110）才出滚动条。画布区还没排好版时不限。
      const auto fit = [tree = QPointer<PixCakeMaskTree>(tree), panel = mask_panel] {
        if (!tree || !panel) return;
        const int room = panel->room() - 110;
        const int wanted = tree->contentHeight() + 2 * tree->frameWidth() + 2;
        tree->setFixedHeight(room >= 80 ? std::min(wanted, room) : wanted);
        panel->refit();
      };
      fit();
      if (!fitted->exchange(true)) {
        mask_panel->onParentResized([current_fit] {
          if (*current_fit) (*current_fit)();
        });
      }
      *current_fit = fit;
      QObject::connect(tree, &QTreeWidget::itemExpanded, tree, [state, fit](QTreeWidgetItem* row) {
        state->collapsed.erase(row->data(0, kMaskRowId).toInt());
        fit();
      });
      QObject::connect(tree, &QTreeWidget::itemCollapsed, tree, [state, fit](QTreeWidgetItem* row) {
        state->collapsed.insert(row->data(0, kMaskRowId).toInt());
        fit();
      });
      QObject::connect(tree, &QTreeWidget::itemClicked, tree, [state, again](QTreeWidgetItem* row, int) {
        const QString kind = row->data(0, kMaskRowKind).toString();
        if (kind == QStringLiteral("mask")) {
          if (state->mask == row->data(0, kMaskRowId).toInt() && !state->whole) return;
          state->mask = row->data(0, kMaskRowId).toInt();
          state->item = -1;
        } else if (kind == QStringLiteral("item") && row->parent() != nullptr) {
          state->mask = row->parent()->data(0, kMaskRowId).toInt();
          state->item = row->data(0, kMaskRowId).toInt();
          state->pending_region.clear();
        } else if (kind == QStringLiteral("whole")) {
          state->whole = true;
          state->draw = false;
          again();
          return;
        } else {
          return;
        }
        state->whole = false;
        again();
      });
      QObject::connect(tree, &QTreeWidget::itemChanged, tree, [=](QTreeWidgetItem* row, int) {
        if (row->data(0, kMaskRowKind).toString() != QStringLiteral("mask")) return;
        const int mask = row->data(0, kMaskRowId).toInt();
        const std::string text = row->text(0).trimmed().toStdString();
        // 不合格的名称（空、超过 20 个字符）还原成现在的名字；合格的提交成一步历史。
        if (!text.empty() && validate_parameter_display_value("color.local", "m1:name", text)) {
          if (actions->parameter_value(binding_of(mask, 0, "name")) != std::optional<std::string>(text)) {
            actions->request_parameter(binding_of(mask, 0, "name"), text, true);
          }
        }
        QTimer::singleShot(0, tree, again);  // 改名的编辑器还在这棵树上，等它关掉再重建
      });
      QObject::connect(tree, &QTreeWidget::customContextMenuRequested, tree,
                       [=, tree = QPointer<PixCakeMaskTree>(tree)](const QPoint& at) {
        if (!tree) return;
        auto* row = tree->itemAt(at);
        if (row == nullptr || row->data(0, kMaskRowKind).toString() != QStringLiteral("mask")) return;
        const int mask = row->data(0, kMaskRowId).toInt();
        QMenu menu(tree);
        auto* rename = menu.addAction(qtext("local.mask.rename"));
        auto* remove = menu.addAction(qtext("local.mask.remove"));
        auto* chosen = menu.exec(tree->viewport()->mapToGlobal(at));
        if (chosen == rename) {
          tree->editItem(row, 0);
        } else if (chosen == remove) {
          actions->request_remove_local_mask(mask);
          if (state->mask == mask) {
            state->mask = -1;
            state->item = -1;
          }
          again();
        }
      });
      *current_tree = tree;
    }

    // 编辑蒙版（右侧）：选中蒙版、“编辑蒙版”打开时才有。官方点“编辑蒙版”后的界面还没看到，这里放的是本工程已有的
    // 子项编辑：部位和叠加方式、在画布上拖动、添加/删除子项、删除蒙版、反相、画笔设置、渐变和范围的位置滑杆。
    if (state->mask < 0 || !state->editing) {
      add_adjustments();
      state->signature = signature();
      return;
    }
    auto* edit = new QFrame(body);
    edit->setObjectName(QStringLiteral("refine-local-edit"));
    auto* edit_layout = new QVBoxLayout(edit);
    edit_layout->setContentsMargins(12, 8, 12, 12);
    edit_layout->setSpacing(8);
    auto* edit_title = text(qtext("local.mask.edit"));
    edit_title->setObjectName(QStringLiteral("refine-local-edit-title"));
    edit_layout->addWidget(edit_title);
    body_layout->addWidget(edit);
    area = edit_layout;
    auto* add_row = new QWidget(body);
    auto* add_layout = new QHBoxLayout(add_row);
    add_layout->setContentsMargins(0, 0, 0, 0);
    auto* region_box = new QComboBox(add_row);
    region_box->setObjectName(QStringLiteral("refine-local-item-region"));
    region_box->setAccessibleName(qtext("local.item.add"));
    for (const auto& option : panels::choice_options({"color.local", "region"})) {
      if (option.id != "all") region_box->addItem(qs(option.label), qs(option.id));
    }
    if (!state->pending_region.empty()) region_box->setCurrentIndex(region_box->findData(qs(state->pending_region)));
    auto* mode_box = new QComboBox(add_row);
    mode_box->setObjectName(QStringLiteral("refine-local-item-mode"));
    mode_box->addItem(qtext("local.item.mode.add"), QStringLiteral("add"));
    mode_box->addItem(qtext("local.item.mode.subtract"), QStringLiteral("subtract"));
    add_layout->addWidget(region_box, 1);
    add_layout->addWidget(mode_box);
    area->addWidget(add_row);
    auto* item_buttons = new QWidget(body);
    auto* item_buttons_layout = new QHBoxLayout(item_buttons);
    item_buttons_layout->setContentsMargins(0, 0, 0, 0);
    auto* draw = action(qtext("local.draw"), item_buttons, true);
    draw->setObjectName(QStringLiteral("refine-local-draw"));
    draw->setCheckable(true);
    draw->setChecked(state->draw);
    QObject::connect(draw, &QPushButton::toggled, draw, [state](bool on) { state->draw = on; });
    item_buttons_layout->addWidget(draw);
    auto* add_item = action(qtext("local.item.add"), item_buttons, true);
    add_item->setObjectName(QStringLiteral("refine-local-item-add"));
    auto* remove_item = action(qtext("local.item.remove"), item_buttons, state->item >= 0);
    remove_item->setObjectName(QStringLiteral("refine-local-item-remove"));
    item_buttons_layout->addWidget(add_item);
    item_buttons_layout->addWidget(remove_item);
    area->addWidget(item_buttons);
    auto* remove = action(qtext("local.mask.remove"), edit, true);
    remove->setObjectName(QStringLiteral("refine-local-delete"));
    area->addWidget(remove);
    QObject::connect(remove, &QPushButton::clicked, remove, [=] {
      if (state->mask < 0) return;
      actions->request_remove_local_mask(state->mask);
      state->mask = -1;
      state->item = -1;
      again();
    });
    QObject::connect(add_item, &QPushButton::clicked, add_item, [=] {
      int free_item = -1;
      for (int item = 0; item <= kMaxLocalItems && free_item < 0; ++item) {
        if (std::none_of(items.begin(), items.end(), [&](const auto& pair) { return pair.first == item; })) free_item = item;
      }
      if (free_item < 0 || region_box->currentIndex() < 0) return;
      std::vector<std::pair<panels::SliderBinding, std::string>> values;  // 一步历史
      if (free_item > 0) {
        values.emplace_back(binding_of(state->mask, free_item, "mode"), mode_box->currentData().toString().toStdString());
      }
      values.emplace_back(binding_of(state->mask, free_item, "region"), region_box->currentData().toString().toStdString());
      actions->request_parameters(values);
      state->item = free_item;
      again();
    });
    QObject::connect(remove_item, &QPushButton::clicked, remove_item, [=] {
      if (state->item < 0) return;
      actions->request_parameter(binding_of(state->mask, state->item, "region"), "all", true);  // 整个画面 = 不参与
      state->item = -1;
      again();
    });

    // 选中的子项：反相（官方“显示蒙版反相”，真的反转这个子项的选区）。
    if (state->item >= 0 && std::any_of(items.begin(), items.end(), [&](const auto& pair) { return pair.first == state->item; })) {
      auto* invert = new QCheckBox(qtext("local.item.invert"), body);
      invert->setObjectName(QStringLiteral("refine-local-item-invert"));
      invert->setAccessibleName(qtext("local.item.invert"));
      const auto invert_binding = binding_of(state->mask, state->item, "invert");
      invert->setChecked(actions->parameter_value(invert_binding) == std::optional<std::string>("on"));
      area->addWidget(invert);
      QObject::connect(invert, &QCheckBox::toggled, invert, [=](bool on) {
        actions->request_parameter(invert_binding, on ? "on" : "off", true);
        again();
      });
    }
    // 选中子项是渐变时的位置滑杆。
    const Refresh refresh = std::make_shared<EnableRefresh>();
    const auto selected_region = std::find_if(items.begin(), items.end(), [&](const auto& pair) { return pair.first == state->item; });
    const std::string item_region = selected_region != items.end() ? selected_region->second : std::string();
    // 选中子项自己的参数前缀：渐变是 gradient_，颜色范围是 range_，亮度范围是 lum_。
    const std::string item_prefix = item_region == "linear" || item_region == "radial" ? "gradient_"
                                    : item_region == "color_range"                      ? "range_"
                                    : item_region == "luminance_range"                  ? "lum_"
                                                                                        : "";
    // 画笔设置：选中的子项是画笔，或者“添加子项”的部位下拉选了画笔时显示。设置只存在界面里，每一笔画下时带进笔触。
    {
      auto* brush_box = new QWidget(body);
      brush_box->setObjectName(QStringLiteral("refine-local-brush"));
      auto* brush_layout = new QVBoxLayout(brush_box);
      brush_layout->setContentsMargins(0, 0, 0, 0);
      brush_layout->setSpacing(4);
      auto* tools = new QWidget(brush_box);
      auto* tools_layout = new QHBoxLayout(tools);
      tools_layout->setContentsMargins(0, 0, 0, 0);
      auto* paint = new QPushButton(qtext("local.brush.tool.paint"), tools);
      auto* erase = new QPushButton(qtext("local.brush.tool.erase"), tools);
      for (auto* button : {paint, erase}) {
        button->setCheckable(true);
        button->setAutoExclusive(true);
        tools_layout->addWidget(button);
      }
      paint->setObjectName(QStringLiteral("refine-local-brush-paint"));
      erase->setObjectName(QStringLiteral("refine-local-brush-erase"));
      paint->setChecked(!state->brush_erase);
      erase->setChecked(state->brush_erase);
      QObject::connect(erase, &QPushButton::toggled, erase, [state](bool on) { state->brush_erase = on; });
      brush_layout->addWidget(tools);
      const auto setting = [&](const QString& label, const char* object, int low, int high, int* value) {
        auto* line = new QWidget(brush_box);
        auto* line_layout = new QHBoxLayout(line);
        line_layout->setContentsMargins(0, 0, 0, 0);
        line_layout->setSpacing(6);
        auto* slider = new QSlider(Qt::Horizontal, line);
        slider->setObjectName(QString::fromLatin1(object));
        slider->setAccessibleName(label);
        slider->setRange(low, high);
        slider->setValue(*value);
        scrollPastOnWheel(slider);
        auto* reading = text(QString::number(*value));
        line_layout->addWidget(text(label));
        line_layout->addWidget(slider, 1);
        line_layout->addWidget(reading);
        QObject::connect(slider, &QSlider::valueChanged, slider, [value, reading = QPointer<QLabel>(reading)](int v) {
          *value = v;
          if (reading) reading->setText(QString::number(v));
        });
        brush_layout->addWidget(line);
      };
      setting(qtext("local.brush.size"), "refine-local-brush-size", 10, 300, &state->brush_size);
      setting(qs(panels::refine_local_feather_label()), "refine-local-brush-feather", 0, 100, &state->brush_feather);
      setting(qtext("local.brush.flow"), "refine-local-brush-flow", 0, 100, &state->brush_flow);
      setting(qtext("local.brush.density"), "refine-local-brush-density", 0, 100, &state->brush_density);
      area->addWidget(brush_box);
      const auto show = [state, brush_box = QPointer<QWidget>(brush_box), region_box = QPointer<QComboBox>(region_box), item_region] {
        if (!brush_box) return;
        const bool on = item_region == "brush" || (region_box && region_box->currentData().toString() == QStringLiteral("brush"));
        brush_box->setVisible(on);
        state->brush_on = on;
      };
      show();
      QObject::connect(region_box, &QComboBox::currentIndexChanged, brush_box, [show](int) { show(); });
    }
    const auto rows = panels::refine_local_rows();
    if (!item_prefix.empty()) {
      for (const auto& row : rows) {
        if (row.kind != panels::PanelRowKind::Slider || row.binding->parameter_id.rfind(item_prefix, 0) != 0) continue;
        if (item_region == "linear" && row.binding->parameter_id == "gradient_ratio") continue;  // 椭圆比例只对径向
        const auto bound = binding_of(state->mask, state->item, row.binding->parameter_id);
        addDisabledSlider(edit, edit_layout, panels::slider_spec(row.label, bound),
                          qs("refine-local-item-" + row.binding->parameter_id), actions, bound, refresh);
      }
    }
    // 整个蒙版的调整：和色彩调节同一套折叠分组。
    add_adjustments();
    state->signature = signature();
  };
  (*rebuild)();
  // 撤销、重做、粘贴、预设让结构变了才重建；滑杆提交不重建。
  actions->watch_history(body, [=] {
    if (signature() != state->signature) (*rebuild)();
  });

  // 画布：选中蒙版且打开“在画布上拖动”时接管鼠标。选中的子项是渐变就改它，否则按“添加子项”的部位下拉（线性或
  // 径向，默认线性）新建一个子项。画布上画出选中渐变的手柄（线性两端和连线，径向椭圆和内椭圆）。
  if (canvas == nullptr) return nullptr;
  const auto hooks = std::make_shared<CanvasHooks>();
  const QPointer<PixCakeImageView> view(canvas);
  hooks->accepts = [state, actions] { return state->draw && state->mask >= 0 && actions->has_photo(); };
  hooks->drag = [=](int phase, const QPointF& at) {
    if (!view) return;
    if (phase == 0) {
      const auto items = items_of(state->mask);
      const auto found = std::find_if(items.begin(), items.end(), [&](const auto& pair) { return pair.first == state->item; });
      state->drag_sample = false;
      state->drag_brush = false;
      state->stroke_points.clear();
      state->sample_min = 100.0;
      state->sample_max = 0.0;
      if (found != items.end() && found->second == "brush") {
        state->drag_item = found->first;  // 在选中的画笔子项上继续画
        state->drag_brush = true;
        state->drag_new = false;
      } else if (found != items.end() && (found->second == "color_range" || found->second == "luminance_range")) {
        state->drag_item = found->first;  // 取样：改选中的范围子项
        state->drag_sample = true;
        state->drag_region = found->second;
        state->drag_new = false;
      } else if (found != items.end() && (found->second == "linear" || found->second == "radial")) {
        state->drag_item = found->first;
        state->drag_radial = found->second == "radial";
        state->drag_new = false;
      } else {
        // 新建：部位下拉选了径向就是径向，否则线性；叠加方式取下拉。
        bool radial = false;
        QString how = QStringLiteral("add");
        QString picked;
        for (auto* box : body->findChildren<QComboBox*>(QStringLiteral("refine-local-item-region"))) {
          picked = box->currentData().toString();
          radial = picked == QStringLiteral("radial");
        }
        for (auto* box : body->findChildren<QComboBox*>(QStringLiteral("refine-local-item-mode"))) how = box->currentData().toString();
        state->pending_region.clear();
        int free_item = -1;
        for (int item = 0; item <= kMaxLocalItems && free_item < 0; ++item) {
          if (std::none_of(items.begin(), items.end(), [&](const auto& pair) { return pair.first == item; })) free_item = item;
        }
        if (free_item < 0) {
          state->drag_item = -1;
          return;
        }
        state->drag_item = free_item;
        state->drag_radial = radial;
        state->drag_new = true;
        state->drag_mode = how.toStdString();
        if (picked == QStringLiteral("brush")) {
          state->drag_brush = true;  // 部位下拉选了画笔：在画布上画第一笔来新建
        } else if (picked == QStringLiteral("color_range") || picked == QStringLiteral("luminance_range")) {
          state->drag_sample = true;  // 部位下拉选了颜色范围、亮度范围：点画布取样来新建
          state->drag_region = picked.toStdString();
        }
      }
      state->item = state->drag_item;
      state->drag_start = at;
      state->drag_values.clear();
      if (!state->drag_sample && !state->drag_brush) return;
    }
    if (state->drag_item < 0) return;
    if (state->drag_brush) {
      // 画笔：收集这一笔的点（图像百分比的百倍，离上一点不到一个显示像素的不要），松开时整笔作为一步提交。
      // 拖动中界面本地画出笔迹（hooks->paint），不写预览：笔触文本比预览请求的长度限制大。
      const QSize shown = view->image().size();
      if (shown.isEmpty()) return;
      const int px = std::clamp(static_cast<int>(std::lround(at.x() * 100.0)), 0, 10000);
      const int py = std::clamp(static_cast<int>(std::lround(at.y() * 100.0)), 0, 10000);
      const bool far_enough =
          state->stroke_points.empty() ||
          std::hypot((px - state->stroke_points.back().first) / 10000.0 * shown.width(),
                     (py - state->stroke_points.back().second) / 10000.0 * shown.height()) >= 1.0;
      if (far_enough) state->stroke_points.emplace_back(px, py);
      if (phase != 2) {
        view->update();
        return;
      }
      mask::BrushStroke stroke;
      stroke.erase = state->brush_erase;
      const double long_edge = std::max(shown.width(), shown.height());
      stroke.size = std::clamp(static_cast<int>(std::lround(state->brush_size / long_edge * 10000.0)), 1, 10000);
      stroke.feather = state->brush_feather;
      stroke.flow = state->brush_flow;
      stroke.density = state->brush_density;
      stroke.points = state->stroke_points;
      std::vector<mask::BrushStroke> all;
      if (const auto existing = actions->parameter_value(binding_of(state->mask, state->drag_item, "strokes"))) {
        mask::decode_brush_strokes(*existing, all);
      }
      const bool nothing_to_erase = stroke.erase && all.empty();
      all.push_back(stroke);
      const std::string joined = mask::encode_brush_strokes(all);
      state->stroke_points.clear();
      if (!nothing_to_erase && joined.size() <= mask::kMaxBrushTextChars) {
        // 笔触放最前，区域放最后：提交后给渲染程序的请求带最后一个值，它必须是短的。
        std::vector<std::pair<panels::SliderBinding, std::string>> values;
        values.emplace_back(binding_of(state->mask, state->drag_item, "strokes"), joined);
        if (state->drag_new && state->drag_item > 0) {
          values.emplace_back(binding_of(state->mask, state->drag_item, "mode"), state->drag_mode);
        }
        values.emplace_back(binding_of(state->mask, state->drag_item, "region"), "brush");
        actions->request_parameters(values);
      }
      state->drag_item = -1;
      state->drag_new = false;
      state->drag_brush = false;
      view->update();
      if (const auto run = std::weak_ptr<std::function<void()>>(rebuild).lock()) {
        state->signature = signature();
        (*run)();
      }
      return;
    }
    if (state->drag_sample) {
      // 取样：点图片取 7×7 的平均色（颜色范围），或拖动经过的亮度范围（亮度范围）。取的是画布上现在显示的画面。
      const QImage& shown = view->image();
      if (shown.isNull()) return;
      const int cx = std::clamp(static_cast<int>(std::lround(at.x() / 100.0 * shown.width())), 0, shown.width() - 1);
      const int cy = std::clamp(static_cast<int>(std::lround(at.y() / 100.0 * shown.height())), 0, shown.height() - 1);
      double sum[3] = {0, 0, 0};
      int count = 0;
      for (int y = std::max(0, cy - 3); y <= std::min(shown.height() - 1, cy + 3); ++y) {
        for (int x = std::max(0, cx - 3); x <= std::min(shown.width() - 1, cx + 3); ++x) {
          const QColor c = shown.pixelColor(x, y);
          sum[0] += c.red();
          sum[1] += c.green();
          sum[2] += c.blue();
          ++count;
        }
      }
      if (count == 0) return;
      const double r = sum[0] / count, g = sum[1] / count, b = sum[2] / count;
      const double luminance = (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255.0 * 100.0;
      state->sample_min = std::min(state->sample_min, luminance);
      state->sample_max = std::max(state->sample_max, luminance);
      state->drag_values.clear();
      const auto put = [&](const char* name, int value) {
        const auto binding = binding_of(state->mask, state->drag_item, name);
        state->drag_values.emplace_back(binding, std::to_string(value));
        actions->request_parameter(binding, std::to_string(value), false);
      };
      if (state->drag_new) {
        if (state->drag_item > 0) {
          const auto binding = binding_of(state->mask, state->drag_item, "mode");
          state->drag_values.emplace_back(binding, state->drag_mode);
          actions->request_parameter(binding, state->drag_mode, false);
        }
        const auto binding = binding_of(state->mask, state->drag_item, "region");
        state->drag_values.emplace_back(binding, state->drag_region);
        actions->request_parameter(binding, state->drag_region, false);
      }
      if (state->drag_region == "color_range") {
        put("range_r", static_cast<int>(std::lround(r)));
        put("range_g", static_cast<int>(std::lround(g)));
        put("range_b", static_cast<int>(std::lround(b)));
      } else {
        // 单击亮度 L：全选 L±10，渐变到 L±35（官方实测）；拖动时全选覆盖经过的 [最小−10, 最大+10]，外沿再各加 25。
        const int b0 = std::clamp(static_cast<int>(std::lround(state->sample_min - 10.0)), 0, 100);
        const int c0 = std::clamp(static_cast<int>(std::lround(state->sample_max + 10.0)), b0, 100);
        const int a0 = std::clamp(b0 - 25, 0, b0);
        const int d0 = std::clamp(c0 + 25, c0, 100);
        put("lum_a", a0);
        put("lum_b", b0);
        put("lum_c", c0);
        put("lum_d", d0);
      }
      if (phase == 2) {
        actions->request_parameters(state->drag_values);
        state->drag_item = -1;
        state->drag_new = false;
        state->drag_sample = false;
        if (const auto run = std::weak_ptr<std::function<void()>>(rebuild).lock()) {
          state->signature = signature();
          (*run)();
        }
      }
      return;
    }
    const auto params = gradient_from_drag(state->drag_radial, state->drag_start, at, view->image().size());
    if (!params.valid) return;
    // 拖动中只写预览；新建的子项到第一次有效移动才写区域和叠加方式的预览。松开时一次提交全部（一步历史）。
    state->drag_values.clear();
    const auto write = [&](const char* name, int value) {
      const auto binding = binding_of(state->mask, state->drag_item, name);
      state->drag_values.emplace_back(binding, std::to_string(value));
      actions->request_parameter(binding, std::to_string(value), false);
    };
    if (state->drag_new) {
      if (state->drag_item > 0) {
        const auto binding = binding_of(state->mask, state->drag_item, "mode");
        state->drag_values.emplace_back(binding, state->drag_mode);
        actions->request_parameter(binding, state->drag_mode, false);
      }
      const auto binding = binding_of(state->mask, state->drag_item, "region");
      const std::string region = state->drag_radial ? "radial" : "linear";
      state->drag_values.emplace_back(binding, region);
      actions->request_parameter(binding, region, false);
    }
    write("gradient_x", params.x);
    write("gradient_y", params.y);
    if (!state->drag_radial) write("gradient_angle", params.angle);
    write("gradient_size", params.size);
    if (state->drag_radial) write("gradient_ratio", params.ratio);
    if (phase == 2) {
      actions->request_parameters(state->drag_values);
      state->drag_item = -1;
      state->drag_new = false;
      if (const auto run = std::weak_ptr<std::function<void()>>(rebuild).lock()) {
        state->signature = signature();
        (*run)();
      }
    }
  };
  hooks->cursor_diameter = [=]() -> double {
    // 画笔设置显示着且“在画布上拖动”打开：光标圈是笔的大小（硬边直径，图像像素按显示比例换算）。
    if (!view || !state->draw || !state->brush_on || state->mask < 0 || view->image().isNull()) return 0.0;
    const QRectF shown = view->imageRect();
    return shown.isEmpty() ? 0.0 : state->brush_size * shown.width() / view->image().width();
  };
  // 画笔快捷键：K 选画笔并打开“在画布上拖动”；其余调当前画笔设置的滑杆（每次 10），只在画笔设置显示时起作用。
  bindShortcut(modules, "brush-tool", [=] {
    if (state->mask < 0) return;
    for (auto* box : body->findChildren<QComboBox*>(QStringLiteral("refine-local-item-region"))) {
      const int index = box->findData(QStringLiteral("brush"));
      if (index >= 0) box->setCurrentIndex(index);
    }
    for (auto* button : body->findChildren<QPushButton*>(QStringLiteral("refine-local-draw"))) button->setChecked(true);
  });
  const auto nudge = [=](const char* slider_name, int delta) {
    if (!state->brush_on) return;
    for (auto* slider : body->findChildren<QSlider*>(QString::fromLatin1(slider_name))) {
      slider->setValue(std::clamp(slider->value() + delta, slider->minimum(), slider->maximum()));
    }
  };
  bindShortcut(modules, "brush-size-down", [=] { nudge("refine-local-brush-size", -10); });
  bindShortcut(modules, "brush-size-up", [=] { nudge("refine-local-brush-size", 10); });
  bindShortcut(modules, "brush-feather-down", [=] { nudge("refine-local-brush-feather", -10); });
  bindShortcut(modules, "brush-feather-up", [=] { nudge("refine-local-brush-feather", 10); });
  bindShortcut(modules, "brush-flow-down", [=] { nudge("refine-local-brush-flow", -10); });
  bindShortcut(modules, "brush-flow-up", [=] { nudge("refine-local-brush-flow", 10); });
  bindShortcut(modules, "brush-density-down", [=] { nudge("refine-local-brush-density", -10); });
  bindShortcut(modules, "brush-density-up", [=] { nudge("refine-local-brush-density", 10); });
  hooks->paint = [=](QPainter& painter, const QRectF& rect) {
    if (state->drag_brush && !state->stroke_points.empty() && view && !view->image().isNull()) {
      // 正在画的一笔：按笔的大小（图像像素）换算成显示像素，半透明红色，圆头。
      const double scale = rect.width() / view->image().width();
      QPen pen(QColor(235, 64, 52, 150));
      pen.setWidthF(std::max(1.0, state->brush_size * scale));
      pen.setCapStyle(Qt::RoundCap);
      pen.setJoinStyle(Qt::RoundJoin);
      painter.setPen(pen);
      QPolygonF line;
      for (const auto& point : state->stroke_points) {
        line << QPointF(rect.left() + point.first / 10000.0 * rect.width(), rect.top() + point.second / 10000.0 * rect.height());
      }
      if (line.size() == 1) painter.drawPoint(line.first()); else painter.drawPolyline(line);
    }
    if (state->mask < 0 || state->item < 0) return;
    const auto items = items_of(state->mask);
    const auto found = std::find_if(items.begin(), items.end(), [&](const auto& pair) { return pair.first == state->item; });
    if (found == items.end() || (found->second != "linear" && found->second != "radial") || !view) return;
    const auto number = [&](const char* name, int fallback) {
      const auto value = actions->parameter_value(binding_of(state->mask, state->item, name));
      return value ? std::atoi(value->c_str()) : fallback;
    };
    const double x = rect.left() + number("gradient_x", 50) / 100.0 * rect.width();
    const double y = rect.top() + number("gradient_y", 50) / 100.0 * rect.height();
    const double w = view->image().width();
    const double h = view->image().height();
    const double scale = w > 0 ? rect.width() / w : 1.0;  // 图像像素到屏幕
    const double diagonal = std::sqrt(w * w + h * h);
    const double size = number("gradient_size", 50) / 100.0 * diagonal / 2.0;  // 像素：线性半宽、径向横向半径
    painter.setBrush(Qt::NoBrush);
    for (const QColor& color : {QColor(0, 0, 0, 140), QColor(255, 255, 255, 230)}) {
      painter.setPen(QPen(color, color.alpha() > 200 ? 1.5 : 3.0));
      if (found->second == "linear") {
        const double radians = number("gradient_angle", 0) * 3.14159265358979323846 / 180.0;
        const QPointF half(std::cos(radians) * size * scale, std::sin(radians) * size * scale);
        painter.drawLine(QPointF(x, y) - half, QPointF(x, y) + half);
        painter.drawEllipse(QPointF(x, y) - half, 4, 4);
        painter.drawEllipse(QPointF(x, y) + half, 4, 4);
      } else {
        const double rx = size * scale;
        const double ry = rx * number("gradient_ratio", 100) / 100.0;
        const double inner = 1.0 - number("gradient_feather", 50) / 100.0;
        painter.drawEllipse(QPointF(x, y), rx, ry);
        painter.drawEllipse(QPointF(x, y), rx * inner, ry * inner);
        painter.drawEllipse(QPointF(x, y), 3, 3);
      }
    }
  };
  canvas->setHooks(hooks);
  return hooks;
}

// 一个模块的内容（图标栏点开后面板里显示的东西）。
void addModuleContent(const std::string& id, QWidget* content, QVBoxLayout* layout, PageActions* actions,
                      PixCakeImageView* canvas) {
  if (id == "color") {
    addColorPanel(content, layout, actions);
    return;
  }
  if (id == "local") {
    // 局部调色：顶部和色彩调节同一个（固定在滚动区外，见 modulePage），下面是蒙版区和同一套调整分组。
    QFrame* frame = nullptr;
    auto* local = labelPanel(content, layout, "refine-local-labels", moduleDescription("local"), &frame);
    local->setSpacing(0);
    addLocalPanel(frame, local, actions, canvas);
    return;
  }
  if (id == "post") {
    // 后期调色：“全图调整”下是和色彩调节同一套的分组（color.post 登记了白平衡、影调）；“局部调整”要蒙版，还没有。
    QFrame* frame = nullptr;
    auto* post = labelPanel(content, layout, "refine-post-color-labels", moduleDescription("post"), &frame);
    post->setSpacing(0);
    const auto titles = panels::refine_post_color_titles();
    auto* whole = disabledLabel(qs(titles.front()));
    whole->setContentsMargins(12, 12, 12, 8);
    post->addWidget(whole);
    addSections(frame, post, panels::color_section_rows("color.post", panels::refine_post_sections(), "refine-post-"),
                actions, "post");
    auto* partial = disabledLabel(qs(titles[1]));
    partial->setContentsMargins(12, 12, 12, 8);
    post->addWidget(partial);
    return;
  }
  if (id == "portrait") {
    // 人像美化：顶部人物属性（女、男、儿童……所有人，右边联动），下面是十一个折叠分组（ui/panels 的人像表）。
    // 人像效果还没有实现：全部禁用。
    QFrame* frame = nullptr;
    auto* portrait = labelPanel(content, layout, "refine-portrait-labels", moduleDescription("portrait"), &frame);
    portrait->setSpacing(0);
    auto* people = new QWidget(frame);
    people->setObjectName(QStringLiteral("refine-portrait-people"));
    // 两行：女 男 儿童 长辈女 / 长辈男 单人 所有人（宽一些） 联动。
    auto* people_column = new QVBoxLayout(people);
    people_column->setContentsMargins(12, 12, 12, 12);
    people_column->setSpacing(8);
    std::array<QHBoxLayout*, 2> lines{new QHBoxLayout, new QHBoxLayout};
    int index = 0;
    for (const auto& person : panels::refine_portrait_people()) {
      auto* button = new QPushButton(qs(person), people);
      button->setObjectName(QStringLiteral("refine-person"));
      button->setCheckable(true);
      button->setChecked(index == 0);
      button->setEnabled(false);
      lines[index < 4 ? 0 : 1]->addWidget(button, index == 6 ? 2 : 1);
      ++index;
    }
    auto* link = pendingIcon(people, 28, qtext("refine.side.link"));
    link->setEnabled(false);
    lines[1]->addWidget(link);
    for (auto* line : lines) {
      line->setSpacing(8);
      people_column->addLayout(line);
    }
    portrait->addWidget(people);
    auto* rule = new QFrame(frame);
    rule->setObjectName(QStringLiteral("refine-section-rule"));
    rule->setFixedHeight(1);
    portrait->addWidget(rule);
    addSections(frame, portrait, panels::refine_portrait_rows(), actions, "portrait");
    return;
  }
  if (id == "image" || id == "clothing") {
    // 图像美化、衣物美化：和色彩调节、人像同一套折叠分组（ui/panels 的表）。效果还没有实现，全部禁用。
    const bool image = id == "image";
    QFrame* frame = nullptr;
    auto* panel = labelPanel(content, layout, image ? "refine-image-labels" : "refine-clothing-labels",
                             moduleDescription(id.c_str()), &frame);
    panel->setSpacing(0);
    addSections(frame, panel, image ? panels::refine_image_rows() : panels::refine_clothing_rows(), actions, id);
    return;
  }
  layout->setContentsMargins(12, 12, 12, 12);
  layout->setSpacing(8);
  if (id == "crop") {
    addCropPanel(content, labelPanel(content, layout, "refine-crop-labels", moduleDescription("crop")), actions);
  } else if (id == "history") {
    auto* history = labelPanel(content, layout, "refine-history-labels", moduleDescription("history"));
    history->addWidget(disabledLabel(qs(copyItem("refine-history", "title").text)));
    if (actions->has_photo()) {
      // 本次打开以来的步骤，点某一步回到那一步；提交和撤销时刷新。
      auto* steps = new QListWidget(content);
      steps->setObjectName(QStringLiteral("refine-history-list"));
      steps->setAccessibleName(moduleDescription("history"));
      const auto fill = [steps, actions] {
        const QSignalBlocker block(steps);
        steps->clear();
        for (const auto& row : actions->history()) {
          steps->addItem(row.text);
          if (row.current) steps->setCurrentRow(steps->count() - 1);
        }
      };
      fill();
      actions->watch_history(steps, fill);
      QObject::connect(steps, &QListWidget::itemClicked, steps,
                       [steps, actions](QListWidgetItem*) { actions->request_history_step(static_cast<std::size_t>(steps->currentRow())); });
      history->addWidget(steps);
    } else {
      auto* history_empty = text(qs(copyItem("refine-history", "empty").text), true);
      history_empty->setEnabled(false);
      history_empty->setAccessibleName(qtext("refine.history.empty.name"));
      history->addWidget(history_empty);
    }
  } else {
    // AI色彩、联机：还没有接入。
    auto* pending = text(qtext("refine.module.pending"), true);
    pending->setEnabled(false);
    layout->addWidget(pending);
  }
}

// 控件显示或藏起来时回调（模块页切换时，蒙版列表浮窗跟着显示、藏起来）。
class ShowHideHook final : public QObject {
 public:
  ShowHideHook(QObject* parent, std::function<void()> run) : QObject(parent), run_(std::move(run)) {}

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    // 模块切换只让页面相对父控件显示/藏起来（ShowToParent/HideToParent），窗口本身没显示时收不到 Show/Hide。
    const auto type = event->type();
    if (type == QEvent::ShowToParent || type == QEvent::HideToParent || type == QEvent::Show || type == QEvent::Hide) run_();
    return QObject::eventFilter(watched, event);
  }

 private:
  std::function<void()> run_;
};

// 一个模块的面板页：顶部标题（色彩调节页右侧还有“大师级色彩”），下面是可滚动的内容。
QWidget* modulePage(const CopyItem& entry, PageActions* actions, std::vector<PixCakeHistogram*>* histograms,
                    PixCakeImageView* canvas) {
  auto* page = new QWidget;
  page->setObjectName(QStringLiteral("refine-module-page"));
  page->setProperty("module", qs(entry.id));
  auto* column = new QVBoxLayout(page);
  column->setContentsMargins(0, 0, 0, 0);
  column->setSpacing(0);
  auto* header = new QFrame(page);
  header->setObjectName(QStringLiteral("refine-module-header"));
  header->setFixedHeight(PixCakeTheme::kToolbarHeight - 12);
  auto* header_row = new QHBoxLayout(header);
  header_row->setContentsMargins(16, 0, 12, 0);
  QToolButton* local_toggle = nullptr;
  QString title = moduleDescription(entry.id.c_str());
  if (title.isEmpty()) title = qs(entry.text);
  auto* name = text(title);
  name->setObjectName(QStringLiteral("refine-module-title"));
  header_row->addWidget(name);
  header_row->addStretch(1);
  if (entry.id == "color") {
    auto* master = action(qtext("refine.master-color"), header, false);
    master->setObjectName(QStringLiteral("refine-master-color"));
    header_row->addWidget(master);
  } else if (entry.id == "image") {
    auto* master = action(qtext("refine.master-image"), header, false);
    master->setObjectName(QStringLiteral("refine-master-color"));
    header_row->addWidget(master);
  } else if (entry.id == "portrait") {
    auto* scan = pendingIcon(header, 28, qtext("refine.portrait.scan"));
    scan->setToolTip(qtext("refine.portrait.scan"));
    scan->setEnabled(false);
    header_row->addWidget(scan);
  } else if (entry.id == "local") {
    // 蒙版列表浮窗的开关（官方是勾选型按钮，浮窗开着时勾上；图标文件还没有，用灰块）。
    local_toggle = new QToolButton(header);
    local_toggle->setObjectName(QStringLiteral("refine-local-mask-toggle"));
    local_toggle->setAccessibleName(qtext("local.mask.list"));
    local_toggle->setToolTip(qtext("local.mask.list"));
    local_toggle->setCheckable(true);
    local_toggle->setChecked(uiState().mask_list);
    local_toggle->setEnabled(canvas != nullptr);
    local_toggle->setFixedSize(24, 24);  // 灰块由样式表画，勾上时描黄边
    header_row->addWidget(local_toggle);
  }
  column->addWidget(header);
  // 色彩调节、局部调色：直方图卡片和“AI 校准”“多图色彩统一”固定在标题下面，只有下面的内容滚动（官方 9.3.0）。
  if (entry.id == "color" || entry.id == "local") addColorPageTop(page, column, actions, canvas, histograms);
  auto* scroll = new QScrollArea(page);
  scroll->setObjectName(QStringLiteral("refine-module-scroll"));
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto* content = new QWidget(scroll);
  content->setObjectName(QStringLiteral("refine-module-content"));
  auto* layout = new QVBoxLayout(content);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  addModuleContent(entry.id, content, layout, actions, canvas);
  // 蒙版列表浮窗：这一页显示着、开关开着才显示；点浮窗的关闭等于关掉开关。
  auto* mask_panel = canvas != nullptr && canvas->parentWidget() != nullptr
                         ? dynamic_cast<PixCakeFloatingPanel*>(canvas->parentWidget()->findChild<QFrame*>(
                               QStringLiteral("refine-mask-panel"), Qt::FindDirectChildrenOnly))
                         : nullptr;
  if (local_toggle != nullptr && mask_panel != nullptr) {
    const auto sync = [page = QPointer<QWidget>(page), panel = QPointer<PixCakeFloatingPanel>(mask_panel)] {
      if (!page || !panel) return;
      const bool shown = page->isVisibleTo(page->window()) && uiState().mask_list;
      panel->setVisible(shown);
      if (shown) panel->refit();
    };
    QObject::connect(local_toggle, &QToolButton::toggled, local_toggle, [sync](bool on) {
      uiState().mask_list = on;
      sync();
    });
    mask_panel->onClosed([toggle = QPointer<QToolButton>(local_toggle)] {
      uiState().mask_list = false;
      if (toggle) toggle->setChecked(false);
    });
    page->installEventFilter(new ShowHideHook(page, sync));
  }
  layout->addStretch(1);
  scroll->setWidget(content);
  column->addWidget(scroll, 1);
  return page;
}

// 点一下就调用的过滤器（工具栏“预设”是 QFrame 拼的，没有 clicked 信号）。
class ClickFilter final : public QObject {
 public:
  ClickFilter(QObject* parent, std::function<void()> run) : QObject(parent), run_(std::move(run)) {}

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::MouseButtonRelease && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
      run_();
      return true;
    }
    return QObject::eventFilter(watched, event);
  }

 private:
  std::function<void()> run_;
};

void repolish(QWidget* widget) {
  widget->style()->unpolish(widget);
  widget->style()->polish(widget);
}

}  // namespace

QWidget* buildRefinePage(const PageContext& context) {
  auto* page = new QWidget;
  page->setObjectName(QStringLiteral("refine-page"));
  auto* outer = new QVBoxLayout(page);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(4);

  auto* top = new QFrame(page);
  top->setObjectName(QStringLiteral("workspace-top"));
  auto* top_layout = new QHBoxLayout(top);
  top_layout->setContentsMargins(8, 6, 8, 6);
  auto* back = iconAction(qtext("refine.back.button"), iconPath("refine-top", "back"), top, true);
  back->setObjectName(QStringLiteral("top-action"));
  back->setAccessibleName(qs(copyItem("refine-top", "back").text));
  PageActions* actions = &context.actions;
  QObject::connect(back, &QPushButton::clicked, back, [actions] { actions->request_page(Page::Home); });
  // 单图撤销 Cmd+Z、重做 Shift+Cmd+Z（需求 §8、§23.1；Qt 的 Ctrl 在 macOS 上就是 Cmd）。
  bindShortcut(page, "undo", [actions] { actions->request_undo(); });
  bindShortcut(page, "redo", [actions] { actions->request_redo(); });
  bindProjectShortcuts(page, actions);
  bindShortcut(page, "virtual-copy", [actions] {
    if (const auto photo = actions->current_photo()) actions->request_virtual_copy({*photo});
  });
  // 复制效果 Cmd+C、粘贴效果 Cmd+V：直接搬运当前效果，不打开勾选框；这两个键不可自定义（需求 §18）。
  bindShortcut(page, "copy-effect", [actions] { actions->request_copy_effect(); });
  bindShortcut(page, "paste-effect", [actions] { actions->request_paste_effect(); });
  top_layout->addWidget(back);
  top_layout->addWidget(itemAction("refine-top", copyItem("refine-top", "search"), top));
  top_layout->addWidget(itemAction("refine-top", copyItem("refine-top", "assistant"), top));
  top_layout->addStretch();
  auto* tabs = new QHBoxLayout;
  addPageTabs(tabs, context);
  top_layout->addLayout(tabs);
  top_layout->addStretch();
  for (const auto& entry : ui::shell::page_list("refine-global")) {
    top_layout->addWidget(toolbarAction("refine-global", entry, top, &context.actions));
  }
  outer->addWidget(top);

  auto* toolbar = new PixCakeToolbar(QStringLiteral("workspace-tools"), page);
  auto* toolbar_layout = toolbar->row();
  toolbar_layout->setSpacing(4);
  QFrame* preset_tool = nullptr;
  QFrame* guides_tool = nullptr;
  for (const auto& entry : ui::shell::page_list("refine-tools")) {
    auto* tool = workspaceTool("refine-tools", entry, toolbar);
    if (entry.id == "preset") preset_tool = tool;
    if (entry.id == "guides") guides_tool = tool;
    toolbar_layout->addWidget(tool);
  }
  toolbar_layout->addStretch();
  if (guides_tool != nullptr) {
    guides_tool->setEnabled(actions->has_photo());
    if (!actions->has_photo()) guides_tool->setAccessibleDescription(qtext("refine.preview"));
  }
  outer->addWidget(toolbar);

  auto* center = new QHBoxLayout;
  center->setSpacing(0);
  auto* canvas = new QFrame(page);
  canvas->setObjectName(QStringLiteral("workspace-canvas"));
  auto* canvas_layout = new QVBoxLayout(canvas);
  PixCakeImageView* image_view = nullptr;
  if (context.actions.has_photo()) {
    image_view = new PixCakeImageView(canvas);
    canvas_layout->addWidget(image_view, 1);
  } else {
    auto* preview = text(qtext("refine.preview"), true);
    preview->setAlignment(Qt::AlignCenter);
    canvas_layout->addWidget(preview, 1);
  }
  canvas_layout->addWidget(action(qs(copyItem("refine-canvas", "compare").text), canvas), 0, Qt::AlignRight);
  center->addWidget(canvas, 1);

  // 右侧（官方 9.3.0 的层级）：预设栏 | 模块面板 | 图标栏。图标栏选一个模块，面板只显示这一个模块；色彩调节页里
  // 各分组默认收起。预设栏由工具栏的“预设”开关。
  auto* preset_column = new QFrame(page);
  preset_column->setObjectName(QStringLiteral("refine-preset-column"));
  preset_column->setFixedWidth(220);
  auto* preset_layout = new QVBoxLayout(preset_column);
  preset_layout->setContentsMargins(12, 12, 12, 12);
  if (actions->has_photo()) {
    preset_layout->addWidget(buildPresetList(preset_column, actions));
    // 预设栏占满整列高度，列表不再限高。
    if (auto* list = preset_column->findChild<QListWidget*>(QStringLiteral("refine-preset-list"))) {
      list->setMaximumHeight(QWIDGETSIZE_MAX);
    }
  } else {
    auto* title = text(qtext("preset.title"));
    title->setEnabled(false);
    preset_layout->addWidget(title);
  }
  preset_layout->addStretch(1);
  preset_column->setVisible(uiState().presets);
  center->addWidget(preset_column);

  auto* panel = new QFrame(page);
  panel->setObjectName(QStringLiteral("refine-module-panel"));
  panel->setAccessibleName(qtext("region.refine-modules"));
  panel->setFixedWidth(PixCakeTheme::kWorkspaceSideWidth + 14);
  auto* panel_layout = new QVBoxLayout(panel);
  panel_layout->setContentsMargins(0, 0, 0, 0);
  panel_layout->setSpacing(0);
  auto* stack = new QStackedWidget(panel);
  stack->setObjectName(QStringLiteral("refine-module-stack"));
  panel_layout->addWidget(stack, 1);

  auto* rail = new QFrame(page);
  rail->setObjectName(QStringLiteral("refine-rail"));
  rail->setAccessibleName(qtext("refine.rail"));
  rail->setFixedWidth(52);
  auto* rail_layout = new QVBoxLayout(rail);
  rail_layout->setContentsMargins(6, 8, 6, 8);
  rail_layout->setSpacing(4);
  auto* rail_group = new QButtonGroup(rail);
  rail_group->setExclusive(true);

  std::vector<PixCakeHistogram*> histograms;
  const auto& entries = ui::shell::page_list("refine-modules");
  if (std::none_of(entries.begin(), entries.end(), [](const CopyItem& e) { return e.id == uiState().module; })) {
    uiState().module = "color";
  }
  for (const auto& entry : entries) {
    const int index = stack->addWidget(modulePage(entry, actions, &histograms, image_view));
    auto* button = new QToolButton(rail);
    button->setObjectName(QStringLiteral("refine-rail-") + qs(entry.id));
    button->setProperty("rail", true);
    button->setCheckable(true);
    button->setAccessibleName(qs(entry.text));
    const QString description = moduleDescription(entry.id.c_str());
    if (!description.isEmpty()) button->setAccessibleDescription(description);
    // 图标在 assets/refine-modules/<编号>.svg。没有文件就用 pending-icon，名字只在悬停时出现。
    const QString asset = QStringLiteral("refine-modules/") + qs(entry.id) + QStringLiteral(".svg");
    if (!shellAssetFile(asset).isEmpty()) {
      applyAssetIcon(button, asset, 20);
      button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    }
    button->setCursor(Qt::PointingHandCursor);
    rail_group->addButton(button, index);
    rail_layout->addWidget(button, 0, Qt::AlignHCenter);
    if (entry.id == "local") {
      auto* separator = new QFrame(rail);
      separator->setObjectName(QStringLiteral("refine-rail-separator"));
      rail_layout->addWidget(separator);
    }
    const std::string id = entry.id;
    QObject::connect(button, &QToolButton::toggled, stack, [stack, index, id](bool on) {
      if (!on) return;
      stack->setCurrentIndex(index);
      uiState().module = id;
    });
    if (entry.id == uiState().module) {
      button->setChecked(true);
      stack->setCurrentIndex(index);
    }
  }
  rail_layout->addStretch(1);

  if (image_view != nullptr) {
    QPointer<PixCakeImageView> view(image_view);
    std::vector<QPointer<PixCakeHistogram>> charts(histograms.begin(), histograms.end());
    actions->watch_preview(image_view, [view, charts](const PreviewView& update) {
      if (view) {
        if (!update.image.isNull()) view->setImage(update.image);
        view->setStatus(update.status);
      }
      for (const auto& chart : charts) {
        if (chart) chart->setHistogram(update.histogram);
      }
    });
    image_view->setGuideMenuLabels(qtext("guides.remove"), qtext("guides.remove-all"));
    image_view->onGuideMoved([actions](std::size_t index, double position) {
      actions->request_move_guide(index, position);
    });
    image_view->onGuideRemoved([actions](std::size_t index) { actions->request_remove_guide(index); });
    image_view->onGuidesRemoved([actions] { actions->request_remove_all_guides(); });
    actions->watch_guides(image_view, [view](const std::vector<GuideLine>& guides, bool visible) {
      if (view) view->setGuides(guides, visible);
    });
  }
  // 面板底部：保存新预设和同步到选中图片（⇧⌘S，需求 §18）。
  auto* side_bottom = new QFrame(panel);
  side_bottom->setObjectName(QStringLiteral("refine-side-bottom"));
  auto* bottom_row = new QHBoxLayout(side_bottom);
  bottom_row->setContentsMargins(12, 8, 12, 8);
  bottom_row->setSpacing(8);
  auto* save_preset = action(qtext("preset.save"), side_bottom, actions->has_photo());
  save_preset->setObjectName(QStringLiteral("refine-save-preset"));
  QObject::connect(save_preset, &QPushButton::clicked, save_preset, [save_preset, actions] { openSavePresetDialog(save_preset, actions); });
  auto* sync = action(qtext("sync.title"), side_bottom, actions->has_photo());
  sync->setObjectName(QStringLiteral("refine-sync"));
  QObject::connect(sync, &QPushButton::clicked, sync, [sync, actions] { openSyncDialog(sync, actions); });
  bottom_row->addWidget(save_preset, 1);
  bottom_row->addWidget(sync, 1);
  panel_layout->addWidget(side_bottom);
  center->addWidget(panel);
  center->addWidget(rail);
  bindShortcut(page, "sync-effect", [sync, actions] {
    if (actions->has_photo()) openSyncDialog(sync, actions);
  });

  // 工具栏“预设”开关预设栏；快捷键同。
  const auto toggle_presets = [preset_column = QPointer<QFrame>(preset_column), preset_tool = QPointer<QFrame>(preset_tool)] {
    uiState().presets = !uiState().presets;
    if (preset_column) preset_column->setVisible(uiState().presets);
    if (preset_tool) {
      preset_tool->setProperty("emphasis", uiState().presets);
      repolish(preset_tool);
    }
  };
  if (preset_tool != nullptr) {
    preset_tool->setProperty("emphasis", uiState().presets);
    preset_tool->setCursor(Qt::PointingHandCursor);
    preset_tool->installEventFilter(new ClickFilter(preset_tool, toggle_presets));
  }
  bindShortcut(page, "preset-list", toggle_presets);
  if (auto* tool = dynamic_cast<PixCakeWorkspaceTool*>(guides_tool)) {
    tool->onClicked([actions, tool](Qt::KeyboardModifiers) {
      QMenu menu(tool);
      menu.setSeparatorsCollapsible(false);
      menu.setStyleSheet(QStringLiteral(R"QSS(
        QMenu {
          background: #3C3C3E;
          color: #FFFFFF;
          border: 1px solid #55555A;
          border-radius: 10px;
          padding: 7px 0px;
          font-family: "SF Pro Text", "SF Pro Display", "-apple-system", sans-serif;
          font-size: 13px;
        }
        QMenu::item {
          min-height: 34px;
          padding: 0px 18px;
          margin: 1px 6px;
          border-radius: 6px;
          color: #FFFFFF;
          background: transparent;
        }
        QMenu::item:selected { background: #515154; }
        QMenu::item:disabled { color: #77777D; }
        QMenu::separator {
          height: 1px;
          background: #5A5A60;
          margin: 7px 12px;
        }
      )QSS"));
      const bool can_create = actions->guides_visible();
      const auto add_guide_action = [&menu, actions](const char* label_key, const char* shortcut_id,
                                                      bool vertical, bool enabled) {
        QAction* item = menu.addAction(qtext(label_key) + QStringLiteral("\t") + nativeShortcut(shortcut_id));
        item->setEnabled(enabled);
        QObject::connect(item, &QAction::triggered, &menu, [actions, vertical] {
          actions->request_create_guide(vertical);
        });
      };
      add_guide_action("guides.create-horizontal", "guide-horizontal", false, can_create);
      add_guide_action("guides.create-vertical", "guide-vertical", true, can_create);
      menu.addSeparator();
      QAction* toggle = menu.addAction(qtext(actions->guides_visible() ? "guides.hide" : "guides.show") +
                                       QStringLiteral("\t") + nativeShortcut("guide-toggle"));
      QObject::connect(toggle, &QAction::triggered, &menu, [actions] { actions->request_toggle_guides(); });
      menu.exec(tool->mapToGlobal(QPoint(0, tool->height())));
    });
  }
  bindShortcut(page, "guide-horizontal", [actions] { actions->request_create_guide(false); });
  bindShortcut(page, "guide-vertical", [actions] { actions->request_create_guide(true); });
  bindShortcut(page, "guide-toggle", [actions] { actions->request_toggle_guides(); });
  // 模块快捷键（需求 §8.1）：切到那个模块；还没有的模块（AI 色彩风格）不绑。
  const std::pair<const char*, const char*> modules_keys[] = {
      {"module-color", "color"},     {"module-local", "local"},       {"module-portrait", "portrait"},
      {"module-image", "image"},     {"module-clothing", "clothing"}, {"module-post", "post"},
      {"module-crop", "crop"},       {"module-history", "history"}};
  for (const auto& [shortcut, module] : modules_keys) {
    const QString name = QStringLiteral("refine-rail-") + QString::fromLatin1(module);
    bindShortcut(page, shortcut, [rail = QPointer<QFrame>(rail), name] {
      if (!rail) return;
      if (auto* button = rail->findChild<QToolButton*>(name)) button->setChecked(true);
    });
  }
  outer->addLayout(center, 1);
  outer->addWidget(
      thumbnailStrip(page, false, actions, [actions](std::int64_t photo) { actions->request_open_photo(photo); }));
  return page;
}

}  // namespace pixcake::shell
