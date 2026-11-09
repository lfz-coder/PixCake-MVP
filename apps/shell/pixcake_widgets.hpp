#pragma once

#include <QCoreApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QMenu>
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
#include <QTreeWidget>
#include <QResizeEvent>
#include <QLinearGradient>
#include <QStringList>
#include <QSvgRenderer>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QPainterPath>
#include <QVBoxLayout>

#include "pixcake/ui/panels/histogram.hpp"
#include "pixcake/ui/shell/page_copy.hpp"
#include "guide_line.hpp"

#include <algorithm>
#include <cmath>
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

// Toolbar tools are frames to keep the icon-only layout. A connected tool
// receives clicks on the whole tile, including its icon.
class PixCakeWorkspaceTool final : public QFrame {
 public:
  explicit PixCakeWorkspaceTool(QWidget* parent = nullptr) : QFrame(parent) {}

  void onClicked(std::function<void(Qt::KeyboardModifiers)> callback) {
    clicked_ = std::move(callback);
    setCursor(clicked_ ? Qt::PointingHandCursor : Qt::ArrowCursor);
  }

 protected:
  void mousePressEvent(QMouseEvent* event) override {
    if (clicked_ && event->button() == Qt::LeftButton) {
      event->accept();
      return;
    }
    QFrame::mousePressEvent(event);
  }
  void mouseReleaseEvent(QMouseEvent* event) override {
    if (clicked_ && event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) {
      clicked_(event->modifiers());
      event->accept();
      return;
    }
    QFrame::mouseReleaseEvent(event);
  }

 private:
  std::function<void(Qt::KeyboardModifiers)> clicked_;
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

// 色彩调节页顶部的 RGB 直方图（官方样式）：深色渐变底，红、绿、蓝三个通道依次画一块 31% 不透明的填充（普通叠放，
// 三者重叠处是官方那种钢蓝色），再各描一条 1px 线。高度是线性的：满高 = 统计像素数 / 40，超出的峰顶截掉
// （ui/panels::histogram_bar_height，实测见 docs/superpowers/evidence/2026-11-09-refine-histogram-scale.md）。
// 没有数据时只画底。Qt 控件画不出这种图，用 QPainter 自绘。
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
    ground.setColorAt(1.0, QColor(31, 34, 39));
    painter.fillRect(rect(), ground);
    if (histogram_.peak == 0) return;
    const qreal span = width();
    const qreal bottom = height();
    const qreal tall = height() - 2;
    const auto path_of = [&](const std::array<std::uint32_t, 256>& bins, bool closed) {
      QPainterPath path;
      path.moveTo(0, bottom);
      for (int i = 0; i < 256; ++i) {
        const qreal y = bottom - tall * ui::panels::histogram_bar_height(histogram_, bins[static_cast<std::size_t>(i)]);
        path.lineTo(span * i / 255.0, y);
      }
      path.lineTo(span, bottom);
      if (closed) path.closeSubpath();
      return path;
    };
    // 线色取自官方截图里最饱和的线像素。
    const std::array<std::pair<const std::array<std::uint32_t, 256>*, QColor>, 3> channels = {
        {{&histogram_.red, QColor(205, 20, 38)}, {&histogram_.green, QColor(0, 190, 73)}, {&histogram_.blue, QColor(0, 90, 255)}}};
    painter.setPen(Qt::NoPen);
    for (const auto& [bins, color] : channels) {
      QColor fill = color;
      fill.setAlpha(79);  // 31%
      painter.setBrush(fill);
      painter.drawPath(path_of(*bins, true));
    }
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
    title_decor_ = new QHBoxLayout;
    title_decor_->setContentsMargins(0, 0, 0, 0);
    title_decor_->setSpacing(6);
    header_row->addLayout(title_decor_);
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
  // 标题右边紧挨着的修饰（问号、New 角标）。
  QHBoxLayout* titleDecor() { return title_decor_; }
  QToolButton* toggle() { return toggle_; }
  bool expanded() const { return toggle_->isChecked(); }
  void setExpanded(bool open) { toggle_->setChecked(open); }
  void onToggled(std::function<void(bool)> callback) { on_toggled_ = std::move(callback); }

 public:
  // 标题左边的实心小三角（收起朝右、展开朝下）。系统箭头在 macOS 上是折线，和官方不一样，这里自己画。
  // 可展开的滑杆行（▸）也用它。
  static QIcon triangleIcon(bool down) { return triangle(down); }

 private:
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
  QHBoxLayout* title_decor_ = nullptr;
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

// 画布上浮出的小面板（官方 9.3.0 局部调色的“蒙版列表”）：顶部居中一条拖动手柄，标题和关闭按钮，一条分隔线，下面
// 是内容。默认贴在父控件（画布区）右上角；按住标题栏可以拖到别处，父控件变大小时留在里面。
class PixCakeFloatingPanel final : public QFrame {
 public:
  PixCakeFloatingPanel(const QString& title, QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("floating-panel"));
    setProperty("floating-panel", true);  // 换了对象名也按这个属性上样式
    setAttribute(Qt::WA_StyledBackground);
    setAccessibleName(title);
    setFixedWidth(200);
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(1, 1, 1, 1);
    column->setSpacing(0);
    header_ = new QWidget(this);
    header_->setObjectName(QStringLiteral("floating-panel-header"));
    auto* header = new QVBoxLayout(header_);
    header->setContentsMargins(16, 5, 10, 8);
    header->setSpacing(4);
    auto* handle = new QFrame(header_);
    handle->setObjectName(QStringLiteral("floating-panel-handle"));
    handle->setFixedSize(24, 4);
    header->addWidget(handle, 0, Qt::AlignHCenter);
    auto* title_row = new QHBoxLayout;
    title_row->setContentsMargins(0, 0, 0, 0);
    auto* name = new QLabel(title, header_);
    name->setObjectName(QStringLiteral("floating-panel-title"));
    title_row->addWidget(name, 1);
    auto* close = new QToolButton(header_);
    close->setObjectName(QStringLiteral("floating-panel-close"));
    close->setAccessibleName(copyText("widget.close"));
    close->setIcon(closeIcon());
    close->setIconSize(QSize(14, 14));
    close->setAutoRaise(true);
    title_row->addWidget(close);
    header->addLayout(title_row);
    column->addWidget(header_);
    auto* rule = new QFrame(this);
    rule->setObjectName(QStringLiteral("floating-panel-rule"));
    rule->setFixedHeight(1);
    column->addWidget(rule);
    body_ = new QVBoxLayout;
    body_->setContentsMargins(0, 0, 0, 0);
    body_->setSpacing(0);
    column->addLayout(body_);
    QObject::connect(close, &QToolButton::clicked, this, [this] {
      hide();
      for (const auto& closed : closed_) closed();
    });
    header_->installEventFilter(this);
    if (parent != nullptr) parent->installEventFilter(this);
  }

  QVBoxLayout* body() { return body_; }
  // 点了关闭按钮（面板已经藏起来）。
  void onClosed(std::function<void()> callback) { closed_.push_back(std::move(callback)); }
  // 内容变了：按内容重算高度，放回原位。
  void refit() {
    adjustSize();
    place();
  }
  // 父控件（画布区）变大小时先回调这个（内容按新的可用高度重算），再放回原位。
  void onParentResized(std::function<void()> callback) { parent_resized_.push_back(std::move(callback)); }
  // 面板在父控件里最多能有多高（上下各留一点）。
  int room() const { return parentWidget() != nullptr ? parentWidget()->height() - top_ - 8 : 0; }

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
      for (const auto& resized : parent_resized_) resized();
      place();
    }
    if (watched == header_) {
      auto* mouse = static_cast<QMouseEvent*>(event);
      if (event->type() == QEvent::MouseButtonPress && mouse->button() == Qt::LeftButton) {
        grab_ = mouse->globalPosition().toPoint() - pos();
        dragging_ = true;
        return true;
      }
      if (event->type() == QEvent::MouseMove && dragging_ && parentWidget() != nullptr) {
        const QPoint at = mouse->globalPosition().toPoint() - grab_;
        right_ = parentWidget()->width() - width() - at.x();
        top_ = at.y();
        place();
        return true;
      }
      if (event->type() == QEvent::MouseButtonRelease) dragging_ = false;
    }
    return QFrame::eventFilter(watched, event);
  }

 private:
  // 位置记成离父控件右边、上边的距离，父控件变大小时贴着右上角走。
  void place() {
    auto* parent = parentWidget();
    if (parent == nullptr) return;
    const int x = std::clamp(parent->width() - width() - right_, 0, std::max(0, parent->width() - width()));
    const int y = std::clamp(top_, 0, std::max(0, parent->height() - height()));
    move(x, y);
    raise();
  }
  static QIcon closeIcon() {
    const qreal ratio = 2.0;
    QPixmap pixmap(qRound(14 * ratio), qRound(14 * ratio));
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(0xc8, 0xc8, 0xc8), 1.3));
    painter.drawLine(QPointF(1.5, 1.5), QPointF(12.5, 12.5));
    painter.drawLine(QPointF(12.5, 1.5), QPointF(1.5, 12.5));
    painter.end();
    return QIcon(pixmap);
  }

  QWidget* header_ = nullptr;
  QVBoxLayout* body_ = nullptr;
  std::vector<std::function<void()>> closed_;
  std::vector<std::function<void()>> parent_resized_;
  QPoint grab_;
  bool dragging_ = false;
  int right_ = 2;
  int top_ = 2;
};

// 官方蒙版列表的树：蒙版一行（左边小三角收起/展开子项），子项缩进在下面。QTreeWidget 自带的分支箭头和官方不一样，
// 这里只换掉分支的画法，用和折叠分组同一个小三角。
class PixCakeMaskTree final : public QTreeWidget {
 public:
  explicit PixCakeMaskTree(QWidget* parent = nullptr) : QTreeWidget(parent) {
    setHeaderHidden(true);
    setIndentation(12);
    setRootIsDecorated(true);
    setExpandsOnDoubleClick(false);  // 双击是改名
    setSelectionMode(QAbstractItemView::NoSelection);
    setFocusPolicy(Qt::ClickFocus);
    setUniformRowHeights(false);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);
    setIconSize(QSize(40, 32));
  }
  // 所有看得见的行高之和（行高由每行的 sizeHint 给定）。
  int contentHeight() const {
    int total = 0;
    for (QTreeWidgetItemIterator it(const_cast<PixCakeMaskTree*>(this)); *it != nullptr; ++it) {
      const QTreeWidgetItem* item = *it;
      bool shown = !item->isHidden();
      for (auto* up = item->parent(); up != nullptr && shown; up = up->parent()) shown = up->isExpanded();
      if (shown) total += std::max(0, item->sizeHint(0).height());
    }
    return total;
  }

 protected:
  void drawBranches(QPainter* painter, const QRect& rect, const QModelIndex& index) const override {
    const auto* item = itemFromIndex(index);
    if (item == nullptr) return;
    // 行的底色（选中蒙版那一块深色底）也铺到三角这一格，和整行连成一块。
    if (const QBrush back = item->background(0); back.style() != Qt::NoBrush) painter->fillRect(rect, back);
    if (item->childCount() == 0) return;
    const QPixmap triangle = PixCakeFoldSection::triangleIcon(item->isExpanded()).pixmap(QSize(8, 8));
    const QRect area(rect.right() - indentation() + 1, rect.top(), indentation(), rect.height());
    painter->drawPixmap(area.left() + (area.width() - 8) / 2, area.top() + (area.height() - 8) / 2, triangle);
  }
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
    setFocusPolicy(Qt::StrongFocus);
  }

  void setImage(QImage image) {
    image_ = std::move(image);
    status_.clear();
    update();
    for (const auto& changed : image_changed_) changed();
  }
  // 换了一帧时回调（蒙版列表“全图”的缩略图跟着换）。
  void onImageChanged(std::function<void()> callback) { image_changed_.push_back(std::move(callback)); }
  void setStatus(QString status) {
    status_ = std::move(status);
    update();
  }
  const QImage& image() const { return image_; }
  void setHooks(std::shared_ptr<CanvasHooks> hooks) {
    hooks_ = std::move(hooks);
    update();
  }
  void setGuides(std::vector<GuideLine> guides, bool visible) {
    guides_ = std::move(guides);
    guides_visible_ = visible;
    if (guide_selected_ && *guide_selected_ >= guides_.size()) guide_selected_.reset();
    if (guide_drag_index_ && *guide_drag_index_ >= guides_.size()) {
      guide_drag_index_.reset();
      guide_dragging_ = false;
    }
    update();
  }
  const std::vector<GuideLine>& guides() const { return guides_; }
  bool guidesVisible() const { return guides_visible_; }
  std::optional<std::size_t> selectedGuide() const { return guide_selected_; }
  void onGuideMoved(std::function<void(std::size_t, double)> callback) { guide_moved_ = std::move(callback); }
  void onGuideRemoved(std::function<void(std::size_t)> callback) { guide_removed_ = std::move(callback); }
  void onGuidesRemoved(std::function<void()> callback) { guides_removed_ = std::move(callback); }
  void setGuideMenuLabels(QString remove, QString remove_all) {
    guide_remove_text_ = std::move(remove);
    guide_remove_all_text_ = std::move(remove_all);
  }
  // 鼠标在画面上时回调指针下像素的颜色（显示的这一帧），移出画面回调空值。色彩调节、局部调色页的直方图读数用它，
  // 可以登记多个。
  void onHoverPixel(std::function<void(std::optional<QColor>)> callback) { hover_pixel_.push_back(std::move(callback)); }
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
    if (event->button() == Qt::RightButton) {
      const auto index = guideAt(event->position());
      if (index) {
        guide_selected_ = *index;
        setFocus(Qt::MouseFocusReason);
        update();
        QMenu menu(this);
        QAction* remove = menu.addAction(guide_remove_text_);
        QAction* remove_all = menu.addAction(guide_remove_all_text_);
        QObject::connect(remove, &QAction::triggered, this, [this, index] {
          guide_selected_.reset();
          if (guide_removed_) guide_removed_(*index);
        });
        QObject::connect(remove_all, &QAction::triggered, this, [this] {
          guide_selected_.reset();
          if (guides_removed_) guides_removed_();
        });
        menu.exec(mapToGlobal(event->position().toPoint()));
        setFocus(Qt::OtherFocusReason);
        event->accept();
        return;
      }
    }
    if (event->button() == Qt::LeftButton) {
      const auto index = guideAt(event->position());
      if (index) {
        guide_selected_ = *index;
        setFocus(Qt::MouseFocusReason);
        guide_drag_index_ = *index;
        guide_dragging_ = true;
        setCursor(guides_[*index].vertical ? Qt::SizeHorCursor : Qt::SizeVerCursor);
        update();
        event->accept();
        return;
      }
      guide_selected_.reset();
      update();
    }
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
    if (guide_dragging_ && guide_drag_index_ && guide_moved_) {
      const auto& guide = guides_[*guide_drag_index_];
      const QRectF target(0.0, 0.0, width(), height());
      if (!target.isEmpty()) {
        const double position = guide.vertical
                                    ? std::clamp((event->position().x() - target.left()) / target.width() * 100.0,
                                                 0.0, 100.0)
                                    : std::clamp((event->position().y() - target.top()) / target.height() * 100.0,
                                                 0.0, 100.0);
        guide_moved_(*guide_drag_index_, position);
      }
      update();
      event->accept();
    } else if (dragging_ && hooks_) {
      hooks_->drag(1, percent(event->position()));
      update();
    } else {
      if (hooks_ && hooks_->cursor_diameter && hooks_->cursor_diameter() > 0) update();
      QFrame::mouseMoveEvent(event);
    }
  }
  void leaveEvent(QEvent* event) override {
    hovering_ = false;
    for (const auto& report : hover_pixel_) report(std::nullopt);
    update();
    QFrame::leaveEvent(event);
  }
  void mouseReleaseEvent(QMouseEvent* event) override {
    if (guide_dragging_) {
      guide_dragging_ = false;
      guide_drag_index_.reset();
      setCursor(Qt::ArrowCursor);
      event->accept();
    } else if (dragging_ && hooks_) {
      dragging_ = false;
      hooks_->drag(2, percent(event->position()));
      update();
    } else {
      QFrame::mouseReleaseEvent(event);
    }
  }
  void keyPressEvent(QKeyEvent* event) override {
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && guide_selected_) {
      const std::size_t index = *guide_selected_;
      guide_selected_.reset();
      if (guide_removed_) guide_removed_(index);
      update();
      event->accept();
      return;
    }
    QFrame::keyPressEvent(event);
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
      if (guides_visible_) paintGuides(painter, QRectF(0.0, 0.0, width(), height()));
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
    if (hover_pixel_.empty()) return;
    const QRectF target = imageRect();
    if (image_.isNull() || !target.contains(hover_)) {
      for (const auto& report : hover_pixel_) report(std::nullopt);
      return;
    }
    const int x = std::clamp(static_cast<int>((hover_.x() - target.left()) / target.width() * image_.width()), 0,
                             image_.width() - 1);
    const int y = std::clamp(static_cast<int>((hover_.y() - target.top()) / target.height() * image_.height()), 0,
                             image_.height() - 1);
    const QColor color = image_.pixelColor(x, y);
    for (const auto& report : hover_pixel_) report(color);
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

  std::optional<std::size_t> guideAt(const QPointF& at) const {
    if (!guides_visible_ || image_.isNull()) return std::nullopt;
    const QRectF target(0.0, 0.0, width(), height());
    if (!target.contains(at)) return std::nullopt;
    constexpr qreal kHitDistance = 8.0;
    for (std::size_t index = guides_.size(); index-- > 0;) {
      const qreal line = guides_[index].vertical
                             ? target.left() + target.width() * guides_[index].position / 100.0
                             : target.top() + target.height() * guides_[index].position / 100.0;
      const qreal current = guides_[index].vertical ? std::abs(at.x() - line) : std::abs(at.y() - line);
      if (current <= kHitDistance) return index;
    }
    return std::nullopt;
  }
  void paintGuides(QPainter& painter, const QRectF& target) const {
    for (std::size_t index = 0; index < guides_.size(); ++index) {
      const auto& guide = guides_[index];
      const qreal line = guide.vertical
                             ? target.left() + target.width() * guide.position / 100.0
                             : target.top() + target.height() * guide.position / 100.0;
      const bool selected = guide_selected_ && *guide_selected_ == index;
      painter.setPen(QPen(QColor(0, 0, 0, 200), selected ? 5.0 : 3.0));
      if (guide.vertical) painter.drawLine(QPointF(line, target.top()), QPointF(line, target.bottom()));
      else painter.drawLine(QPointF(target.left(), line), QPointF(target.right(), line));
      painter.setPen(QPen(selected ? QColor(0xff, 0xd2, 0x3d) : QColor(0x55, 0xd8, 0xff, 235),
                         selected ? 2.0 : 1.0, selected ? Qt::SolidLine : Qt::DashLine));
      if (guide.vertical) painter.drawLine(QPointF(line, target.top()), QPointF(line, target.bottom()));
      else painter.drawLine(QPointF(target.left(), line), QPointF(target.right(), line));
    }
  }

  QImage image_;
  QString status_;
  std::shared_ptr<CanvasHooks> hooks_;
  bool dragging_ = false;
  QPointF hover_;
  bool hovering_ = false;
  std::vector<std::function<void(std::optional<QColor>)>> hover_pixel_;
  std::vector<std::function<void()>> image_changed_;
  std::vector<GuideLine> guides_;
  bool guides_visible_ = true;
  std::optional<std::size_t> guide_selected_;
  std::optional<std::size_t> guide_drag_index_;
  bool guide_dragging_ = false;
  std::function<void(std::size_t, double)> guide_moved_;
  std::function<void(std::size_t)> guide_removed_;
  std::function<void()> guides_removed_;
  QString guide_remove_text_;
  QString guide_remove_all_text_;
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
