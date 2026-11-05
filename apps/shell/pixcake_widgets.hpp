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

namespace pixcake::shell {

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
  icon->setAccessibleName(accessible.isEmpty() ? QStringLiteral("待补充官方图标")
                                               : accessible);
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
    setAccessibleName(QStringLiteral("直方图"));
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
                       accessible.isEmpty() ? QString()
                                            : accessible + QStringLiteral("待补充"));
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

inline QString toolAssetPath(const QString& label) {
  if (label == QStringLiteral("显示比例调整")) return QStringLiteral("workspace-tools/zoom-display.svg");
  if (label == QStringLiteral("抓手")) return QStringLiteral("workspace-tools/hand.svg");
  if (label == QStringLiteral("祛边")) return QStringLiteral("workspace-tools/edge-remove.svg");
  if (label == QStringLiteral("参考线")) return QStringLiteral("workspace-tools/guides.svg");
  if (label == QStringLiteral("液化")) return QStringLiteral("workspace-tools/liquify.svg");
  if (label == QStringLiteral("污点修复") || label == QStringLiteral("污点修复画笔"))
    return QStringLiteral("workspace-tools/spot-heal.svg");
  if (label == QStringLiteral("修补") || label == QStringLiteral("修补工具"))
    return QStringLiteral("workspace-tools/patch.svg");
  if (label == QStringLiteral("仿制图章")) return QStringLiteral("workspace-tools/clone-stamp.svg");
  if (label == QStringLiteral("智能消除")) return QStringLiteral("workspace-tools/smart-remove.svg");
  if (label == QStringLiteral("AI点修")) return QStringLiteral("workspace-tools/ai-spot.svg");
  if (label == QStringLiteral("重置效果")) return QStringLiteral("workspace-tools/reset.svg");
  if (label == QStringLiteral("预设")) return QStringLiteral("workspace-tools/preset.svg");
  if (label == QStringLiteral("更改屏幕模式")) return QStringLiteral("workspace-tools/screen-mode.svg");
  if (label == QStringLiteral("移动图层")) return QStringLiteral("workspace-tools/move-layer.svg");
  if (label == QStringLiteral("缩放工具")) return QStringLiteral("workspace-tools/zoom-tool.svg");
  if (label == QStringLiteral("矩形/椭圆选区")) return QStringLiteral("workspace-tools/marquee.svg");
  if (label == QStringLiteral("套索/选区画笔")) return QStringLiteral("workspace-tools/lasso.svg");
  if (label == QStringLiteral("裁切")) return QStringLiteral("workspace-tools/crop.svg");
  return {};
}

inline QString globalAssetPath(const QString& label) {
  if (label == QStringLiteral("搜索")) return QStringLiteral("workspace-global/search.svg");
  if (label == QStringLiteral("会员续费")) return QStringLiteral("workspace-global/membership.svg");
  if (label == QStringLiteral("技术支持")) return QStringLiteral("workspace-global/support.svg");
  if (label == QStringLiteral("个人中心")) return QStringLiteral("workspace-global/account.svg");
  if (label == QStringLiteral("消息中心")) return QStringLiteral("workspace-global/messages.svg");
  if (label == QStringLiteral("传输列表")) return QStringLiteral("workspace-global/transfer.svg");
  if (label == QStringLiteral("导出列表")) return QStringLiteral("workspace-global/export-list.svg");
  if (label == QStringLiteral("导出")) return QStringLiteral("workspace-global/export.svg");
  if (label == QStringLiteral("像素助手")) return QStringLiteral("workspace-global/assistant.svg");
  if (label == QStringLiteral("AI工具")) return QStringLiteral("workspace-global/ai-tools.svg");
  if (label == QStringLiteral("历史")) return QStringLiteral("workspace-global/history.svg");
  if (label == QStringLiteral("批量")) return QStringLiteral("workspace-global/batch.svg");
  if (label == QStringLiteral("帮助")) return QStringLiteral("help.svg");
  if (label == QStringLiteral("商家账号")) return QStringLiteral("merchant.svg");
  if (label == QStringLiteral("返回") || label == QStringLiteral("首页"))
    return QStringLiteral("workspace-global/home.svg");
  if (label == QStringLiteral("筛选")) return QStringLiteral("workspace-global/filter.svg");
  if (label == QStringLiteral("排序")) return QStringLiteral("workspace-global/sort.svg");
  if (label == QStringLiteral("星标")) return QStringLiteral("workspace-global/star.svg");
  if (label == QStringLiteral("旗标")) return QStringLiteral("workspace-global/flag.svg");
  if (label == QStringLiteral("色标")) return QStringLiteral("workspace-global/color-tag.svg");
  if (label == QStringLiteral("网格视图")) return QStringLiteral("workspace-global/view-grid.svg");
  if (label == QStringLiteral("单视图")) return QStringLiteral("workspace-global/view-single.svg");
  if (label == QStringLiteral("筛选视图")) return QStringLiteral("workspace-global/view-filter.svg");
  if (label == QStringLiteral("对比视图")) return QStringLiteral("workspace-global/view-compare.svg");
  if (label == QStringLiteral("参考视图")) return QStringLiteral("workspace-global/view-reference.svg");
  if (label == QStringLiteral("图库")) return QStringLiteral("workspace-global/tab-library.svg");
  if (label == QStringLiteral("精修")) return QStringLiteral("workspace-global/tab-refine.svg");
  if (label == QStringLiteral("创意")) return QStringLiteral("workspace-global/tab-creative.svg");
  return {};
}

inline QString homeNavAssetPath(const QString& label) {
  if (label == QStringLiteral("主页")) return QStringLiteral("home-nav/home.svg");
  if (label == QStringLiteral("AI 工具箱")) return QStringLiteral("home-nav/ai-toolbox.svg");
  if (label == QStringLiteral("本地项目")) return QStringLiteral("home-nav/local-projects.svg");
  if (label == QStringLiteral("云空间")) return QStringLiteral("home-nav/cloud.svg");
  if (label == QStringLiteral("工作流")) return QStringLiteral("home-nav/workflow.svg");
  if (label == QStringLiteral("选片交付")) return QStringLiteral("home-nav/delivery.svg");
  if (label == QStringLiteral("回收站")) return QStringLiteral("home-nav/trash.svg");
  return {};
}

inline QString homeCardAssetPath(const QString& label) {
  if (label == QStringLiteral("联机拍摄")) return QStringLiteral("home-capture.svg");
  if (label == QStringLiteral("AI工具箱")) return QStringLiteral("home-ai-toolbox.svg");
  if (label == QStringLiteral("像素助手")) return QStringLiteral("home-assistant.svg");
  return {};
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
