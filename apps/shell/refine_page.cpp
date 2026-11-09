#include "export_dialog.hpp"
#include "pages.hpp"
#include "pixcake_theme.hpp"
#include "pixcake_widgets.hpp"
#include "preset_dialog.hpp"
#include "shell_icons.hpp"
#include "sync_dialog.hpp"

#include "pixcake/capability/capability_registry.hpp"
#include "pixcake/mask/brush_mask.hpp"
#include "pixcake/ui/panels/slider_spec.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QListWidget>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSlider>
#include <QStandardItemModel>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <vector>
#include <memory>
#include <functional>

namespace pixcake::shell {
namespace {

namespace panels = ui::panels;

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

// 范围、步进、默认值只来自内核参数模式（ui/panels 的 slider_spec）。照片打开时，绑定全图色彩的
// 滑杆可用：拖动发预览，松手（或键盘改值）经命令入口提交。
void addDisabledSlider(QWidget* parent, QLayout* layout, const panels::SliderSpec& spec, const QString& object_name,
                       PageActions* actions = nullptr, const std::optional<panels::SliderBinding>& binding = {},
                       const Refresh& refresh = nullptr) {
  auto* row = new QWidget(parent);
  auto* row_layout = new QHBoxLayout(row);
  row_layout->setContentsMargins(0, 0, 0, 0);
  row_layout->setSpacing(6);
  auto* name = text(qs(spec.label));
  name->setEnabled(false);
  auto* slider = new QSlider(Qt::Horizontal, row);
  if (!object_name.isEmpty()) slider->setObjectName(object_name);
  slider->setRange(0, spec.tick_count);
  slider->setSingleStep(1);
  slider->setValue(spec.default_tick);
  slider->setEnabled(false);
  slider->setAccessibleName(qs(spec.accessible_name));
  auto* reading = text(qs(spec.default_text));
  reading->setEnabled(false);
  row_layout->addWidget(name);
  row_layout->addWidget(slider, 1);
  row_layout->addWidget(reading);
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

void addColorPanel(QWidget* modules, QVBoxLayout* modules_layout, PageActions* actions,
                   PixCakeHistogram** histogram_out) {
  auto* layout = labelPanel(modules, modules_layout, "refine-color-labels", moduleDescription("color"));
  auto* histogram = new PixCakeHistogram(modules);
  *histogram_out = histogram;
  histogram->setMinimumHeight(PixCakeTheme::kControlHeight * 2);
  layout->addWidget(histogram);
  auto* readings = new QHBoxLayout;
  readings->setContentsMargins(0, 0, 0, 0);
  readings->setSpacing(6);
  auto* rgb = disabledLabel(QStringLiteral("RGB"), qtext("refine.rgb.name"));
  auto* lab = disabledLabel(QStringLiteral("Lab"), qtext("refine.lab.name"));
  readings->addWidget(rgb);
  readings->addWidget(lab);
  readings->addStretch();
  layout->addLayout(readings);
  const Refresh refresh = std::make_shared<EnableRefresh>();
  for (const auto& row : panels::refine_color_rows()) {
    if (row.kind == panels::PanelRowKind::Title) {
      layout->addWidget(disabledLabel(qs(row.label)));
    } else if (row.kind == panels::PanelRowKind::Choice) {
      addChoice(modules, layout, row, actions, refresh);
    } else {
      addDisabledSlider(modules, layout, panels::slider_spec(row.label, row.binding), qs(row.object_name), actions,
                        row.binding, refresh);
    }
  }
}

void addLabelList(QVBoxLayout* layout, std::string_view list, bool accessible_is_text = false) {
  for (const auto& entry : ui::shell::page_list(list)) {
    const QString label = qs(entry.text);
    const QString accessible = !entry.accessible.empty() ? qs(entry.accessible) : (accessible_is_text ? label : QString());
    layout->addWidget(disabledLabel(label, accessible));
  }
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
  QString signature;
};

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
  layout->addWidget(body);
  if (actions == nullptr || !actions->has_photo()) return nullptr;
  const auto state = std::make_shared<LocalState>();
  const auto rebuild = std::make_shared<std::function<void()>>();
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
    if (std::find(masks.begin(), masks.end(), state->mask) == masks.end()) {
      state->mask = masks.empty() ? -1 : masks.front();
      state->item = -1;
    }
    const auto region_label = [](const std::string& id) {
      for (const auto& option : panels::choice_options({"color.local", "region"})) {
        if (option.id == id) return qs(option.label);
      }
      return qs(id);
    };

    // 蒙版列表。
    auto* masks_list = new QListWidget(body);
    masks_list->setObjectName(QStringLiteral("refine-local-masks"));
    masks_list->setAccessibleName(qtext("local.mask.new"));
    masks_list->setMaximumHeight(110);
    for (const int mask : masks) {
      // 名称是蒙版自己的文本参数；没有就显示“自定义-N”。双击改名。
      const auto name = actions->parameter_value(binding_of(mask, 0, "name"));
      auto* row = new QListWidgetItem(name ? qs(*name) : qtext("local.mask.name").arg(mask), masks_list);
      row->setData(Qt::UserRole, mask);
      row->setFlags(row->flags() | Qt::ItemIsEditable);
      if (mask == state->mask) masks_list->setCurrentItem(row);
    }
    body_layout->addWidget(masks_list);
    auto* buttons = new QWidget(body);
    auto* buttons_layout = new QHBoxLayout(buttons);
    buttons_layout->setContentsMargins(0, 0, 0, 0);
    auto* create = action(qtext("local.mask.new"), buttons, true);
    create->setObjectName(QStringLiteral("refine-local-new"));
    auto* remove = action(qtext("local.mask.remove"), buttons, state->mask >= 0);
    remove->setObjectName(QStringLiteral("refine-local-delete"));
    buttons_layout->addWidget(create);
    buttons_layout->addWidget(remove);
    body_layout->addWidget(buttons);
    const auto again = [state, weak, signature] {
      state->signature = signature();
      if (const auto run = weak.lock()) (*run)();
    };
    QObject::connect(masks_list, &QListWidget::itemChanged, masks_list, [=](QListWidgetItem* row) {
      const int mask = row->data(Qt::UserRole).toInt();
      const std::string text = row->text().trimmed().toStdString();
      // 不合格的名称（空、超过 20 个字符）还原成现在的名字；合格的提交成一步历史。
      if (!text.empty() && validate_parameter_display_value("color.local", "m1:name", text)) {
        if (actions->parameter_value(binding_of(mask, 0, "name")) != std::optional<std::string>(text)) {
          actions->request_parameter(binding_of(mask, 0, "name"), text, true);
        }
      }
      again();
    });
    QObject::connect(masks_list, &QListWidget::itemClicked, masks_list, [state, again](QListWidgetItem* row) {
      state->mask = row->data(Qt::UserRole).toInt();
      state->item = -1;
      again();
    });
    QObject::connect(create, &QPushButton::clicked, create, [=] {
      const auto existing = actions->local_masks();
      int fresh = 1;
      while (fresh <= kMaxLocalInstances && std::find(existing.begin(), existing.end(), fresh) != existing.end()) ++fresh;
      if (fresh > kMaxLocalInstances) return;
      actions->request_parameter(binding_of(fresh, 0, "region"), "all", true);
      state->mask = fresh;
      state->item = -1;
      again();
    });
    QObject::connect(remove, &QPushButton::clicked, remove, [=] {
      if (state->mask < 0) return;
      actions->request_remove_local_mask(state->mask);
      state->mask = -1;
      state->item = -1;
      again();
    });
    if (state->mask < 0) {
      body_layout->addWidget(disabledLabel(qtext("local.mask.none")));
      state->signature = signature();
      return;
    }

    // 选中蒙版的子项。
    const auto items = items_of(state->mask);
    if (std::none_of(items.begin(), items.end(), [&](const auto& pair) { return pair.first == state->item; })) {
      state->item = items.empty() ? -1 : items.front().first;
    }
    auto* items_list = new QListWidget(body);
    items_list->setObjectName(QStringLiteral("refine-local-items"));
    items_list->setAccessibleName(qtext("local.item.add"));
    items_list->setMaximumHeight(110);
    for (const auto& [item, region] : items) {
      const auto how = item == 0 ? std::optional<std::string>("add")
                                 : actions->parameter_value(binding_of(state->mask, item, "mode"));
      const bool inverted = actions->parameter_value(binding_of(state->mask, item, "invert")) == std::optional<std::string>("on");
      auto* row = new QListWidgetItem(
          qtext("local.item.text").arg(region_label(region),
                                       qtext(how == std::optional<std::string>("subtract") ? "local.item.mode.subtract"
                                                                                           : "local.item.mode.add")) +
              (inverted ? qtext("local.item.text.invert") : QString()),
          items_list);
      row->setData(Qt::UserRole, item);
      if (item == state->item) items_list->setCurrentItem(row);
    }
    body_layout->addWidget(items_list);
    QObject::connect(items_list, &QListWidget::itemClicked, items_list, [state, again](QListWidgetItem* row) {
      state->item = row->data(Qt::UserRole).toInt();
      again();
    });
    auto* add_row = new QWidget(body);
    auto* add_layout = new QHBoxLayout(add_row);
    add_layout->setContentsMargins(0, 0, 0, 0);
    auto* region_box = new QComboBox(add_row);
    region_box->setObjectName(QStringLiteral("refine-local-item-region"));
    region_box->setAccessibleName(qtext("local.item.add"));
    for (const auto& option : panels::choice_options({"color.local", "region"})) {
      if (option.id != "all") region_box->addItem(qs(option.label), qs(option.id));
    }
    auto* mode_box = new QComboBox(add_row);
    mode_box->setObjectName(QStringLiteral("refine-local-item-mode"));
    mode_box->addItem(qtext("local.item.mode.add"), QStringLiteral("add"));
    mode_box->addItem(qtext("local.item.mode.subtract"), QStringLiteral("subtract"));
    add_layout->addWidget(region_box, 1);
    add_layout->addWidget(mode_box);
    body_layout->addWidget(add_row);
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
    body_layout->addWidget(item_buttons);
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
      body_layout->addWidget(invert);
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
    const auto own_parameter = [](const std::string& id) {
      return id.rfind("gradient_", 0) == 0 || id.rfind("range_", 0) == 0 || id.rfind("lum_", 0) == 0;
    };
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
      body_layout->addWidget(brush_box);
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
        addDisabledSlider(modules, body_layout, panels::slider_spec(row.label, bound),
                          qs("refine-local-item-" + row.binding->parameter_id), actions, bound, refresh);
      }
    }
    // 整个蒙版的十四支调整滑杆。
    for (const auto& row : rows) {
      if (row.kind == panels::PanelRowKind::Choice || (row.binding && own_parameter(row.binding->parameter_id))) continue;
      if (row.kind == panels::PanelRowKind::Title) {
        body_layout->addWidget(disabledLabel(qs(row.label)));
      } else {
        const auto bound = binding_of(state->mask, 0, row.binding->parameter_id);
        addDisabledSlider(modules, body_layout, panels::slider_spec(row.label, bound), qs(row.object_name), actions, bound,
                          refresh);
      }
    }
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

void addModules(QWidget* modules, QVBoxLayout* modules_layout, PageActions* actions, PixCakeHistogram** histogram,
                PixCakeImageView* canvas) {
  for (const auto& entry : ui::shell::page_list("refine-modules")) {
    auto* button = action(qs(entry.text), modules);
    const QString description = moduleDescription(entry.id.c_str());
    if (!description.isEmpty()) button->setAccessibleDescription(description);
    modules_layout->addWidget(button);
  }
  if (actions->has_photo()) modules_layout->addWidget(buildPresetList(modules, actions));
  addColorPanel(modules, modules_layout, actions, histogram);

  auto* local = labelPanel(modules, modules_layout, "refine-local-labels", moduleDescription("local"));
  addLabelList(local, "refine-local");
  addLocalPanel(modules, local, actions, canvas);
  const char* feather = panels::refine_local_feather_label();
  addDisabledSlider(modules, local, panels::slider_spec(feather, panels::refine_local_binding(feather)), QString());

  auto* post = labelPanel(modules, modules_layout, "refine-post-color-labels", moduleDescription("post"));
  // 后期调色：全图调整是十四支白平衡、影调滑杆（照片打开时可调）；局部调整要蒙版，还没有。
  {
    const Refresh refresh = std::make_shared<EnableRefresh>();
    const auto titles = panels::refine_post_color_titles();
    post->addWidget(disabledLabel(qs(titles.front())));
    for (const auto& row : panels::refine_post_color_rows()) {
      if (row.kind == panels::PanelRowKind::Title) {
        post->addWidget(disabledLabel(qs(row.label)));
      } else {
        addDisabledSlider(modules, post, panels::slider_spec(row.label, row.binding), qs(row.object_name), actions,
                          row.binding, refresh);
      }
    }
    post->addWidget(disabledLabel(qs(titles[1])));
  }

  addLabelList(labelPanel(modules, modules_layout, "refine-clothing-labels", qtext("refine.clothing.name")),
               "refine-clothing");
  addLabelList(labelPanel(modules, modules_layout, "refine-portrait-labels", moduleDescription("portrait")),
               "refine-portrait", true);
  addLabelList(labelPanel(modules, modules_layout, "refine-image-labels", moduleDescription("image")), "refine-image",
               true);
  addLabelList(labelPanel(modules, modules_layout, "refine-pet-labels", qs(copyItem("refine-modules", "pet").text)),
               "refine-pet", true);

  auto* crop = labelPanel(modules, modules_layout, "refine-crop-labels", moduleDescription("crop"));
  addCropPanel(modules, crop, actions);

  auto* history = labelPanel(modules, modules_layout, "refine-history-labels", moduleDescription("history"));
  history->addWidget(disabledLabel(qs(copyItem("refine-history", "title").text)));
  if (actions->has_photo()) {
    // 本次打开以来的步骤，点某一步回到那一步；提交和撤销时刷新。
    auto* steps = new QListWidget(modules);
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
  for (const auto& entry : ui::shell::page_list("refine-tools")) {
    toolbar_layout->addWidget(workspaceTool("refine-tools", entry, toolbar));
  }
  toolbar_layout->addStretch();
  outer->addWidget(toolbar);

  auto* center = new QHBoxLayout;
  center->setSpacing(4);
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

  auto* modules = new QFrame(page);
  modules->setObjectName(QStringLiteral("workspace-side"));
  modules->setMinimumWidth(PixCakeTheme::kWorkspaceSideWidth);
  modules->setAccessibleName(qtext("region.refine-modules"));
  auto* modules_layout = new QVBoxLayout(modules);
  modules_layout->setContentsMargins(14, 12, 14, 12);
  modules_layout->addWidget(text(qtext("region.refine-modules")));
  PixCakeHistogram* histogram = nullptr;
  addModules(modules, modules_layout, actions, &histogram, image_view);
  if (image_view != nullptr) {
    QPointer<PixCakeImageView> view(image_view);
    QPointer<PixCakeHistogram> chart(histogram);
    actions->watch_preview(image_view, [view, chart](const PreviewView& update) {
      if (view) {
        if (!update.image.isNull()) view->setImage(update.image);
        view->setStatus(update.status);
      }
      if (chart) chart->setHistogram(update.histogram);
    });
  }
  modules_layout->addStretch();
  // The module panels are taller than the window: scroll them instead of
  // squeezing every row (the colour panel's histogram and sliders included).
  auto* side_scroll = new QScrollArea(page);
  side_scroll->setObjectName(QStringLiteral("workspace-side-scroll"));
  side_scroll->setWidget(modules);
  side_scroll->setWidgetResizable(true);
  side_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  side_scroll->setMinimumWidth(PixCakeTheme::kWorkspaceSideWidth + side_scroll->verticalScrollBar()->sizeHint().width());
  // 功能区底部：保存新预设（预设还没接）和同步到选中图片（⇧⌘S，需求 §18）。
  auto* side_column = new QVBoxLayout;
  side_column->setSpacing(4);
  side_column->addWidget(side_scroll, 1);
  auto* side_bottom = new QHBoxLayout;
  auto* sync = action(qtext("sync.title"), page, actions->has_photo());
  sync->setObjectName(QStringLiteral("refine-sync"));
  QObject::connect(sync, &QPushButton::clicked, sync, [sync, actions] { openSyncDialog(sync, actions); });
  auto* save_preset = action(qtext("preset.save"), page, actions->has_photo());
  save_preset->setObjectName(QStringLiteral("refine-save-preset"));
  QObject::connect(save_preset, &QPushButton::clicked, save_preset, [save_preset, actions] { openSavePresetDialog(save_preset, actions); });
  side_bottom->addStretch();
  side_bottom->addWidget(save_preset);
  side_bottom->addWidget(sync);
  side_column->addLayout(side_bottom);
  center->addLayout(side_column);
  bindShortcut(page, "sync-effect", [sync, actions] {
    if (actions->has_photo()) openSyncDialog(sync, actions);
  });
  // 模块快捷键（需求 §8.1）：把功能区滚到那个模块；还没有的模块（AI 色彩风格）不绑。
  const std::pair<const char*, const char*> modules_keys[] = {
      {"module-color", "refine-color-labels"},         {"module-local", "refine-local-labels"},
      {"module-portrait", "refine-portrait-labels"},   {"module-image", "refine-image-labels"},
      {"module-clothing", "refine-clothing-labels"},   {"module-post", "refine-post-color-labels"},
      {"module-crop", "refine-crop-labels"},           {"module-history", "refine-history-labels"},
      {"preset-list", "refine-preset-labels"}};
  for (const auto& [id, panel_name] : modules_keys) {
    const QString name = QString::fromLatin1(panel_name);
    bindShortcut(page, id, [side_scroll = QPointer<QScrollArea>(side_scroll), name] {
      if (!side_scroll) return;
      if (auto* panel = side_scroll->widget()->findChild<QWidget*>(name)) {
        side_scroll->verticalScrollBar()->setValue(panel->mapTo(side_scroll->widget(), QPoint(0, 0)).y() - 8);
      }
    });
  }
  if (histogram != nullptr && context.actions.has_photo()) {
    // With a photo open, start at the colour panel (histogram and sliders).
    QPointer<QScrollArea> scroll(side_scroll);
    QPointer<PixCakeHistogram> chart(histogram);
    QTimer::singleShot(0, side_scroll, [scroll, chart] {
      if (scroll && chart) scroll->verticalScrollBar()->setValue(chart->mapTo(scroll->widget(), QPoint(0, 0)).y() - 40);
    });
  }
  outer->addLayout(center, 1);
  outer->addWidget(
      thumbnailStrip(page, false, actions, [actions](std::int64_t photo) { actions->request_open_photo(photo); }));
  return page;
}

}  // namespace pixcake::shell
