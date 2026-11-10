#include "pages.hpp"

#include "pixcake_widgets.hpp"
#include "shell_icons.hpp"
#include "trash_dialog.hpp"

#include <QComboBox>
#include <QDialog>
#include <QFileDialog>
#include <QGridLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QScrollArea>
#include <QStackedWidget>

#include <algorithm>
#include <functional>
#include <optional>
#include <vector>

namespace pixcake::shell {
namespace {

bool is_local_project(const ProjectEntry& project) {
  return project.origin == kOriginLocal || project.origin == kOriginCloudDownloaded;
}

class ProjectCard final : public QFrame {
 public:
  ProjectCard(const ProjectEntry& project, bool create_card, QWidget* parent = nullptr)
      : QFrame(parent), project_(project), create_card_(create_card) {
    setObjectName(create_card ? QStringLiteral("local-project-create-card") : QStringLiteral("local-project-card"));
    setAccessibleName(create_card ? qtext("local-projects.new") : qs(project.name));
    setFixedSize(228, create_card ? 196 : 204);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
  }

  const ProjectEntry& project() const { return project_; }
  bool isCreateCard() const { return create_card_; }
  bool matches(const QString& needle) const {
    return create_card_ || needle.isEmpty() || qs(project_.name).contains(needle, Qt::CaseInsensitive);
  }
  void setSelected(bool selected) {
    if (selected_ == selected) return;
    selected_ = selected;
    update();
  }
  void onClicked(std::function<void()> callback) { clicked_ = std::move(callback); }
  void onDoubleClicked(std::function<void()> callback) { double_clicked_ = std::move(callback); }

 protected:
  void mouseReleaseEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()) && clicked_) clicked_();
    QFrame::mouseReleaseEvent(event);
  }
  void mouseDoubleClickEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton && double_clicked_) double_clicked_();
    QFrame::mouseDoubleClickEvent(event);
  }
  void enterEvent(QEnterEvent* event) override {
    hovered_ = true;
    update();
    QFrame::enterEvent(event);
  }
  void leaveEvent(QEvent* event) override {
    hovered_ = false;
    update();
    QFrame::leaveEvent(event);
  }
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF bounds = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
    const QColor base = hovered_ ? QColor(QStringLiteral("#303033")) : QColor(QStringLiteral("#272729"));
    painter.setPen(Qt::NoPen);
    painter.setBrush(base);
    painter.drawRoundedRect(bounds, 10.0, 10.0);

    if (create_card_) {
      painter.setPen(QPen(hovered_ ? QColor(QStringLiteral("#B4B4BB")) : QColor(QStringLiteral("#8A8A8F")), 2.0));
      const qreal cx = width() / 2.0;
      const qreal cy = height() / 2.0;
      painter.drawLine(QPointF(cx - 14.0, cy), QPointF(cx + 14.0, cy));
      painter.drawLine(QPointF(cx, cy - 14.0), QPointF(cx, cy + 14.0));
      return;
    }

    const qreal preview_height = height() * 0.70;
    painter.save();
    QPainterPath clip;
    clip.addRoundedRect(QRectF(1.0, 1.0, width() - 2.0, preview_height + 10.0), 9.0, 9.0);
    painter.setClipPath(clip);
    const int hue = static_cast<int>((project_.id * 47) % 360);
    const QColor a = QColor::fromHsv(hue, 90, hovered_ ? 205 : 180);
    const QColor b = QColor::fromHsv((hue + 45) % 360, 75, hovered_ ? 150 : 126);
    const bool multi = project_.id % 3 == 0;
    if (multi) {
      const qreal block = width() / 3.0;
      for (int index = 0; index < 3; ++index) {
        QLinearGradient gradient(QPointF(index * block, 0), QPointF((index + 1) * block, preview_height));
        gradient.setColorAt(0.0, QColor::fromHsv((hue + index * 28) % 360, 65, 190));
        gradient.setColorAt(1.0, QColor::fromHsv((hue + index * 28 + 30) % 360, 80, 95));
        painter.fillRect(QRectF(index * block, 0, block + 1.0, preview_height + 10.0), gradient);
        painter.setPen(QPen(QColor(255, 255, 255, 45), 1.0));
        painter.drawLine(QPointF((index + 1) * block, 0), QPointF((index + 1) * block, preview_height));
      }
    } else {
      QLinearGradient gradient(QPointF(0, 0), QPointF(width(), preview_height));
      gradient.setColorAt(0.0, a);
      gradient.setColorAt(0.55, b);
      gradient.setColorAt(1.0, QColor(QStringLiteral("#1C1C21")));
      painter.fillRect(QRectF(0, 0, width(), preview_height + 10.0), gradient);
      painter.setPen(QPen(QColor(255, 255, 255, 35), 1.0));
      painter.drawEllipse(QRectF(width() * 0.14, preview_height * 0.18, width() * 0.48, preview_height * 0.48));
      painter.setBrush(QColor(255, 255, 255, 25));
      painter.drawEllipse(QRectF(width() * 0.56, preview_height * 0.38, width() * 0.35, preview_height * 0.55));
    }
    QLinearGradient shade(QPointF(0, preview_height * 0.35), QPointF(0, preview_height));
    shade.setColorAt(0.0, QColor(0, 0, 0, 0));
    shade.setColorAt(1.0, QColor(0, 0, 0, 135));
    painter.fillRect(QRectF(0, 0, width(), preview_height + 2.0), shade);
    painter.restore();

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(20, 20, 22, 235));
    painter.drawRect(QRectF(1.0, preview_height, width() - 2.0, height() - preview_height - 1.0));
    QFont name_font = painter.font();
    name_font.setPixelSize(15);
    name_font.setWeight(QFont::Medium);
    painter.setFont(name_font);
    painter.setPen(QColor(QStringLiteral("#FFFFFF")));
    const QFontMetrics name_metrics(name_font);
    const QString name = name_metrics.elidedText(qs(project_.name), Qt::ElideRight, width() - 48);
    painter.drawText(QRectF(14, preview_height + 10, width() - 38, 20), Qt::AlignLeft | Qt::AlignVCenter, name);

    QFont detail_font = painter.font();
    detail_font.setPixelSize(11);
    detail_font.setWeight(QFont::Normal);
    painter.setFont(detail_font);
    painter.setPen(QColor(QStringLiteral("#A3A3A8")));
    const int total = 6 + static_cast<int>(std::abs(project_.id) % 8);
    const int edited = static_cast<int>(std::abs(project_.id) % (total + 1));
    painter.drawText(QRectF(14, preview_height + 31, width() - 24, 16), Qt::AlignLeft | Qt::AlignVCenter,
                     qtext("local-projects.card.status").arg(edited).arg(total));
    painter.drawText(QRectF(14, preview_height + 48, width() - 24, 16), Qt::AlignLeft | Qt::AlignVCenter,
                     qtext("local-projects.card.updated"));
    if (project_.cloud_marked) {
      const QPixmap cloud = loadAssetPixmap(iconPath("home-nav", "cloud"), 15);
      if (!cloud.isNull()) painter.drawPixmap(QRect(width() - 28, preview_height + 11, 15, 15), cloud);
    }
    if (selected_) {
      painter.setBrush(Qt::NoBrush);
      painter.setPen(QPen(QColor(QStringLiteral("#489CFF")), 2.0));
      painter.drawRoundedRect(bounds, 10.0, 10.0);
    }
  }

 private:
  ProjectEntry project_;
  bool create_card_ = false;
  bool selected_ = false;
  bool hovered_ = false;
  std::function<void()> clicked_;
  std::function<void()> double_clicked_;
};

class ProjectGrid final : public QWidget {
 public:
  explicit ProjectGrid(QWidget* parent = nullptr) : QWidget(parent) {
    layout_ = new QGridLayout(this);
    layout_->setContentsMargins(2, 2, 2, 18);
    layout_->setHorizontalSpacing(16);
    layout_->setVerticalSpacing(16);
    layout_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  }

  const std::vector<ProjectCard*>& cards() const { return cards_; }
  void setCards(std::vector<ProjectCard*> cards) {
    for (auto* card : cards_) card->deleteLater();
    cards_ = std::move(cards);
    reflow();
  }
  void filter(const QString& needle) {
    filter_ = needle.trimmed();
    reflow();
  }

 protected:
  void resizeEvent(QResizeEvent* event) override {
    QWidget::resizeEvent(event);
    reflow();
  }

 private:
  void reflow() {
    while (auto* item = layout_->takeAt(0)) delete item;
    const int usable = std::max(1, width() - layout_->contentsMargins().left() - layout_->contentsMargins().right());
    const int columns = std::max(1, (usable + layout_->horizontalSpacing()) / (228 + layout_->horizontalSpacing()));
    int visible = 0;
    for (auto* card : cards_) {
      const bool show = card->matches(filter_);
      card->setVisible(show);
      if (!show) continue;
      layout_->addWidget(card, visible / columns, visible % columns);
      ++visible;
    }
    const int rows = std::max(1, (visible + columns - 1) / columns);
    setMinimumHeight(rows * 220 + layout_->contentsMargins().top() + layout_->contentsMargins().bottom());
    updateGeometry();
  }

  QGridLayout* layout_ = nullptr;
  std::vector<ProjectCard*> cards_;
  QString filter_;
};

class CreateProjectDialog final : public QDialog {
 public:
  explicit CreateProjectDialog(QWidget* parent = nullptr) : QDialog(parent) {
    setObjectName(QStringLiteral("create-project-dialog"));
    setModal(true);
    setFixedSize(440, 236);
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 20, 24, 22);
    root->setSpacing(0);
    auto* header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 16);
    auto* title = text(qtext("local-projects.create.title"));
    title->setObjectName(QStringLiteral("create-project-title"));
    header->addWidget(title);
    header->addStretch();
    auto* close = new QPushButton(qtext("local-projects.close.symbol"), this);
    close->setObjectName(QStringLiteral("create-project-close"));
    close->setFixedSize(28, 28);
    close->setFlat(true);
    close->setAccessibleName(qtext("common.close"));
    QObject::connect(close, &QPushButton::clicked, this, &QDialog::reject);
    header->addWidget(close);
    root->addLayout(header);
    auto* input_row = new QHBoxLayout;
    input_row->setSpacing(10);
    input_ = new QLineEdit(this);
    input_->setObjectName(QStringLiteral("create-project-name"));
    input_->setPlaceholderText(qtext("local-projects.create.placeholder"));
    input_->setMaxLength(40);
    counter_ = text(QStringLiteral("0/40"), true);
    counter_->setObjectName(QStringLiteral("create-project-counter"));
    counter_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    input_row->addWidget(input_, 1);
    input_row->addWidget(counter_);
    root->addLayout(input_row);
    root->addStretch(1);
    save_ = new QPushButton(qtext("local-projects.create.save"), this);
    save_->setObjectName(QStringLiteral("create-project-save"));
    save_->setFixedSize(100, 42);
    save_->setCursor(Qt::PointingHandCursor);
    auto* save_row = new QHBoxLayout;
    save_row->addStretch();
    save_row->addWidget(save_);
    root->addLayout(save_row);
    QObject::connect(input_, &QLineEdit::textChanged, this, [this](const QString& value) {
      counter_->setText(QStringLiteral("%1/40").arg(value.size()));
      if (!value.trimmed().isEmpty()) input_->setStyleSheet(QString());
    });
    QObject::connect(save_, &QPushButton::clicked, this, [this] {
      if (input_->text().trimmed().isEmpty()) {
        input_->setStyleSheet(QStringLiteral("QLineEdit { border: 1px solid #FF5C67; }") +
                              QStringLiteral("QLineEdit:focus { border: 1px solid #FF5C67; }"));
        input_->setFocus();
        return;
      }
      accept();
    });
    input_->setFocus();
  }

  QString projectName() const { return input_->text().trimmed(); }

 private:
  QLineEdit* input_ = nullptr;
  QLabel* counter_ = nullptr;
  QPushButton* save_ = nullptr;
};

QWidget* buildImportPlaceholder(QWidget* parent, PageActions* actions, const QString& name) {
  auto* page = new QWidget(parent);
  page->setObjectName(QStringLiteral("local-project-import-page"));
  auto* layout = new QVBoxLayout(page);
  layout->setContentsMargins(24, 24, 24, 32);
  layout->setAlignment(Qt::AlignCenter);
  auto* title = text(name.isEmpty() ? qtext("local-projects.import.title")
                                     : qtext("local-projects.import.title").arg(name));
  title->setObjectName(QStringLiteral("local-project-import-title"));
  title->setAlignment(Qt::AlignCenter);
  layout->addWidget(title);
  auto* drop = new PixCakeDropPanel(QStringLiteral("local-project-import-drop"), page);
  drop->setFixedSize(420, 300);
  drop->setAccessibleName(qtext("local-projects.import.hint"));
  auto* drop_layout = new QVBoxLayout(drop);
  drop_layout->setContentsMargins(24, 24, 24, 24);
  drop_layout->setAlignment(Qt::AlignCenter);
  auto* plus = text(QStringLiteral("+"));
  plus->setObjectName(QStringLiteral("local-project-import-plus"));
  plus->setAlignment(Qt::AlignCenter);
  drop_layout->addWidget(plus);
  auto* hint = text(qtext("local-projects.import.hint"), true);
  hint->setAlignment(Qt::AlignCenter);
  drop_layout->addWidget(hint);
  auto* link = new QPushButton(qtext("local-projects.import.link"), drop);
  link->setObjectName(QStringLiteral("local-project-import-link"));
  link->setFlat(true);
  link->setCursor(Qt::PointingHandCursor);
  drop_layout->addWidget(link, 0, Qt::AlignCenter);
  layout->addWidget(drop, 0, Qt::AlignCenter);
  const auto import = [drop, actions, name] {
    const QStringList chosen = QFileDialog::getOpenFileNames(drop, qtext("local-projects.import.choose"), QString(),
                                                               qtext("home.import.filter"));
    std::vector<std::string> paths;
    for (const auto& path : chosen) paths.push_back(path.toStdString());
    if (!paths.empty()) actions->request_import_named(paths, name.toStdString());
  };
  drop->onClicked(import);
  drop->onDropped([actions, name](const std::vector<std::string>& paths) {
    if (!paths.empty()) actions->request_import_named(paths, name.toStdString());
  });
  QObject::connect(link, &QPushButton::clicked, link, [] {});
  return page;
}

}  // namespace

QWidget* buildLocalProjectsPage(const PageContext& context) {
  PageActions* actions = &context.actions;
  auto* page = new QWidget;
  page->setObjectName(QStringLiteral("local-projects-page"));
  auto* content = new QFrame(page);
  content->setObjectName(QStringLiteral("local-projects-content"));
  auto* main = new QVBoxLayout(content);
  main->setContentsMargins(28, 0, 28, 24);
  main->setSpacing(0);
  auto* top = new QHBoxLayout;
  top->setContentsMargins(0, 16, 0, 12);
  auto* search = new PixCakeField(qtext("local-projects.search.name"), content);
  search->setObjectName(QStringLiteral("local-projects-search"));
  search->setPlaceholderText(qtext("local-projects.search.placeholder"));
  search->setMaximumWidth(360);
  const auto search_icon = loadAssetPixmap(iconPath("global", "search"), 16);
  if (!search_icon.isNull()) search->addAction(QIcon(search_icon), QLineEdit::LeadingPosition);
  top->addWidget(search);
  top->addStretch();
  auto* membership = new QPushButton(qtext("local-projects.membership"), content);
  membership->setObjectName(QStringLiteral("local-projects-membership"));
  membership->setCursor(Qt::PointingHandCursor);
  top->addWidget(membership);
  auto* avatar = new QPushButton(QStringLiteral("L"), content);
  avatar->setObjectName(QStringLiteral("local-projects-avatar"));
  avatar->setAccessibleName(qtext("local-projects.account"));
  avatar->setFixedSize(32, 32);
  avatar->setCursor(Qt::PointingHandCursor);
  auto* account_menu = new QMenu(avatar);
  account_menu->addAction(qtext("local-projects.account.menu"));
  account_menu->addAction(qtext("local-projects.account.settings"));
  QObject::connect(avatar, &QPushButton::clicked, avatar, [avatar, account_menu] {
    account_menu->exec(avatar->mapToGlobal(QPoint(avatar->width() - account_menu->sizeHint().width(), avatar->height() + 8)));
  });
  top->addSpacing(10);
  top->addWidget(avatar);
  main->addLayout(top);

  auto* header = new QHBoxLayout;
  header->setContentsMargins(0, 16, 0, 18);
  auto* title = text(qtext("page.local-projects"));
  title->setObjectName(QStringLiteral("local-projects-title"));
  header->addWidget(title);
  header->addStretch();
  auto* sort_label = text(qtext("local-projects.sort.label"), true);
  sort_label->setObjectName(QStringLiteral("local-projects-sort-label"));
  header->addWidget(sort_label);
  auto* sort = new QComboBox(content);
  sort->setObjectName(QStringLiteral("local-projects-sort"));
  sort->addItem(qtext("local-projects.sort.last-opened"));
  sort->addItem(qtext("local-projects.sort.created"));
  sort->addItem(qtext("local-projects.sort.name"));
  sort->setMinimumWidth(154);
  header->addWidget(sort);
  main->addLayout(header);

  auto* stack = new QStackedWidget(content);
  auto* grid_scroll = new QScrollArea(stack);
  grid_scroll->setObjectName(QStringLiteral("local-projects-scroll"));
  grid_scroll->setWidgetResizable(true);
  grid_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto* grid = new ProjectGrid;
  grid->setObjectName(QStringLiteral("local-projects-grid"));
  grid_scroll->setWidget(grid);
  stack->addWidget(grid_scroll);
  auto* import_page = buildImportPlaceholder(stack, actions, QString());
  stack->addWidget(import_page);
  stack->setCurrentWidget(grid_scroll);
  main->addWidget(stack, 1);
  auto* compatibility_list = new QListWidget(page);
  compatibility_list->setObjectName(QStringLiteral("local-projects-list"));
  compatibility_list->setVisible(false);

  std::vector<ProjectEntry> projects = actions->projects();
  projects.erase(std::remove_if(projects.begin(), projects.end(),
                                [](const ProjectEntry& project) { return !is_local_project(project); }),
                 projects.end());
  for (const auto& project : projects) compatibility_list->addItem(qs(project.name));
  auto rebuild = [grid, projects](int sort_index) mutable {
    auto ordered = projects;
    if (sort_index == 1) {
      std::sort(ordered.begin(), ordered.end(), [](const ProjectEntry& a, const ProjectEntry& b) { return a.id < b.id; });
    } else if (sort_index == 2) {
      std::sort(ordered.begin(), ordered.end(), [](const ProjectEntry& a, const ProjectEntry& b) { return a.name < b.name; });
    } else {
      std::sort(ordered.begin(), ordered.end(), [](const ProjectEntry& a, const ProjectEntry& b) { return a.id > b.id; });
    }
    std::vector<ProjectCard*> cards;
    cards.push_back(new ProjectCard({}, true, grid));
    for (const auto& project : ordered) cards.push_back(new ProjectCard(project, false, grid));
    grid->setCards(std::move(cards));
  };
  rebuild(0);
  QObject::connect(sort, qOverload<int>(&QComboBox::currentIndexChanged), sort, rebuild);
  QObject::connect(search, &QLineEdit::textChanged, search, [grid](const QString& value) { grid->filter(value); });

  auto show_create_dialog = [page, stack, import_page, actions]() mutable {
    CreateProjectDialog dialog(page);
    if (page->window() != nullptr) {
      const QRect window_rect = page->window()->frameGeometry();
      dialog.move(window_rect.center() - dialog.rect().center());
    }
    if (dialog.exec() != QDialog::Accepted) return;
    const QString project_name = dialog.projectName();
    auto* replacement = buildImportPlaceholder(stack, actions, project_name);
    stack->removeWidget(import_page);
    import_page->deleteLater();
    import_page = replacement;
    stack->addWidget(import_page);
    stack->setCurrentWidget(import_page);
  };
  auto attach_handlers = [grid, show_create_dialog, actions] {
    for (auto* card : grid->cards()) {
      if (card->isCreateCard()) {
        card->onClicked(show_create_dialog);
        continue;
      }
      card->onClicked([grid, card] {
        for (auto* candidate : grid->cards()) candidate->setSelected(candidate == card);
      });
      card->onDoubleClicked([actions, card] { actions->request_open_project(card->project().id); });
      card->setContextMenuPolicy(Qt::CustomContextMenu);
      QObject::connect(card, &QWidget::customContextMenuRequested, card, [card, actions](const QPoint& at) {
        QMenu menu(card);
        auto* remove = menu.addAction(qtext("home.project.delete"));
        if (menu.exec(card->mapToGlobal(at)) == remove && confirmDeleteProject(card->window(), qs(card->project().name))) {
          actions->request_delete_project(card->project().id);
        }
      });
    }
  };
  attach_handlers();
  QObject::connect(sort, qOverload<int>(&QComboBox::currentIndexChanged), sort, [attach_handlers] { attach_handlers(); });
  return page;
}

}  // namespace pixcake::shell
