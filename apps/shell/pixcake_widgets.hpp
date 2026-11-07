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
#include <QStringList>
#include <QSvgRenderer>
#include <QHBoxLayout>
#include <QImage>
#include <QPainterPath>
#include <QVBoxLayout>

#include "pixcake/ui/panels/histogram.hpp"
#include "pixcake/ui/shell/page_copy.hpp"

#include <functional>
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

// RGB line histogram: three QPainter polylines over the 0..255 axis.  Counts
// come from ui/panels::rgb_histogram; without data only the axis is drawn.
class PixCakeHistogram final : public QFrame {
 public:
  explicit PixCakeHistogram(QWidget* parent = nullptr) : QFrame(parent) {
    setObjectName(QStringLiteral("refine-histogram"));
    setAccessibleName(copyText("widget.histogram"));
  }

  void setHistogram(const ui::panels::RgbHistogram& histogram) {
    histogram_ = histogram;
    update();
  }
  const ui::panels::RgbHistogram& histogram() const { return histogram_; }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.setPen(palette().color(QPalette::Mid));
    const int baseline = height() - 18;
    painter.drawLine(8, baseline, width() - 8, baseline);
    painter.drawText(8, height() - 2, QStringLiteral("0"));
    const QString end = QStringLiteral("255");
    painter.drawText(width() - 8 - painter.fontMetrics().horizontalAdvance(end), height() - 2, end);
    if (histogram_.peak == 0) return;
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal left = 8;
    const qreal span = width() - 16;
    const qreal top = 4;
    const qreal tall = baseline - top;
    const auto line = [&](const std::array<std::uint32_t, 256>& bins, const QColor& color) {
      QPainterPath path;
      for (int i = 0; i < 256; ++i) {
        const QPointF point(left + span * i / 255.0, baseline - tall * bins[static_cast<std::size_t>(i)] / histogram_.peak);
        if (i == 0) path.moveTo(point); else path.lineTo(point);
      }
      painter.setPen(QPen(color, 1.2));
      painter.drawPath(path);
    };
    line(histogram_.red, QColor(230, 70, 70, 220));
    line(histogram_.green, QColor(70, 200, 90, 220));
    line(histogram_.blue, QColor(80, 130, 240, 220));
  }

 private:
  ui::panels::RgbHistogram histogram_;
};

// The refine canvas: shows the latest preview frame fitted into the widget
// (aspect kept, never enlarged past 1:1 device pixels), or a status line.
class PixCakeImageView final : public QFrame {
 public:
  explicit PixCakeImageView(QWidget* parent = nullptr) : QFrame(parent) {
    setObjectName(QStringLiteral("refine-canvas-image"));
    setAccessibleName(copyText("widget.canvas"));
    setMinimumSize(160, 120);
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

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    if (!image_.isNull()) {
      const qreal ratio = devicePixelRatioF();
      QSizeF size = QSizeF(image_.size()) / ratio;
      size.scale(QSizeF(rect().size()).boundedTo(size), Qt::KeepAspectRatio);
      const QRectF target(QPointF((width() - size.width()) / 2, (height() - size.height()) / 2), size);
      painter.setRenderHint(QPainter::SmoothPixmapTransform);
      painter.drawImage(target, image_);
    }
    if (!status_.isEmpty()) {
      painter.setPen(palette().color(QPalette::PlaceholderText));
      painter.drawText(rect(), Qt::AlignCenter, status_);
    }
  }

 private:
  QImage image_;
  QString status_;
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

inline void applyAssetIcon(QPushButton* button, const QString& relative, int size) {
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
