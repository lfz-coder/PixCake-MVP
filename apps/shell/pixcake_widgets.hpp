#pragma once

#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
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

inline QLabel* pendingIcon(QWidget* parent = nullptr) {
  auto* icon = new QLabel(parent);
  icon->setObjectName(QStringLiteral("pending-icon"));
  icon->setAccessibleName(QStringLiteral("待补充官方图标"));
  icon->setFixedSize(28, 28);
  return icon;
}

inline PixCakeEmptyState* emptyState(const QString& title, const QString& detail,
                                     QWidget* parent = nullptr) {
  return new PixCakeEmptyState(title, detail, parent);
}

}  // namespace pixcake::shell
