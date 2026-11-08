#include "sync_dialog.hpp"

#include "pages.hpp"
#include "pixcake_theme.hpp"
#include "pixcake_widgets.hpp"

#include <QDialog>
#include <QHeaderView>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <map>

namespace pixcake::shell {
namespace {

class SyncDialog final : public QDialog {
 public:
  SyncDialog(QWidget* parent, PageActions* actions) : QDialog(parent), actions_(actions) {
    setObjectName(QStringLiteral("sync-dialog"));
    setWindowTitle(qtext("sync.title"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(520, 560);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(text(qtext("sync.hint"), true));
    tree_ = new QTreeWidget(this);
    tree_->setObjectName(QStringLiteral("sync-tree"));
    tree_->setHeaderHidden(true);
    std::map<std::string, QTreeWidgetItem*> modules;
    for (const auto& entry : actions_->sync_items()) {
      auto& parent_item = modules[entry.module_label];
      if (parent_item == nullptr) {
        parent_item = new QTreeWidgetItem(tree_, QStringList{qs(entry.module_label)});
        parent_item->setFlags(parent_item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
        parent_item->setCheckState(0, Qt::Unchecked);
      }
      auto* item = new QTreeWidgetItem(parent_item, QStringList{qs(entry.label)});
      item->setData(0, Qt::UserRole, qs(entry.id));
      if (entry.enabled) {
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, Qt::Checked);
      } else {
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        item->setToolTip(0, qs(entry.blocked_reason));
        item->setText(0, qtext("sync.blocked").arg(qs(entry.label), qs(entry.blocked_reason)));
      }
    }
    tree_->expandAll();
    layout->addWidget(tree_, 1);
    auto* bottom = new QHBoxLayout;
    auto* count = text(qtext("sync.targets").arg(actions_->sync_target_count()), true);
    count->setObjectName(QStringLiteral("sync-targets"));
    result_ = text(QString(), true);
    result_->setObjectName(QStringLiteral("sync-result"));
    bottom->addWidget(count);
    bottom->addWidget(result_, 1);
    auto* all = action(qtext("sync.all"), this, true);
    all->setObjectName(QStringLiteral("sync-all"));
    auto* none = action(qtext("sync.none"), this, true);
    none->setObjectName(QStringLiteral("sync-none"));
    save_ = action(qtext("sync.save"), this, actions_->sync_target_count() > 0);
    save_->setObjectName(QStringLiteral("sync-save"));
    bottom->addWidget(all);
    bottom->addWidget(none);
    bottom->addWidget(save_);
    layout->addLayout(bottom);
    connect(all, &QPushButton::clicked, this, [this] { setAll(Qt::Checked); });
    connect(none, &QPushButton::clicked, this, [this] { setAll(Qt::Unchecked); });
    connect(save_, &QPushButton::clicked, this, [this] { run(); });
  }

 private:
  void setAll(Qt::CheckState state) {
    for (QTreeWidgetItemIterator it(tree_); *it; ++it) {
      if ((*it)->parent() != nullptr && ((*it)->flags() & Qt::ItemIsUserCheckable)) (*it)->setCheckState(0, state);
    }
  }

  void run() {
    std::vector<std::string> ids;
    for (QTreeWidgetItemIterator it(tree_); *it; ++it) {
      if ((*it)->parent() != nullptr && ((*it)->flags() & Qt::ItemIsUserCheckable) && (*it)->checkState(0) == Qt::Checked) {
        ids.push_back((*it)->data(0, Qt::UserRole).toString().toStdString());
      }
    }
    if (ids.empty()) {
      result_->setText(qtext("sync.none.checked"));
      return;
    }
    const std::size_t failed = actions_->request_sync(ids);
    if (failed == 0) {
      accept();
    } else {
      result_->setText(qtext("sync.failed").arg(failed));
    }
  }

  PageActions* actions_;
  QTreeWidget* tree_ = nullptr;
  QLabel* result_ = nullptr;
  QPushButton* save_ = nullptr;
};

}  // namespace

void openSyncDialog(QWidget* parent, PageActions* actions) {
  QWidget* window = parent != nullptr ? parent->window() : nullptr;
  if (window != nullptr) {
    if (auto* open = window->findChild<QDialog*>(QStringLiteral("sync-dialog"))) {
      open->raise();
      open->activateWindow();
      return;
    }
  }
  (new SyncDialog(window, actions))->show();
}

}  // namespace pixcake::shell
