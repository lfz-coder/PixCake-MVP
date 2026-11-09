#include "trash_dialog.hpp"

#include "pages.hpp"
#include "pixcake_theme.hpp"
#include "pixcake_widgets.hpp"

#include <QDialog>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QTableWidget>
#include <QVBoxLayout>

namespace pixcake::shell {
namespace {

bool ask(QWidget* parent, const QString& message, const QString& confirm_text) {
  QMessageBox box(QMessageBox::Question, qtext("trash.confirm.title"), message, QMessageBox::NoButton, parent);
  box.setObjectName(QStringLiteral("confirm-delete"));
  auto* yes = box.addButton(confirm_text, QMessageBox::AcceptRole);
  box.addButton(qtext("trash.cancel"), QMessageBox::RejectRole);
  box.setDefaultButton(nullptr);
  box.exec();
  return box.clickedButton() == yes;
}

QString kindText(TrashKind kind) {
  switch (kind) {
    case TrashKind::Project: return qtext("trash.kind.project");
    case TrashKind::Photo: return qtext("trash.kind.photo");
    case TrashKind::Copy: return qtext("trash.kind.copy");
  }
  return {};
}

class TrashDialog final : public QDialog {
 public:
  TrashDialog(QWidget* parent, PageActions* actions) : QDialog(parent), actions_(actions) {
    setObjectName(QStringLiteral("trash-dialog"));
    setWindowTitle(qtext("trash.title"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(760, 480);
    auto* layout = new QVBoxLayout(this);
    count_ = text(QString(), true);
    count_->setObjectName(QStringLiteral("trash-count"));
    layout->addWidget(count_);
    table_ = new QTableWidget(this);
    table_->setObjectName(QStringLiteral("trash-table"));
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels({qtext("trash.col.name"), qtext("trash.col.kind"), qtext("trash.col.project"),
                                       qtext("trash.col.left")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table_, &QWidget::customContextMenuRequested, this, [this](const QPoint& at) {
      const int row = table_->rowAt(at.y());
      if (row < 0) return;
      table_->selectRow(row);
      QMenu menu(this);
      menu.setObjectName(QStringLiteral("trash-menu"));
      QAction* restore = menu.addAction(qtext("trash.restore"));
      restore->setObjectName(QStringLiteral("trash-menu-restore"));
      QAction* erase = menu.addAction(qtext("trash.erase"));
      erase->setObjectName(QStringLiteral("trash-menu-erase"));
      erase->setEnabled(items_[static_cast<std::size_t>(row)].kind == TrashKind::Project);
      QAction* chosen = menu.exec(table_->viewport()->mapToGlobal(at));
      if (chosen == restore) restoreSelected();
      if (chosen == erase) eraseSelected();
    });
    layout->addWidget(table_, 1);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    clear_ = action(qtext("trash.clear"), this);
    clear_->setObjectName(QStringLiteral("trash-clear"));
    buttons->addWidget(clear_);
    connect(clear_, &QPushButton::clicked, this, [this] { clearAll(); });
    restore_ = action(qtext("trash.restore"), this, true);
    restore_->setObjectName(QStringLiteral("trash-restore"));
    erase_ = action(qtext("trash.erase"), this);
    erase_->setObjectName(QStringLiteral("trash-erase"));
    buttons->addWidget(erase_);
    buttons->addWidget(restore_);
    layout->addLayout(buttons);
    connect(restore_, &QPushButton::clicked, this, [this] { restoreSelected(); });
    connect(erase_, &QPushButton::clicked, this, [this] { eraseSelected(); });
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] { updateButtons(); });
    reload();
  }

 private:
  int selectedRow() const {
    const auto rows = table_->selectionModel()->selectedRows();
    return rows.isEmpty() ? -1 : rows.front().row();
  }

  void updateButtons() {
    const int row = selectedRow();
    restore_->setEnabled(row >= 0);
    bool any_project = false;
    for (const auto& item : items_) any_project = any_project || item.kind == TrashKind::Project;
    clear_->setEnabled(any_project);
    erase_->setEnabled(row >= 0 && items_[static_cast<std::size_t>(row)].kind == TrashKind::Project);
  }

  // 清空回收站：只彻底删除回收站里的项目（需求 §7）；单独进回收站的图片和副本不动，到期自动清。
  void clearAll() {
    std::vector<std::int64_t> projects;
    for (const auto& item : items_) {
      if (item.kind == TrashKind::Project) projects.push_back(item.id);
    }
    if (projects.empty()) return;
    if (!ask(this, qtext("trash.clear.confirm").arg(projects.size()), qtext("trash.erase"))) return;
    for (const auto id : projects) actions_->request_erase_project(id);
    reload();
  }

  void reload() {
    items_ = actions_->trash();
    table_->setRowCount(static_cast<int>(items_.size()));
    for (int row = 0; row < static_cast<int>(items_.size()); ++row) {
      const auto& item = items_[static_cast<std::size_t>(row)];
      QString name = qs(item.name);
      if (item.copy > 0) name = qtext("trash.name.copy").arg(name).arg(item.copy);
      table_->setItem(row, 0, new QTableWidgetItem(name));
      table_->setItem(row, 1, new QTableWidgetItem(kindText(item.kind)));
      table_->setItem(row, 2, new QTableWidgetItem(item.kind == TrashKind::Project ? QString() : qs(item.project)));
      table_->setItem(row, 3, new QTableWidgetItem(qtext("trash.left").arg(item.days_left)));
    }
    count_->setText(items_.empty() ? qtext("trash.empty") : qtext("trash.count").arg(items_.size()));
    updateButtons();
  }

  void restoreSelected() {
    const int row = selectedRow();
    if (row < 0) return;
    const auto item = items_[static_cast<std::size_t>(row)];
    actions_->request_restore(item.kind, item.id);
    reload();
  }

  void eraseSelected() {
    const int row = selectedRow();
    if (row < 0) return;
    const auto item = items_[static_cast<std::size_t>(row)];
    if (item.kind != TrashKind::Project || !confirmErase(item)) return;
    actions_->request_erase_project(item.id);
    reload();
  }

  bool confirmErase(const TrashItem& item) {
    return ask(this, qtext("trash.erase.confirm").arg(qs(item.name)), qtext("trash.erase"));
  }

  PageActions* actions_;
  std::vector<TrashItem> items_;
  QLabel* count_ = nullptr;
  QTableWidget* table_ = nullptr;
  QPushButton* restore_ = nullptr;
  QPushButton* erase_ = nullptr;
  QPushButton* clear_ = nullptr;
};

}  // namespace

bool confirmDeletePhotos(QWidget* parent, std::size_t count) {
  return ask(parent, count == 1 ? qtext("trash.confirm.photo") : qtext("trash.confirm.photos").arg(count),
             qtext("trash.confirm.ok"));
}

bool confirmDeleteProject(QWidget* parent, const QString& name) {
  return ask(parent, qtext("trash.confirm.project").arg(name), qtext("trash.confirm.ok"));
}

void openTrashDialog(QWidget* parent, PageActions* actions) {
  QWidget* window = parent != nullptr ? parent->window() : nullptr;
  if (window != nullptr) {
    if (auto* open = window->findChild<QDialog*>(QStringLiteral("trash-dialog"))) {
      open->raise();
      open->activateWindow();
      return;
    }
  }
  auto* dialog = new TrashDialog(window, actions);
  dialog->show();
}

}  // namespace pixcake::shell
