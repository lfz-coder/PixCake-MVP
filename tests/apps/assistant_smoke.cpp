#include "assistant_smoke.hpp"
#include "shell_controller.hpp"
#include "assistant_gateway_server.hpp"
#include "pages.hpp"
#include "pixcake/account/account_session.hpp"
#include "pixcake/platform/mac_source_access.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QPushButton>
#include <QListWidget>
#include <QLineEdit>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>

namespace pixcake::shell {
namespace {

bool check(bool value, const char* stage) {
  if (!value) qWarning("assistant generation smoke failed: %s", stage);
  return value;
}

bool until(const std::function<bool()>& done) {
  QElapsedTimer timer;
  timer.start();
  while (!done() && timer.elapsed() < 10000) {
    QCoreApplication::processEvents();
    QThread::msleep(2);
  }
  return done();
}

bool click_confirmation(ShellController& controller, const std::string& id, bool approved) {
  std::unique_ptr<QWidget> page(buildRefinePage({controller.display(), controller}));
  for (auto* card : page->findChildren<QFrame*>(QStringLiteral("assistant-confirmation-card"))) {
    if (card->property("approvalId").toString() != QString::fromStdString(id)) continue;
    auto* button = card->findChild<QPushButton*>(approved ? QStringLiteral("assistant-confirm") : QStringLiteral("assistant-reject"));
    if (!button || !button->isEnabled()) return false;
    button->click();
    return true;
  }
  return false;
}

bool smoke_home_interactions() {
  struct Actions : PageActions {
    void request_page(Page) override {}
    std::vector<ProjectEntry> values = {{1, "Zulu", kOriginLocal, false}, {2, "Alpha", kOriginLocal, false}};
    std::vector<ProjectEntry> projects() const override { return values; }
  } actions;
  const auto display = ShellDisplay::dev_preview(Page::Home);
  std::unique_ptr<QWidget> page(buildHomePage({display, actions}));
  auto* list = page->findChild<QListWidget*>(QStringLiteral("home-projects"));
  auto* sort = page->findChild<QPushButton*>(QStringLiteral("home-project-sort"));
  auto* refresh = page->findChild<QPushButton*>(QStringLiteral("home-project-refresh"));
  auto* search = page->findChild<QLineEdit*>(QStringLiteral("home-search"));
  const auto tabs = page->findChildren<QPushButton*>(QStringLiteral("home-project-tab"));
  if (!page->layout() || !list || !sort || !refresh || !search || tabs.size() != 3) return false;
  sort->click();
  sort->click();
  if (list->count() != 2 || list->item(0)->text() != QStringLiteral("Alpha")) return false;
  tabs[2]->click();
  if (list->item(0)->data(Qt::UserRole).isValid()) return false;
  tabs[0]->click();
  if (list->count() != 2) return false;
  actions.values.push_back({3, "Beta", kOriginLocal, false});
  refresh->click();
  if (list->count() != 3) return false;
  search->setText(QStringLiteral("Beta"));
  return list->count() == 1 && list->item(0)->data(Qt::UserRole).toLongLong() == 3;
}

QJsonObject wire_call(const QString& path, const QJsonObject& request, const QString& command, const QJsonObject& payload) {
  QLocalSocket socket;
  socket.connectToServer(path);
  if (!until([&] { return socket.state() == QLocalSocket::ConnectedState; })) return {};
  const auto body = QJsonDocument(QJsonObject{{"request", request}, {"command", command}, {"payload", payload}}).toJson();
  QByteArray frame(4, 0);
  qToLittleEndian<quint32>(body.size(), frame.data());
  frame.append(body);
  socket.write(frame);
  QByteArray response;
  if (!until([&] {
        response.append(socket.readAll());
        return response.size() >= 4 && response.size() >= 4 + qFromLittleEndian<quint32>(response.constData());
      })) return {};
  return QJsonDocument::fromJson(response.mid(4)).object();
}

}  // namespace

bool smoke_assistant_ui() {
  if (!check(smoke_home_interactions(), "home widget interaction")) return false;
  struct Actions : PageActions {
    std::vector<std::pair<QString, bool>> decisions;
    void request_page(Page) override {}
    bool assistant_visible() const override { return true; }
    std::vector<AssistantConfirmationCard> assistant_confirmations() const override {
      return {{QStringLiteral("approve"), QStringLiteral("Apply"), QStringLiteral("<b>untrusted parameter</b>"), true},
              {QStringLiteral("reject"), QStringLiteral("Undo"), QStringLiteral("Original action"), true},
              {QStringLiteral("no-quote"), QStringLiteral("Generate"), QStringLiteral("No quote"), false}};
    }
    void request_assistant_resolve(const QString& id, bool approved) override { decisions.emplace_back(id, approved); }
  } actions;
  const auto display = ShellDisplay::dev_preview(Page::Refine);
  std::unique_ptr<QWidget> page(buildRefinePage({display, actions}));
  const auto cards = page->findChildren<QFrame*>(QStringLiteral("assistant-confirmation-card"));
  if (!check(cards.size() == 3, "three pending cards rendered")) return false;
  for (auto* card : cards) {
    auto* confirm = card->findChild<QPushButton*>(QStringLiteral("assistant-confirm"));
    auto* reject = card->findChild<QPushButton*>(QStringLiteral("assistant-reject"));
    const auto id = card->property("approvalId").toString();
    if (!confirm || !reject || !reject->isEnabled()) return false;
    for (auto* label : card->findChildren<QLabel*>()) {
      if (!check(label->textFormat() == Qt::PlainText, "card content cannot inject rich text")) return false;
    }
    if (id == QStringLiteral("approve")) {
      confirm->click();
      confirm->click();
      if (!check(!confirm->isEnabled() && !reject->isEnabled(), "buttons disabled after decision")) return false;
    } else {
      if (id == QStringLiteral("no-quote") && !check(!confirm->isEnabled(), "generation requires a quote")) return false;
      reject->click();
    }
  }
  return check(actions.decisions.size() == 3 && actions.decisions[0].second &&
               !actions.decisions[1].second && !actions.decisions[2].second, "UI forwards each decision exactly once");
}

bool smoke_assistant_generation(const std::string& renderd, const std::string& generated) {
  if (!check(smoke_home_interactions(), "home callbacks after page construction")) return false;
  QTemporaryDir directory;
  if (!directory.isValid()) return false;
  const std::string root = directory.path().toStdString();
  const QString photo_path = directory.path() + QStringLiteral("/photo.png");
  QImage photo(16, 16, QImage::Format_RGBA8888);
  photo.fill(Qt::gray);
  if (!photo.save(photo_path)) return false;

  account::AccountSession account;
  account::AccountSnapshot account_snapshot;
  account_snapshot.account_id = "assistant-smoke";
  account_snapshot.session_handle = "fixture-session";
  if (account.sign_in(account_snapshot) != ResultCode::Ok) return false;
  platform::MacSourceAccess access;
  std::unique_ptr<project_session::ProjectSession> projects;
  std::string diagnostic;
  QDir().mkpath(directory.path() + QStringLiteral("/data"));
  if (project_session::ProjectSession::open(root + "/data", account, access,
      &platform::MacSourceAccess::create_bookmark, projects, diagnostic) != ResultCode::Ok) return false;
  auto* project = projects.get();
  ShellController controller(ShellDisplay::dev_preview(Page::Home));
  preview_channel::PreviewSessionOptions preview;
  preview.renderd_executable = renderd;
  preview.log_directory = root + "/logs";
  controller.attach_projects(std::move(projects), preview);
  generation_session::GenerationSessionOptions options;
  options.process.executable = generated;
  options.process.data_directory = root + "/generation";
  options.process.log_directory = root + "/logs";
  options.process.fixture = true;
  controller.attach_generation(account, options);
  controller.request_import({QFileInfo(photo_path).canonicalFilePath().toStdString()});
  if (!check(controller.has_photo() && controller.current_photo().has_value(), "open isolated photo")) {
    qWarning("preview diagnostic: %s", qPrintable(controller.preview_view().status));
    for (const auto& notice : controller.display().notices()) qWarning("import diagnostic: %s", notice.c_str());
    return false;
  }

  const auto photo_id = *controller.current_photo();
  const std::string layer_path = project->layer_document_path(photo_id);
  assistant::AssistantRequest request;
  request.request_id = "request-generation";
  request.thread_id = "thread-generation";
  request.message = "fixture generation";
  request.image.image_id = project->image_id(photo_id);
  auto& gateway = controller.assistant_gateway();
  request.base_revision = gateway.read_edit_context(request).revision;
  request.image.editing_instance_id = gateway.read_edit_context(request).editing_instance_id;
  assistant::AssistantGenerationRequest task;
  task.task_id = "generation-1";
  task.tool_id = generate::kDevelopmentFixtureToolId;
  task.image_id = request.image.image_id;
  task.source_revision = request.base_revision;
  PosixFileSystem files;
  int heartbeats = 0;
  QTimer heartbeat;
  QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++heartbeats; });
  heartbeat.start(1);
  AssistantGatewayServer server(gateway);
  QString socket_diagnostic;
  const auto socket_path = directory.path() + QStringLiteral("/assistant.sock");
  if (!check(server.start(socket_path, socket_diagnostic), "local JSON gateway started")) return false;
  const QJsonObject wire_request{{"request_id", qs(request.request_id)}, {"thread_id", qs(request.thread_id)},
                                {"message", qs(request.message)}, {"base_revision", static_cast<qint64>(request.base_revision)},
                                {"image", QJsonObject{{"image_id", qs(request.image.image_id)},
                                                      {"editing_instance_id", qs(request.image.editing_instance_id)}}}};
  const auto forged = wire_call(socket_path, wire_request, QStringLiteral("run_generation"),
      {{"task_id", qs(task.task_id)}, {"tool_id", qs(task.tool_id)}, {"image_id", qs(task.image_id)},
       {"source_revision", static_cast<qint64>(task.source_revision)}, {"user_confirmed", true}});
  const auto pending = gateway.run_generation(request, task, true);
  if (!check(!forged.isEmpty() && !forged.value("ok").toBool() && pending.approval_status == "pending" &&
             !controller.assistant_busy() && !files.exists(layer_path), "wire confirmation flag cannot start generation")) return false;
  const auto bypass = wire_call(socket_path, wire_request, QStringLiteral("resolve_approval"),
                               {{"approval_id", qs(pending.approval_id)}, {"approved", true}});
  if (!check(!bypass.isEmpty() && !bypass.value("ok").toBool() && !controller.assistant_busy(),
             "approval is not a wire command")) return false;
  if (!check(click_confirmation(controller, pending.approval_id, true), "desktop generation confirmation button")) return false;
  const auto queued = gateway.approval_status(request, pending.approval_id);
  if (!check(queued.code == ResultCode::Ok && queued.task_status == "queued" && controller.assistant_busy() &&
             !files.exists(layer_path), "queue returns before layer placement")) return false;
  if (!check(gateway.run_generation(request, task, true).code == ResultCode::Ok, "idempotent submit")) return false;
  if (!check(until([&] { return !controller.assistant_busy(); }) && controller.generation_status() == "succeeded" &&
             heartbeats > 0, "completion leaves event loop responsive")) return false;
  LayerDocument layers;
  const auto loaded = LayerDocument::load(layers, files, layer_path);
  if (!check(loaded.code == ResultCode::Ok && layers.generations().size() == 1, "one verified result placed")) return false;
  std::vector<std::uint8_t> before;
  if (!files.read_file(layer_path, before).ok) return false;

  task.task_id = "generation-2";
  {
    const auto approval = gateway.run_generation(request, task, true);
    if (!check(click_confirmation(controller, approval.approval_id, true) &&
               gateway.approval_status(request, approval.approval_id).code == ResultCode::Ok, "second task queued")) return false;
  }
  controller.request_parameter({"color.global", "exposure"}, "0.50", true);
  if (!check(gateway.read_edit_context(request).revision > request.base_revision, "concurrent edit committed")) return false;
  if (!check(until([&] { return !controller.assistant_busy(); }) &&
             controller.generation_status() == "succeeded-source-changed", "stale result withheld")) return false;
  std::vector<std::uint8_t> after;
  if (!check(files.read_file(layer_path, after).ok && before == after, "stale task did not change layers")) return false;

  request.base_revision = gateway.read_edit_context(request).revision;
  task.source_revision = request.base_revision;
  task.task_id = "generation-3";
  {
    const auto approval = gateway.run_generation(request, task, true);
    if (!check(click_confirmation(controller, approval.approval_id, true) &&
               gateway.approval_status(request, approval.approval_id).code == ResultCode::Ok, "cancel task queued")) return false;
  }
  const auto cancelled = gateway.cancel_generation(request, {task.task_id});
  if (!check(cancelled.code == ResultCode::Ok && cancelled.task_status == "cancelling", "cancel accepted")) return false;
  if (!check(until([&] { return !controller.assistant_busy(); }) &&
      (controller.generation_status() == "cancelled" || controller.generation_status() == "succeeded-not-applied"),
      "cancelled result never applied")) return false;
  if (!check(files.read_file(layer_path, after).ok && before == after, "cancellation preserved layers")) return false;
  const auto history = gateway.list_task_history(request);
  if (!check(history.code == ResultCode::Ok && history.task_history.size() == 3, "history retains completed tasks")) return false;

  const auto make_plan = [&](const char* id) {
    assistant::EditPlan plan;
    plan.plan_id = id;
    plan.request_id = request.request_id;
    plan.base_revision = request.base_revision;
    plan.summary = "Adjust exposure";
    plan.operations.push_back({"op", "color.global", "", {{"exposure", "0.75"}}, true, true});
    return plan;
  };
  auto plan = make_plan("rejected-edit");
  if (!check(gateway.preview(request, plan).code == ResultCode::Ok, "edit preview")) return false;
  const auto rejected = gateway.commit(request, plan, true);
  if (!check(click_confirmation(controller, rejected.approval_id, false) &&
      gateway.read_edit_context(request).revision == request.base_revision &&
      controller.parameter_value({"color.global", "exposure"}) == std::optional<std::string>("0.50"),
      "reject restores preview without persistence")) return false;
  plan = make_plan("approved-edit");
  if (!check(gateway.preview(request, plan).code == ResultCode::Ok, "second edit preview")) return false;
  const auto edit = gateway.commit(request, plan, true);
  if (!check(click_confirmation(controller, edit.approval_id, true) &&
      gateway.read_edit_context(request).revision == request.base_revision + 1, "one desktop edit committed")) return false;
  controller.request_assistant_resolve(qs(edit.approval_id), true);
  if (!check(gateway.read_edit_context(request).revision == request.base_revision + 1, "duplicate button is harmless")) return false;
  request.base_revision = gateway.read_edit_context(request).revision;
  const auto undo = gateway.undo(request, edit.command_id, true);
  controller.request_parameter({"color.global", "exposure"}, "0.60", true);
  const auto revision = gateway.read_edit_context(request).revision;
  if (!check(click_confirmation(controller, undo.approval_id, true) &&
      gateway.approval_status(request, undo.approval_id).approval_status == "failed" &&
      gateway.read_edit_context(request).revision == revision, "stale undo preserves manual edit")) return false;
  request.base_revision = revision;
  const auto snapshot = gateway.create_snapshot(request, "current effect");
  const auto apply = gateway.apply_snapshot(request, {snapshot.snapshot_id, request.image.image_id, ""}, true);
  controller.request_parameter({"color.global", "exposure"}, "0.65", true);
  if (!check(click_confirmation(controller, apply.approval_id, true) &&
      gateway.approval_status(request, apply.approval_id).approval_status == "failed", "stale snapshot refused")) return false;
  request.base_revision = gateway.read_edit_context(request).revision;
  task.source_revision = request.base_revision;
  task.task_id = "expired-before-start";
  const auto expired = gateway.run_generation(request, task, true);
  controller.request_assistant_cancel();
  if (!check(gateway.resolve_approval(expired.approval_id, true).code == ResultCode::StateConflict &&
      !controller.assistant_busy(), "stop revokes pending approval")) return false;

  task.task_id = "generation-4";
  {
    const auto approval = gateway.run_generation(request, task, true);
    if (!check(click_confirmation(controller, approval.approval_id, true) &&
               gateway.approval_status(request, approval.approval_id).code == ResultCode::Ok, "photo switch task queued")) return false;
  }
  controller.request_page(Page::Home);
  if (!check(until([&] { return !controller.assistant_busy(); }) && !controller.has_photo(), "photo switch cancels safely")) return false;
  if (!check(files.read_file(layer_path, after).ok && before == after, "photo switch preserved layers")) return false;

  return true;
}

}  // namespace pixcake::shell
