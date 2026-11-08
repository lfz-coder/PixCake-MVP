#include "preset_dialog.hpp"

#include "pages.hpp"
#include "pixcake_theme.hpp"
#include "pixcake_widgets.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <map>
#include <set>

namespace pixcake::shell {
namespace {

bool askYesNo(QWidget* parent, const QString& message, const QString& yes_text) {
  QMessageBox box(QMessageBox::Question, qtext("trash.confirm.title"), message, QMessageBox::NoButton, parent);
  box.setObjectName(QStringLiteral("preset-confirm"));
  auto* yes = box.addButton(yes_text, QMessageBox::AcceptRole);
  box.addButton(qtext("trash.cancel"), QMessageBox::RejectRole);
  box.setDefaultButton(nullptr);
  box.exec();
  return box.clickedButton() == yes;
}

class SaveDialog final : public QDialog {
 public:
  SaveDialog(QWidget* parent, PageActions* actions) : QDialog(parent), actions_(actions) {
    setObjectName(QStringLiteral("preset-save-dialog"));
    setWindowTitle(qtext("preset.save.title"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(460, 600);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(text(qtext("preset.save.name")));
    name_ = new PixCakeField(qtext("preset.save.name"), this);
    name_->setObjectName(QStringLiteral("preset-name"));
    layout->addWidget(name_);
    layout->addWidget(text(qtext("preset.save.category")));
    category_ = new QComboBox(this);
    category_->setObjectName(QStringLiteral("preset-category"));
    category_->setEditable(true);
    std::set<std::string> categories;
    for (const auto& row : actions_->presets()) categories.insert(row.category);
    category_->addItem(qtext("preset.save.category.default"));
    for (const auto& name : categories) {
      if (category_->findText(qs(name)) < 0) category_->addItem(qs(name));
    }
    layout->addWidget(category_);
    layout->addWidget(text(qtext("preset.save.type")));
    type_ = new QComboBox(this);
    type_->setObjectName(QStringLiteral("preset-type"));
    type_->addItem(qtext("preset.save.type.auto"), QString());
    for (const char* type : {kPresetColor, kPresetAiColor, kPresetPortrait, kPresetCombo}) type_->addItem(qs(type), qs(type));
    layout->addWidget(type_);
    tree_ = new QTreeWidget(this);
    tree_->setObjectName(QStringLiteral("preset-tree"));
    tree_->setHeaderHidden(true);
    std::map<std::string, QTreeWidgetItem*> modules;
    for (const auto& capability : actions_->adjusted_preset_capabilities()) {
      adjusted_.insert(capability.id);
      auto*& parent_item = modules[capability.module];
      if (parent_item == nullptr) {
        parent_item = new QTreeWidgetItem(tree_, QStringList{qs(capability.module)});
        parent_item->setFlags(parent_item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
      }
      auto* item = new QTreeWidgetItem(parent_item, QStringList{qs(capability.label)});
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
      item->setData(0, Qt::UserRole, qs(capability.id));
      item->setCheckState(0, Qt::Checked);
    }
    tree_->expandAll();
    layout->addWidget(tree_, 1);
    auto* picks = new QHBoxLayout;
    for (const auto& [key, state] : {std::pair<const char*, Qt::CheckState>{"preset.save.adjusted", Qt::Checked},
                                     {"preset.save.all", Qt::Checked},
                                     {"preset.save.cleared", Qt::Unchecked}}) {
      auto* button = action(qtext(key), this, true);
      const Qt::CheckState chosen = state;
      connect(button, &QPushButton::clicked, this, [this, chosen] { setAll(chosen); });
      picks->addWidget(button);
    }
    picks->addStretch();
    layout->addLayout(picks);
    layout->addWidget(text(qtext("preset.save.overwrite")));
    overwrite_ = new QComboBox(this);
    overwrite_->setObjectName(QStringLiteral("preset-overwrite"));
    overwrite_->addItem(qtext("preset.save.overwrite.none"), QString());
    for (const auto& row : actions_->presets()) overwrite_->addItem(qs(row.name), qs(row.id));
    layout->addWidget(overwrite_);
    problem_ = text(QString(), true);
    problem_->setObjectName(QStringLiteral("preset-problem"));
    problem_->setWordWrap(true);
    if (adjusted_.empty()) problem_->setText(qtext("preset.save.nothing"));
    layout->addWidget(problem_);
    auto* save = action(qtext("preset.save"), this, !adjusted_.empty());
    save->setObjectName(QStringLiteral("preset-save"));
    layout->addWidget(save, 0, Qt::AlignRight);
    connect(save, &QPushButton::clicked, this, [this] { run(); });
  }

 private:
  void setAll(Qt::CheckState state) {
    for (QTreeWidgetItemIterator it(tree_); *it; ++it) {
      if ((*it)->parent() != nullptr) (*it)->setCheckState(0, state);
    }
  }

  void run() {
    project_session::ProjectSession::PresetDraft draft;
    draft.name = name_->text().toStdString();
    draft.category = category_->currentText().toStdString();
    draft.type = type_->currentData().toString().toStdString();
    draft.overwrite_id = overwrite_->currentData().toString().toStdString();
    for (QTreeWidgetItemIterator it(tree_); *it; ++it) {
      if ((*it)->parent() != nullptr && (*it)->checkState(0) == Qt::Checked) {
        draft.capability_ids.push_back((*it)->data(0, Qt::UserRole).toString().toStdString());
      }
    }
    std::string problem;
    if (actions_->request_save_preset(draft, problem)) {
      accept();
    } else {
      problem_->setText(qtext("preset.save.failed").arg(qs(problem)));
    }
  }

  PageActions* actions_;
  PixCakeField* name_ = nullptr;
  QComboBox* category_ = nullptr;
  QComboBox* type_ = nullptr;
  QComboBox* overwrite_ = nullptr;
  QTreeWidget* tree_ = nullptr;
  QLabel* problem_ = nullptr;
  std::set<std::string> adjusted_;
};

}  // namespace

void openSavePresetDialog(QWidget* parent, PageActions* actions) {
  QWidget* window = parent != nullptr ? parent->window() : nullptr;
  if (window != nullptr) {
    if (auto* open = window->findChild<QDialog*>(QStringLiteral("preset-save-dialog"))) {
      open->raise();
      open->activateWindow();
      return;
    }
  }
  (new SaveDialog(window, actions))->show();
}

bool applyPresetAsking(QWidget* parent, PageActions* actions, const std::string& id, const QString& name) {
  if (actions->preset_overwrites_color(id) &&
      !askYesNo(parent, qtext("preset.overwrite.confirm").arg(name), qtext("preset.apply"))) {
    return false;
  }
  actions->request_apply_preset(id);
  return true;
}

QWidget* buildPresetList(QWidget* parent, PageActions* actions) {
  auto* frame = new QFrame(parent);
  frame->setObjectName(QStringLiteral("refine-preset-labels"));
  auto* layout = new QVBoxLayout(frame);
  layout->setContentsMargins(0, 0, 0, 0);
  auto* title = text(qtext("preset.title"));
  title->setEnabled(false);
  layout->addWidget(title);
  const auto rows = actions->presets();
  if (rows.empty()) {
    auto* empty = text(qtext("preset.empty"), true);
    empty->setEnabled(false);
    empty->setWordWrap(true);
    layout->addWidget(empty);
    return frame;
  }
  auto* list = new QListWidget(frame);
  list->setObjectName(QStringLiteral("refine-preset-list"));
  list->setAccessibleName(qtext("preset.title"));
  for (const auto& row : rows) {
    auto* item = new QListWidgetItem(qs(row.name), list);
    item->setData(Qt::UserRole, qs(row.id));
    item->setToolTip(qtext("preset.tooltip").arg(qs(row.category), qs(row.type)));
  }
  list->setMaximumHeight(160);
  const auto id_of = [](QListWidgetItem* item) { return item->data(Qt::UserRole).toString().toStdString(); };
  QObject::connect(list, &QListWidget::itemActivated, list,
                   [list, actions, id_of](QListWidgetItem* item) { applyPresetAsking(list, actions, id_of(item), item->text()); });
  list->setContextMenuPolicy(Qt::CustomContextMenu);
  QObject::connect(list, &QWidget::customContextMenuRequested, list, [list, actions, id_of](const QPoint& at) {
    QListWidgetItem* item = list->itemAt(at);
    if (item == nullptr) return;
    QMenu menu(list);
    menu.setObjectName(QStringLiteral("preset-menu"));
    QAction* apply = menu.addAction(qtext("preset.apply"));
    QAction* rename = menu.addAction(qtext("preset.rename"));
    QAction* remove = menu.addAction(qtext("preset.delete"));
    QAction* chosen = menu.exec(list->viewport()->mapToGlobal(at));
    const std::string id = id_of(item);
    const QString name = item->text();
    if (chosen == apply) applyPresetAsking(list, actions, id, name);
    if (chosen == rename) {
      bool ok = false;
      const QString renamed = QInputDialog::getText(list, qtext("preset.rename.title"), qtext("preset.rename.label"),
                                                    QLineEdit::Normal, name, &ok);
      if (ok && renamed != name) actions->request_rename_preset(id, renamed.toStdString());
    }
    if (chosen == remove && askYesNo(list, qtext("preset.delete.confirm").arg(name), qtext("preset.delete"))) {
      actions->request_delete_preset(id);
    }
  });
  layout->addWidget(list);
  return frame;
}

}  // namespace pixcake::shell
