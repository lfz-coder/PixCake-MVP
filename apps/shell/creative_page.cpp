#include "pages.hpp"
#include "pixcake_theme.hpp"
#include "pixcake_widgets.hpp"
#include "shell_icons.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLineEdit>
#include <QListWidget>

namespace pixcake::shell {

QWidget* buildCreativePage(const PageContext& context) {
  auto* page = new QWidget;
  page->setObjectName(QStringLiteral("creative-page"));
  auto* outer = new QVBoxLayout(page);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(4);

  auto* top = new QFrame(page);
  top->setObjectName(QStringLiteral("workspace-top"));
  auto* top_layout = new QHBoxLayout(top);
  top_layout->setContentsMargins(8, 6, 8, 6);
  top_layout->addStretch();
  auto* tabs = new QHBoxLayout;
  addPageTabs(tabs, context);
  top_layout->addLayout(tabs);
  top_layout->addStretch();
  for (const auto& entry : ui::shell::page_list("creative-top")) {
    const int size = entry.id == "help" ? 16 : 18;
    auto* button = itemAction("creative-top", entry, top, false, size);
    if (entry.id != "export") showNameOnHover(button, size);
    if (entry.id == "ai") {
      button->setEnabled(context.actions.generation_available() && context.actions.has_photo());
      QObject::connect(button, &QPushButton::clicked, button, [&actions = context.actions] {
        actions.request_generation();
      });
    }
    top_layout->addWidget(button);
  }
  outer->addWidget(top);

  auto* center = new QHBoxLayout;
  center->setSpacing(4);
  auto* tool_panel = new PixCakeToolbar(QStringLiteral("workspace-tools"), page);
  auto* tool_layout = tool_panel->row();
  tool_layout->setSpacing(4);
  for (const auto& entry : ui::shell::page_list("creative-tools")) {
    tool_layout->addWidget(workspaceTool("creative-tools", entry, tool_panel));
  }
  tool_layout->addStretch();

  auto* canvas = new PixCakePanel(QStringLiteral("creative-canvas"), page);
  auto* canvas_layout = new QVBoxLayout(canvas);
  canvas_layout->addWidget(new PixCakeEmptyState(qtext("creative.empty"), qtext("creative.empty.hint"), canvas), 1);
  center->addWidget(canvas, 1);

  auto* side = new QFrame(page);
  side->setObjectName(QStringLiteral("workspace-side"));
  side->setMinimumWidth(PixCakeTheme::kWorkspaceSideWidth);
  auto* side_layout = new QVBoxLayout(side);
  side_layout->setContentsMargins(14, 12, 14, 12);
  for (const char* region : {"region.creative-toolbox", "region.creative-side", "region.creative-layers"}) {
    auto* group = new QGroupBox(qtext(region), side);
    auto* group_layout = new QVBoxLayout(group);
    if (std::string_view(region) == "region.creative-side") {
      for (const auto& entry : ui::shell::page_list("creative-side")) {
        const bool has_icon = !iconPath("creative-side", entry.id).isEmpty();
        auto* button = has_icon ? itemAction("creative-side", entry, group, false, entry.id == "help" ? 16 : 18)
                                : action(qs(entry.text), group);
        group_layout->addWidget(button);
      }
    } else if (std::string_view(region) == "region.creative-toolbox") {
      const auto tools = context.actions.generation_tools();
      if (tools.empty()) {
        group_layout->addWidget(emptyState(qtext("creative.generation.unavailable"),
                                           qtext("creative.generation.unavailable.hint"), group));
      } else {
        auto* tool_list = new QListWidget(group);
        tool_list->setObjectName(QStringLiteral("creative-generation-tools"));
        tool_list->setSelectionMode(QAbstractItemView::SingleSelection);
        for (const auto& tool : tools) {
          auto* item = new QListWidgetItem(qs(tool.title), tool_list);
          item->setData(Qt::UserRole, qs(tool.id));
          item->setToolTip(qs(tool.category));
          item->setSelected(tool.selected);
        }
        QObject::connect(tool_list, &QListWidget::currentRowChanged, tool_list,
                         [&actions = context.actions, tool_list](int row) {
                           if (row < 0 || tool_list->item(row) == nullptr) return;
                           actions.request_generation_tool(
                               tool_list->item(row)->data(Qt::UserRole).toString().toStdString());
                         });
        for (int row = 0; row < tool_list->count(); ++row) {
          if (tool_list->item(row)->isSelected()) {
            tool_list->setCurrentRow(row);
            break;
          }
        }
        group_layout->addWidget(tool_list);
        for (const auto& parameter : context.actions.generation_parameters()) {
          auto* label = text(qs(parameter.title), true);
          label->setObjectName(QStringLiteral("creative-generation-parameter-label-%1").arg(qs(parameter.id)));
          group_layout->addWidget(label);
          if (parameter.type == "choice") {
            auto* selector = new QComboBox(group);
            selector->setObjectName(QStringLiteral("creative-generation-parameter-%1").arg(qs(parameter.id)));
            for (const auto& choice : parameter.choices) selector->addItem(qs(choice));
            const int selected = selector->findText(qs(parameter.value));
            if (selected >= 0) selector->setCurrentIndex(selected);
            QObject::connect(selector, &QComboBox::currentTextChanged, selector,
                             [&actions = context.actions, id = parameter.id](const QString& value) {
                               actions.request_generation_parameter(id, value.toStdString());
                             });
            group_layout->addWidget(selector);
          } else if (parameter.type == "boolean") {
            auto* checkbox = new QCheckBox(qs(parameter.title), group);
            checkbox->setObjectName(QStringLiteral("creative-generation-parameter-%1").arg(qs(parameter.id)));
            checkbox->setChecked(parameter.value == "true");
            QObject::connect(checkbox, &QCheckBox::toggled, checkbox,
                             [&actions = context.actions, id = parameter.id](bool checked) {
                               actions.request_generation_parameter(id, checked ? "true" : "false");
                             });
            group_layout->addWidget(checkbox);
          } else {
            auto* editor = new QLineEdit(qs(parameter.value), group);
            editor->setObjectName(QStringLiteral("creative-generation-parameter-%1").arg(qs(parameter.id)));
            if (parameter.max_length > 0) editor->setMaxLength(static_cast<int>(parameter.max_length));
            editor->setPlaceholderText(qs(parameter.title));
            QObject::connect(editor, &QLineEdit::editingFinished, editor,
                             [&actions = context.actions, id = parameter.id, editor] {
                               actions.request_generation_parameter(id, editor->text().toStdString());
                             });
            group_layout->addWidget(editor);
          }
        }
        auto* run = action(qtext("creative.generation.run"), group, context.actions.has_photo());
        run->setObjectName(QStringLiteral("creative-generation-run"));
        QObject::connect(run, &QPushButton::clicked, run, [&actions = context.actions] {
          actions.request_generation();
        });
        group_layout->addWidget(run);
        if (context.actions.generation_cancel_available()) {
          auto* cancel = action(qtext("creative.generation.cancel"), group, true);
          cancel->setObjectName(QStringLiteral("creative-generation-cancel"));
          QObject::connect(cancel, &QPushButton::clicked, cancel, [&actions = context.actions] {
            actions.request_generation_cancel();
          });
          group_layout->addWidget(cancel);
        }
        const QString status = qs(context.actions.generation_status());
        auto* status_label = text(status.isEmpty() ? qtext("creative.generation.idle") : status, true);
        status_label->setObjectName(QStringLiteral("creative-generation-status"));
        group_layout->addWidget(status_label);
        if (context.actions.generation_retry_available()) {
          auto* retry = action(qtext("creative.generation.retry"), group, true);
          retry->setObjectName(QStringLiteral("creative-generation-retry"));
          QObject::connect(retry, &QPushButton::clicked, retry, [&actions = context.actions] {
            actions.request_generation_retry();
          });
          group_layout->addWidget(retry);
        }
        const auto history = context.actions.generation_history();
        if (!history.empty()) {
          auto* recent = new QListWidget(group);
          recent->setObjectName(QStringLiteral("creative-generation-history"));
          recent->setSelectionMode(QAbstractItemView::NoSelection);
          for (const auto& entry : history) {
            const QString result_count = QString::number(static_cast<qulonglong>(entry.result_count));
            recent->addItem(qs(entry.task_id) + qtext("creative.generation.task.separator") + qs(entry.tool_id) +
                            qtext("creative.generation.task.separator") + qs(entry.status) +
                            qtext("creative.generation.task.separator") + result_count);
          }
          group_layout->addWidget(recent);
        }
        if (context.actions.generation_apply_available()) {
          auto* apply = action(qtext("creative.generation.apply"), group, true);
          apply->setObjectName(QStringLiteral("creative-generation-apply"));
          QObject::connect(apply, &QPushButton::clicked, apply, [&actions = context.actions] {
            actions.request_generation_apply();
          });
          group_layout->addWidget(apply);
        }
      }
    } else {
      group_layout->addWidget(emptyState(qtext("common.empty"), QString(), group));
    }
    side_layout->addWidget(group);
  }
  side_layout->addStretch();
  center->addWidget(side);
  outer->insertWidget(1, tool_panel);
  outer->addLayout(center, 1);
  outer->addWidget(thumbnailStrip(page, false));
  return page;
}

}  // namespace pixcake::shell
