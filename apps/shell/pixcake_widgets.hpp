#pragma once

#include <QCoreApplication>
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
#include <QVBoxLayout>

#include "pixcake/ui/shell/page_copy.hpp"

#include <string_view>

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

class PixCakeHistogram final : public QFrame {
 public:
  explicit PixCakeHistogram(QWidget* parent = nullptr) : QFrame(parent) {
    setObjectName(QStringLiteral("refine-histogram"));
    setAccessibleName(copyText("widget.histogram"));
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.setPen(palette().color(QPalette::Mid));
    const int baseline = height() - 18;
    painter.drawLine(8, baseline, width() - 8, baseline);
    painter.drawText(8, height() - 2, QStringLiteral("0"));
    const QString end = QStringLiteral("255");
    painter.drawText(width() - 8 - painter.fontMetrics().horizontalAdvance(end), height() - 2, end);
  }
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
