#include "pages.hpp"

#include <QDateTime>
#include <QComboBox>
#include <QHeaderView>
#include <QProgressBar>
#include <QTableWidget>
#include <QTimer>

#include <algorithm>

namespace pixcake::shell {
QWidget* buildCloudPage(const PageContext& context) {
  auto* page = new QWidget;
  page->setObjectName(QStringLiteral("cloud-page"));
  auto* layout = new QVBoxLayout(page);
  layout->setContentsMargins(26, 24, 26, 24);
  layout->setSpacing(14);
  const auto view = context.actions.cloud_view();
  auto* heading = new QHBoxLayout;
  auto* title = text(qtext("page.cloud"));
  title->setObjectName(QStringLiteral("cloud-heading"));
  auto font = title->font(); font.setPointSize(21); font.setBold(true); title->setFont(font);
  heading->addWidget(title);
  heading->addStretch();
  auto* refresh = action(qtext("home.projects.refresh"), page);
  refresh->setObjectName(QStringLiteral("cloud-refresh"));
  refresh->setEnabled(!view.busy && view.state != ui::shell::CloudState::NotConfigured &&
                      view.state != ui::shell::CloudState::SignedOut);
  auto* actions = &context.actions;
  QObject::connect(refresh, &QPushButton::clicked, refresh, [actions, selected = view.selected_space] {
    actions->request_cloud_refresh(selected);
  });
  heading->addWidget(refresh);
  layout->addLayout(heading);
  auto* status = text(qs(ui::shell::cloud_status_text(view)));
  status->setObjectName(QStringLiteral("cloud-status"));
  status->setWordWrap(true);
  layout->addWidget(status);
  auto* transfers = new QHBoxLayout;
  auto* local = new QComboBox(page); local->setObjectName(QStringLiteral("cloud-local-project"));
  local->setAccessibleName(qtext("cloud.sync.local"));
  local->setPlaceholderText(qtext("cloud.sync.local"));
  local->setToolTip(qtext("cloud.sync.local"));
  for (const auto& project : context.actions.projects()) local->addItem(qs(project.name), QVariant::fromValue<qlonglong>(project.id));
  transfers->addWidget(local, 1);
  auto* destination = new QComboBox(page); destination->setObjectName(QStringLiteral("cloud-upload-space"));
  destination->setAccessibleName(qtext("cloud.sync.destination"));
  destination->setPlaceholderText(qtext("cloud.sync.destination"));
  destination->setToolTip(qtext("cloud.sync.destination"));
  destination->setMinimumWidth(200);
  for (const auto& space : view.spaces) {
    if (std::find(space.permissions.begin(), space.permissions.end(), cloud::Permission::Write) != space.permissions.end())
      destination->addItem(qs(space.name), qs(space.id));
  }
  const int selected = destination->findData(qs(view.selected_space));
  if (selected >= 0) destination->setCurrentIndex(selected);
  transfers->addWidget(destination);
  auto* upload = action(qtext("cloud.sync.upload"), page); upload->setObjectName(QStringLiteral("cloud-upload"));
  transfers->addWidget(upload);
  QObject::connect(upload, &QPushButton::clicked, upload, [actions, local, destination] {
    actions->request_cloud_upload(local->currentData().toLongLong(), destination->currentData().toString().toStdString());
  });
  layout->addLayout(transfers);
  auto* content = new QHBoxLayout;
  content->setSpacing(18);
  auto* spaces = new QListWidget(page);
  spaces->setObjectName(QStringLiteral("cloud-spaces"));
  spaces->setAccessibleName(qtext("region.cloud-spaces"));
  spaces->setMinimumWidth(220);
  spaces->setMaximumWidth(320);
  spaces->setEnabled(!view.busy);
  for (const auto& space : view.spaces) {
    const auto kind = qtext(space.kind == cloud::SpaceKind::Personal ? "cloud.personal" : "cloud.team");
    auto* row = new QListWidgetItem(qs(space.name) + QStringLiteral("\n") + kind, spaces);
    row->setData(Qt::UserRole, qs(space.id));
    row->setSizeHint(QSize(220, 62));
    const bool readable = std::find(space.permissions.begin(), space.permissions.end(), cloud::Permission::Read)
                          != space.permissions.end();
    if (!readable) { row->setFlags(Qt::NoItemFlags); row->setToolTip(qtext("cloud.no-read")); }
    if (space.id == view.selected_space) spaces->setCurrentItem(row);
  }
  QObject::connect(spaces, &QListWidget::itemClicked, spaces, [actions](QListWidgetItem* row) {
    if (row->flags() & Qt::ItemIsEnabled) actions->request_cloud_refresh(row->data(Qt::UserRole).toString().toStdString());
  });
  content->addWidget(spaces);
  auto* projects = new QTableWidget(static_cast<int>(view.projects.size()), 4, page);
  projects->setObjectName(QStringLiteral("cloud-projects"));
  projects->setAccessibleName(qtext("region.cloud-projects"));
  projects->setHorizontalHeaderLabels({qtext("cloud.project-name"), qtext("cloud.revision"),
                                       qtext("cloud.updated"), qtext("cloud.bytes")});
  projects->setEditTriggers(QAbstractItemView::NoEditTriggers);
  projects->setSelectionBehavior(QAbstractItemView::SelectRows);
  projects->verticalHeader()->hide();
  projects->verticalHeader()->setDefaultSectionSize(44);
  projects->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  for (int column = 1; column < 4; ++column) projects->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
  for (std::size_t index = 0; index < view.projects.size(); ++index) {
    const auto& project = view.projects[index];
    const auto date = QDateTime::fromSecsSinceEpoch(project.updated_at);
    const QStringList values{qs(project.name), QString::number(project.revision),
                             date.isValid() ? date.toString(QStringLiteral("yyyy-MM-dd HH:mm")) : qtext("cloud.unknown-time"),
                             QString::number(project.size_bytes)};
    for (int column = 0; column < 4; ++column)
      projects->setItem(static_cast<int>(index), column, new QTableWidgetItem(values[column]));
    projects->item(static_cast<int>(index), 0)->setData(Qt::UserRole, qs(project.id));
    projects->item(static_cast<int>(index), 0)->setData(Qt::UserRole + 1, QVariant::fromValue<qlonglong>(project.revision));
  }
  content->addWidget(projects, 1);
  layout->addLayout(content, 1);
  auto* controls = new QHBoxLayout;
  auto* download = action(qtext("cloud.sync.download"), page); download->setObjectName(QStringLiteral("cloud-download"));
  auto* resume = action(qtext("cloud.sync.retry"), page); resume->setObjectName(QStringLiteral("cloud-sync-resume"));
  auto* pause = action(qtext("cloud.sync.pause"), page); pause->setObjectName(QStringLiteral("cloud-sync-pause"));
  auto* keep = action(qtext("cloud.sync.keep-both"), page); keep->setObjectName(QStringLiteral("cloud-sync-keep-both"));
  auto* discard = action(qtext("cloud.sync.discard"), page); discard->setObjectName(QStringLiteral("cloud-sync-discard"));
  for (auto* button : {download, resume, pause, keep, discard}) controls->addWidget(button);
  controls->addStretch(); layout->addLayout(controls);
  QObject::connect(download, &QPushButton::clicked, download, [actions, projects] {
    const auto* item = projects->item(projects->currentRow(), 0);
    if (item) actions->request_cloud_download(item->data(Qt::UserRole).toString().toStdString());
  });
  QObject::connect(resume, &QPushButton::clicked, resume, [actions] { actions->request_cloud_transfer_retry(); });
  QObject::connect(pause, &QPushButton::clicked, pause, [actions] { actions->request_cloud_transfer_pause(); });
  QObject::connect(keep, &QPushButton::clicked, keep, [actions] { actions->request_cloud_transfer_retry(true); });
  QObject::connect(discard, &QPushButton::clicked, discard, [actions] { actions->request_cloud_transfer_discard(); });
  auto* progress = new QProgressBar(page); progress->setObjectName(QStringLiteral("cloud-sync-progress"));
  layout->addWidget(progress);
  auto* hint = text(qtext("cloud.sync.ready"), true); hint->setObjectName(QStringLiteral("cloud-sync-status"));
  hint->setWordWrap(true);
  layout->addWidget(hint);
  const auto update = [=] {
    const auto transfer = actions->cloud_transfer_view();
    const bool available = transfer.configured && !transfer.busy && !transfer.pending;
    upload->setEnabled(available && local->count() > 0 && destination->count() > 0);
    local->setEnabled(!transfer.busy); destination->setEnabled(!transfer.busy);
    const auto* item = projects->item(projects->currentRow(), 0);
    download->setEnabled(available && item && item->data(Qt::UserRole + 1).toLongLong() > 0);
    resume->setEnabled(transfer.pending && !transfer.busy);
    pause->setEnabled(transfer.busy && transfer.phase != cloud_sync::Phase::Paused);
    discard->setEnabled(transfer.pending && !transfer.busy);
    keep->setVisible(transfer.phase == cloud_sync::Phase::Conflict); keep->setEnabled(!transfer.busy);
    progress->setVisible(transfer.busy);
    progress->setRange(0, transfer.total ? 1000 : 0);
    if (transfer.total) progress->setValue(static_cast<int>(std::min<std::uint64_t>(1000, transfer.done * 1000 / transfer.total)));
    hint->setText(qs(ui::shell::cloud_transfer_text(transfer)));
  };
  auto* timer = new QTimer(page); timer->setInterval(100);
  QObject::connect(timer, &QTimer::timeout, page, update); timer->start(); update();
  return page;
}
}  // namespace pixcake::shell
