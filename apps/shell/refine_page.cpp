#include "export_dialog.hpp"
#include "pages.hpp"
#include "pixcake_theme.hpp"
#include "pixcake_widgets.hpp"
#include "shell_icons.hpp"

#include "pixcake/ui/panels/slider_spec.hpp"

#include <QComboBox>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
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

void addModules(QWidget* modules, QVBoxLayout* modules_layout, PageActions* actions,
                PixCakeHistogram** histogram) {
  for (const auto& entry : ui::shell::page_list("refine-modules")) {
    auto* button = action(qs(entry.text), modules);
    const QString description = moduleDescription(entry.id.c_str());
    if (!description.isEmpty()) button->setAccessibleDescription(description);
    modules_layout->addWidget(button);
  }
  addColorPanel(modules, modules_layout, actions, histogram);

  auto* local = labelPanel(modules, modules_layout, "refine-local-labels", moduleDescription("local"));
  addLabelList(local, "refine-local");
  const char* feather = panels::refine_local_feather_label();
  addDisabledSlider(modules, local, panels::slider_spec(feather, panels::refine_local_binding(feather)), QString());

  auto* post = labelPanel(modules, modules_layout, "refine-post-color-labels", moduleDescription("post"));
  for (const auto& title : panels::refine_post_color_titles()) post->addWidget(disabledLabel(qs(title)));

  addLabelList(labelPanel(modules, modules_layout, "refine-clothing-labels", qtext("refine.clothing.name")),
               "refine-clothing");
  addLabelList(labelPanel(modules, modules_layout, "refine-portrait-labels", moduleDescription("portrait")),
               "refine-portrait", true);
  addLabelList(labelPanel(modules, modules_layout, "refine-image-labels", moduleDescription("image")), "refine-image",
               true);
  addLabelList(labelPanel(modules, modules_layout, "refine-pet-labels", qs(copyItem("refine-modules", "pet").text)),
               "refine-pet", true);

  auto* crop = labelPanel(modules, modules_layout, "refine-crop-labels", moduleDescription("crop"));
  for (const auto& entry : ui::shell::page_list("refine-crop")) {
    QString accessible;
    if (entry.id == "angle") {
      accessible = qs(panels::slider_spec(entry.text, panels::refine_crop_angle_binding()).accessible_name);
    }
    crop->addWidget(disabledLabel(qs(entry.text), accessible));
  }

  auto* history = labelPanel(modules, modules_layout, "refine-history-labels", moduleDescription("history"));
  history->addWidget(disabledLabel(qs(copyItem("refine-history", "title").text)));
  auto* history_empty = text(qs(copyItem("refine-history", "empty").text), true);
  history_empty->setEnabled(false);
  history_empty->setAccessibleName(qtext("refine.history.empty.name"));
  history->addWidget(history_empty);
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
  addModules(modules, modules_layout, actions, &histogram);
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
  center->addWidget(side_scroll);
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
