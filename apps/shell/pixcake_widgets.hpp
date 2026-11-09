#pragma once

#include <QCoreApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QUrl>
#include <QFileInfo>
#include <QFrame>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QPushButton>
#include <QToolButton>
#include <QLinearGradient>
#include <QStringList>
#include <QSvgRenderer>
#include <QHBoxLayout>
#include <QImage>
#include <QPainterPath>
#include <QVBoxLayout>

#include "pixcake/ui/panels/histogram.hpp"
#include "pixcake/ui/shell/page_copy.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pixcake::shell {

// 页面文字只来自 ui/shell 的文案表（Q-11）。
inline QString copyText(std::string_view key) { return QString::fromStdString(ui::shell::page_text(key)); }

// Shared shell widgets keep page-specific code focused on composition while
// preserving stable names for AX and visual regression checks.
class PixCakeButton final : public QPushButton {
 public:
  explicit PixCakeButton(const QString& label, QWidget* parent = nullptr,
                         const QString& variant = QStringLiteral("default"))
      : QPushButton(label, parent) {
    setAccessibleName(label);
    if (variant != QStringLiteral("default")) setObjectName(variant);
  }
};

class PixCakePanel final : public QFrame {
 public:
  explicit PixCakePanel(const QString& object_name, QWidget* parent = nullptr)
      : QFrame(parent) {
    setObjectName(object_name);
    setAccessibleName(object_name);
  }
};

// A panel that takes a click and dropped files/folders (requirement 5.2:
// "拖动或点击导入图片").  Callbacks are optional; without them it is inert.
class PixCakeDropPanel final : public QFrame {
 public:
  explicit PixCakeDropPanel(const QString& object_name, QWidget* parent = nullptr) : QFrame(parent) {
    setObjectName(object_name);
    setAccessibleName(object_name);
  }
  void onClicked(std::function<void()> callback) {
    clicked_ = std::move(callback);
    setCursor(clicked_ ? Qt::PointingHandCursor : Qt::ArrowCursor);
  }
  void onDropped(std::function<void(const std::vector<std::string>&)> callback) {
    dropped_ = std::move(callback);
    setAcceptDrops(static_cast<bool>(dropped_));
  }

 protected:
  void mouseReleaseEvent(QMouseEvent* event) override {
    if (clicked_ && event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) clicked_();
    QFrame::mouseReleaseEvent(event);
  }
  void dragEnterEvent(QDragEnterEvent* event) override {
    if (dropped_ && event->mimeData()->hasUrls()) event->acceptProposedAction();
  }
  void dropEvent(QDropEvent* event) override {
    std::vector<std::string> paths;
    for (const auto& url : event->mimeData()->urls()) {
      if (url.isLocalFile()) paths.push_back(url.toLocalFile().toStdString());
    }
    if (dropped_ && !paths.empty()) {
      event->acceptProposedAction();
      dropped_(paths);
    }
  }

 private:
  std::function<void()> clicked_;
  std::function<void(const std::vector<std::string>&)> dropped_;
};

class PixCakeToolbar final : public QFrame {
 public:
  explicit PixCakeToolbar(const QString& object_name, QWidget* parent = nullptr)
      : QFrame(parent) {
    setObjectName(object_name);
    setAccessibleName(object_name);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 5, 8, 5);
    layout->setSpacing(6);
  }
  QHBoxLayout* row() { return static_cast<QHBoxLayout*>(layout()); }
};

class PixCakeStatusStrip final : public QFrame {
 public:
  explicit PixCakeStatusStrip(const QString& object_name, QWidget* parent = nullptr)
      : QFrame(parent) {
    setObjectName(object_name);
    setAccessibleName(object_name);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 5, 8, 5);
    layout->setSpacing(6);
  }
  QHBoxLayout* row() { return static_cast<QHBoxLayout*>(layout()); }
};

class PixCakeField final : public QLineEdit {
 public:
  explicit PixCakeField(const QString& accessible_name, QWidget* parent = nullptr)
      : QLineEdit(parent) {
    setAccessibleName(accessible_name);
  }
};

class PixCakeEmptyState final : public QFrame {
 public:
  PixCakeEmptyState(const QString& title, const QString& detail = QString(),
                   QWidget* parent = nullptr)
      : QFrame(parent) {
    setObjectName(QStringLiteral("empty-state"));
    setAccessibleName(title);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(8);
    layout->setAlignment(Qt::AlignCenter);
    auto* heading = new QLabel(title, this);
    heading->setObjectName(QStringLiteral("empty-state-title"));
    heading->setAlignment(Qt::AlignCenter);
    layout->addWidget(heading);
    if (!detail.isEmpty()) {
      auto* caption = new QLabel(detail, this);
      caption->setObjectName(QStringLiteral("empty-state-detail"));
      caption->setProperty("muted", true);
      caption->setAlignment(Qt::AlignCenter);
      layout->addWidget(caption);
    }
  }
};

inline QFrame* horizontalRule(QWidget* parent = nullptr) {
  auto* line = new QFrame(parent);
  line->setObjectName(QStringLiteral("login-divider-line"));
  line->setFrameShape(QFrame::HLine);
  line->setFixedHeight(1);
  return line;
}

inline QLabel* pendingIcon(QWidget* parent = nullptr, int size = 28,
                           const QString& accessible = QString()) {
  auto* icon = new QLabel(parent);
  icon->setObjectName(QStringLiteral("pending-icon"));
  icon->setAccessibleName(accessible.isEmpty() ? copyText("icon.pending") : accessible);
  icon->setFixedSize(size, size);
  return icon;
}

inline QString shellAssetFile(const QString& relative) {
  const auto app_dir = QCoreApplication::applicationDirPath();
  const QStringList candidates = {
      app_dir + QStringLiteral("/../Resources/assets/") + relative,
      app_dir + QStringLiteral("/assets/") + relative,
      app_dir + QStringLiteral("/../../../apps/shell/assets/") + relative};
  for (const auto& path : candidates) {
    if (QFileInfo::exists(path)) return path;
  }
  return {};
}

// 色彩调节页顶部的 RGB 直方图（官方样式）：深色渐变底，三个通道各画一块半透明填充、叠加处相加变亮，再描一条细线。
// 计数来自 ui/panels::rgb_histogram；没有数据时只画底。Qt 控件画不出这种图，用 QPainter 自绘。
class PixCakeHistogram final : public QFrame {
 public:
  explicit PixCakeHistogram(QWidget* parent = nullptr) : QFrame(parent) {
    setObjectName(QStringLiteral("refine-histogram"));
    setAccessibleName(copyText("widget.histogram"));
    setMinimumHeight(96);
  }

  void setHistogram(const ui::panels::RgbHistogram& histogram) {
    histogram_ = histogram;
    update();
  }
  const ui::panels::RgbHistogram& histogram() const { return histogram_; }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QLinearGradient ground(0, 0, 0, height());
    ground.setColorAt(0.0, QColor(0, 0, 0));
    ground.setColorAt(1.0, QColor(36, 39, 46));
    painter.fillRect(rect(), ground);
    if (histogram_.peak == 0) return;
    const qreal span = width();
    const qreal bottom = height();
    const qreal tall = height() - 2;
    const auto path_of = [&](const std::array<std::uint32_t, 256>& bins, bool closed) {
      QPainterPath path;
      path.moveTo(0, bottom);
      for (int i = 0; i < 256; ++i) {
        const qreal y = bottom - tall * bins[static_cast<std::size_t>(i)] / histogram_.peak;
        path.lineTo(span * i / 255.0, y);
      }
      path.lineTo(span, bottom);
      if (closed) path.closeSubpath();
      return path;
    };
    const std::array<std::pair<const std::array<std::uint32_t, 256>*, QColor>, 3> channels = {
        {{&histogram_.red, QColor(220, 60, 60)}, {&histogram_.green, QColor(60, 190, 80)}, {&histogram_.blue, QColor(50, 130, 235)}}};
    painter.setPen(Qt::NoPen);
    painter.setCompositionMode(QPainter::CompositionMode_Plus);
    for (const auto& [bins, color] : channels) {
      QColor fill = color;
      fill.setAlpha(110);
      painter.setBrush(fill);
      painter.drawPath(path_of(*bins, true));
    }
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setBrush(Qt::NoBrush);
    for (const auto& [bins, color] : channels) {
      painter.setPen(QPen(color, 1.0));
      painter.drawPath(path_of(*bins, false));
    }
  }

 private:
  ui::panels::RgbHistogram histogram_;
};

// 可折叠分组（色彩调节页的“白平衡”“影调”等）：标题栏是一个可勾选的 QToolButton（左边三角箭头 + 分组名），右侧
// 可以放分组自己的按钮；点标题展开或收起下面的内容。默认收起。
class PixCakeFoldSection final : public QFrame {
 public:
  PixCakeFoldSection(const QString& title, const QString& object_name, QWidget* parent = nullptr) : QFrame(parent) {
    setObjectName(object_name);
    setProperty("fold-section", true);
    setAccessibleName(title);
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    auto* header = new QFrame(this);
    header->setObjectName(QStringLiteral("refine-section-header"));
    auto* header_row = new QHBoxLayout(header);
    header_row->setContentsMargins(12, 0, 12, 0);
    header_row->setSpacing(8);
    toggle_ = new QToolButton(header);
    toggle_->setObjectName(QStringLiteral("refine-section-toggle"));
    toggle_->setText(title);
    toggle_->setAccessibleName(title);
    toggle_->setCheckable(true);
    toggle_->setIcon(triangle(false));
    toggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toggle_->setIconSize(QSize(8, 8));
    toggle_->setCursor(Qt::PointingHandCursor);
    header_row->addWidget(toggle_);
    header_row->addStretch(1);
    actions_ = new QHBoxLayout;
    actions_->setContentsMargins(0, 0, 0, 0);
    actions_->setSpacing(10);
    header_row->addLayout(actions_);
    column->addWidget(header);
    body_ = new QWidget(this);
    body_->setObjectName(QStringLiteral("refine-section-body"));
    auto* body_layout = new QVBoxLayout(body_);
    body_layout->setContentsMargins(12, 2, 12, 12);
    body_layout->setSpacing(10);
    body_->setVisible(false);
    column->addWidget(body_);
    QObject::connect(toggle_, &QToolButton::toggled, this, [this](bool open) {
      toggle_->setIcon(triangle(open));
      body_->setVisible(open);
      if (on_toggled_) on_toggled_(open);
    });
  }
  QVBoxLayout* body() { return static_cast<QVBoxLayout*>(body_->layout()); }
  QWidget* bodyWidget() { return body_; }
  QHBoxLayout* actions() { return actions_; }
  QToolButton* toggle() { return toggle_; }
  bool expanded() const { return toggle_->isChecked(); }
  void setExpanded(bool open) { toggle_->setChecked(open); }
  void onToggled(std::function<void(bool)> callback) { on_toggled_ = std::move(callback); }

 private:
  // 标题左边的实心小三角（收起朝右、展开朝下）。系统箭头在 macOS 上是折线，和官方不一样，这里自己画。
  static QIcon triangle(bool down) {
    const qreal ratio = 2.0;
    QPixmap pixmap(qRound(8 * ratio), qRound(8 * ratio));
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0xe6, 0xe6, 0xe6));
    QPolygonF shape;
    if (down) {
      shape << QPointF(0.5, 2) << QPointF(7.5, 2) << QPointF(4, 6.5);
    } else {
      shape << QPointF(2, 0.5) << QPointF(6.5, 4) << QPointF(2, 7.5);
    }
    painter.drawPolygon(shape);
    painter.end();
    return QIcon(pixmap);
  }

  QToolButton* toggle_ = nullptr;
  QHBoxLayout* actions_ = nullptr;
  QWidget* body_ = nullptr;
  std::function<void(bool)> on_toggled_;
};

// 画布上的交互叠加（局部调色的渐变手柄）：画在图像上的内容、是否接管鼠标、拖动（阶段 0 按下、1 移动、2 松开；
// 位置是图像上的百分比 0..100）。由精修页的局部面板设置。
struct CanvasHooks {
  std::function<void(QPainter&, const QRectF&)> paint;
  std::function<bool()> accepts;
  std::function<void(int, const QPointF&)> drag;
  std::function<double()> cursor_diameter;  // 画笔光标圈的直径（控件里的点）；<= 0 或没有就不画
};

// The refine canvas: shows the latest preview frame fitted into the widget
// (aspect kept, never enlarged past 1:1 device pixels), or a status line.
class PixCakeImageView final : public QFrame {
 public:
  explicit PixCakeImageView(QWidget* parent = nullptr) : QFrame(parent) {
    setObjectName(QStringLiteral("refine-canvas-image"));
    setAccessibleName(copyText("widget.canvas"));
    setMinimumSize(160, 120);
    setMouseTracking(true);  // 画笔光标圈跟着鼠标
  }

  void setImage(QImage image) {
    image_ = std::move(image);
    status_.clear();
    update();
  }
  void setStatus(QString status) {
    status_ = std::move(status);
    update();
  }
  const QImage& image() const { return image_; }
  void setHooks(std::shared_ptr<CanvasHooks> hooks) {
    hooks_ = std::move(hooks);
    update();
  }
  // 鼠标在画面上时回调指针下像素的颜色（显示的这一帧），移出画面回调空值。色彩调节页的直方图读数用它。
  void onHoverPixel(std::function<void(std::optional<QColor>)> callback) { hover_pixel_ = std::move(callback); }
  // 图像在控件里显示的矩形；没有图像时为空。
  QRectF imageRect() const {
    if (image_.isNull()) return {};
    const qreal ratio = devicePixelRatioF();
    QSizeF size = QSizeF(image_.size()) / ratio;
    size.scale(QSizeF(rect().size()).boundedTo(size), Qt::KeepAspectRatio);
    return QRectF(QPointF((width() - size.width()) / 2, (height() - size.height()) / 2), size);
  }

 protected:
  void mousePressEvent(QMouseEvent* event) override {
    if (takes(event)) {
      dragging_ = true;
      hooks_->drag(0, percent(event->position()));
    } else {
      QFrame::mousePressEvent(event);
    }
  }
  void mouseMoveEvent(QMouseEvent* event) override {
    hover_ = event->position();
    hovering_ = true;
    reportPixel();
    if (dragging_ && hooks_) {
      hooks_->drag(1, percent(event->position()));
      update();
    } else {
      if (hooks_ && hooks_->cursor_diameter && hooks_->cursor_diameter() > 0) update();
      QFrame::mouseMoveEvent(event);
    }
  }
  void leaveEvent(QEvent* event) override {
    hovering_ = false;
    if (hover_pixel_) hover_pixel_(std::nullopt);
    update();
    QFrame::leaveEvent(event);
  }
  void mouseReleaseEvent(QMouseEvent* event) override {
    if (dragging_ && hooks_) {
      dragging_ = false;
      hooks_->drag(2, percent(event->position()));
      update();
    } else {
      QFrame::mouseReleaseEvent(event);
    }
  }
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    if (!image_.isNull()) {
      const QRectF target = imageRect();
      painter.setRenderHint(QPainter::SmoothPixmapTransform);
      painter.drawImage(target, image_);
      if (hooks_ && hooks_->paint) {
        painter.setRenderHint(QPainter::Antialiasing);
        hooks_->paint(painter, target);
      }
      if (hovering_ && hooks_ && hooks_->cursor_diameter && target.contains(hover_)) {
        const double diameter = hooks_->cursor_diameter();
        if (diameter > 0) {
          painter.setRenderHint(QPainter::Antialiasing);
          painter.setBrush(Qt::NoBrush);
          painter.setPen(QPen(QColor(0, 0, 0, 160), 2.0));
          painter.drawEllipse(hover_, diameter / 2 + 0.5, diameter / 2 + 0.5);
          painter.setPen(QPen(QColor(255, 255, 255, 230), 1.0));
          painter.drawEllipse(hover_, diameter / 2, diameter / 2);
        }
      }
    }
    if (!status_.isEmpty()) {
      painter.setPen(palette().color(QPalette::PlaceholderText));
      painter.drawText(rect(), Qt::AlignCenter, status_);
    }
  }

 private:
  void reportPixel() {
    if (!hover_pixel_) return;
    const QRectF target = imageRect();
    if (image_.isNull() || !target.contains(hover_)) {
      hover_pixel_(std::nullopt);
      return;
    }
    const int x = std::clamp(static_cast<int>((hover_.x() - target.left()) / target.width() * image_.width()), 0,
                             image_.width() - 1);
    const int y = std::clamp(static_cast<int>((hover_.y() - target.top()) / target.height() * image_.height()), 0,
                             image_.height() - 1);
    hover_pixel_(image_.pixelColor(x, y));
  }
  bool takes(const QMouseEvent* event) const {
    return hooks_ && hooks_->accepts && hooks_->accepts() && hooks_->drag && event->button() == Qt::LeftButton &&
           imageRect().contains(event->position());
  }
  QPointF percent(const QPointF& at) const {
    const QRectF target = imageRect();
    if (target.isEmpty()) return {};
    return {std::clamp((at.x() - target.left()) / target.width() * 100.0, 0.0, 100.0),
            std::clamp((at.y() - target.top()) / target.height() * 100.0, 0.0, 100.0)};
  }

  QImage image_;
  QString status_;
  std::shared_ptr<CanvasHooks> hooks_;
  bool dragging_ = false;
  QPointF hover_;
  bool hovering_ = false;
  std::function<void(std::optional<QColor>)> hover_pixel_;
};

inline QPixmap loadAssetPixmap(const QString& relative, int size) {
  const auto path = shellAssetFile(relative);
  if (path.isEmpty() || size <= 0) return {};
  QSvgRenderer renderer(path);
  if (!renderer.isValid()) return {};
  const qreal ratio = 2.0;
  QPixmap pixmap(qRound(size * ratio), qRound(size * ratio));
  pixmap.setDevicePixelRatio(ratio);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  renderer.render(&painter, QRectF(0, 0, size, size));
  return pixmap;
}

inline QLabel* assetIcon(const QString& relative, int size, const QString& accessible,
                         QWidget* parent = nullptr) {
  const auto pixmap = loadAssetPixmap(relative, size);
  if (pixmap.isNull()) {
    return pendingIcon(parent, size,
                       accessible.isEmpty() ? QString() : accessible + copyText("icon.pending_suffix"));
  }
  auto* icon = new QLabel(parent);
  icon->setObjectName(QStringLiteral("shell-icon"));
  icon->setAccessibleName(accessible);
  icon->setPixmap(pixmap);
  icon->setFixedSize(size, size);
  return icon;
}

inline void applyAssetIcon(QAbstractButton* button, const QString& relative, int size) {
  const auto pixmap = loadAssetPixmap(relative, size);
  if (pixmap.isNull() || button == nullptr) return;
  QIcon icon;
  icon.addPixmap(pixmap, QIcon::Normal);
  icon.addPixmap(pixmap, QIcon::Disabled);
  button->setIcon(icon);
  button->setIconSize(QSize(size, size));
}

inline QStringList uploadedShellAssets() {
  return {QStringLiteral("pixcake-logo.svg"),
          QStringLiteral("wechat.svg"),
          QStringLiteral("help.svg"),
          QStringLiteral("home-capture.svg"),
          QStringLiteral("home-ai-toolbox.svg"),
          QStringLiteral("home-assistant.svg"),
          QStringLiteral("workspace-tools/zoom-display.svg"),
          QStringLiteral("workspace-tools/hand.svg"),
          QStringLiteral("workspace-tools/edge-remove.svg"),
          QStringLiteral("workspace-tools/guides.svg"),
          QStringLiteral("workspace-tools/liquify.svg"),
          QStringLiteral("workspace-tools/spot-heal.svg"),
          QStringLiteral("workspace-tools/patch.svg"),
          QStringLiteral("workspace-tools/clone-stamp.svg"),
          QStringLiteral("workspace-tools/smart-remove.svg"),
          QStringLiteral("workspace-tools/ai-spot.svg"),
          QStringLiteral("workspace-tools/reset.svg"),
          QStringLiteral("workspace-tools/preset.svg"),
          QStringLiteral("workspace-tools/screen-mode.svg"),
          QStringLiteral("workspace-tools/move-layer.svg"),
          QStringLiteral("workspace-tools/zoom-tool.svg"),
          QStringLiteral("workspace-tools/marquee.svg"),
          QStringLiteral("workspace-tools/lasso.svg"),
          QStringLiteral("workspace-tools/crop.svg"),
          QStringLiteral("workspace-global/search.svg"),
          QStringLiteral("workspace-global/membership.svg"),
          QStringLiteral("workspace-global/support.svg"),
          QStringLiteral("workspace-global/account.svg"),
          QStringLiteral("workspace-global/messages.svg"),
          QStringLiteral("workspace-global/transfer.svg"),
          QStringLiteral("workspace-global/export-list.svg"),
          QStringLiteral("workspace-global/export.svg"),
          QStringLiteral("workspace-global/assistant.svg"),
          QStringLiteral("workspace-global/ai-tools.svg"),
          QStringLiteral("workspace-global/history.svg"),
          QStringLiteral("workspace-global/batch.svg"),
          QStringLiteral("merchant.svg"),
          QStringLiteral("workspace-global/home.svg"),
          QStringLiteral("workspace-global/filter.svg"),
          QStringLiteral("workspace-global/sort.svg"),
          QStringLiteral("workspace-global/star.svg"),
          QStringLiteral("workspace-global/flag.svg"),
          QStringLiteral("workspace-global/color-tag.svg"),
          QStringLiteral("workspace-global/view-grid.svg"),
          QStringLiteral("workspace-global/view-single.svg"),
          QStringLiteral("workspace-global/view-filter.svg"),
          QStringLiteral("workspace-global/view-compare.svg"),
          QStringLiteral("workspace-global/view-reference.svg"),
          QStringLiteral("workspace-global/tab-library.svg"),
          QStringLiteral("workspace-global/tab-refine.svg"),
          QStringLiteral("workspace-global/tab-creative.svg"),
          QStringLiteral("home-nav/home.svg"),
          QStringLiteral("home-nav/ai-toolbox.svg"),
          QStringLiteral("home-nav/local-projects.svg"),
          QStringLiteral("home-nav/cloud.svg"),
          QStringLiteral("home-nav/workflow.svg"),
          QStringLiteral("home-nav/delivery.svg"),
          QStringLiteral("home-nav/trash.svg")};
}

inline PixCakeEmptyState* emptyState(const QString& title, const QString& detail,
                                     QWidget* parent = nullptr) {
  return new PixCakeEmptyState(title, detail, parent);
}

}  // namespace pixcake::shell
