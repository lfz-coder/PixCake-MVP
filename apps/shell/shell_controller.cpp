#include "shell_controller.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QImageReader>
#include <QInputDialog>
#include <QLineEdit>

#include "pixcake/capability/capability_registry.hpp"
#include "pixcake/generate/generation_tool_catalog.hpp"

#include <QDebug>
#include <QCoreApplication>
#include <QMetaObject>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>

namespace pixcake::shell {

ShellController::ShellController(ShellDisplay display)
    : display_(std::move(display)),
      assistant_gateway_(assistant::GatewayHandlers{
          [this](const assistant::AssistantRequest& request) { return assistant_read_edit_context(request); },
          [this](const assistant::AssistantRequest& request) { return assistant_read_selection_context(request); },
          [this](const assistant::AssistantRequest& request, const assistant::EditPlan& plan) {
            return assistant_preview_plan(request, plan);
          },
          [this](const assistant::AssistantRequest& request, const assistant::EditPlan& plan) {
            return assistant_commit_plan(request, plan);
          },
          [this](const assistant::AssistantRequest& request, const std::string& plan_id) {
            return assistant_reject_plan(request, plan_id);
          },
          [this](const assistant::AssistantRequest& request, const std::string& command_id) {
            return assistant_undo(request, command_id);
          },
          [this](const assistant::AssistantRequest& request, const assistant::AssistantCard& card) {
            return assistant_request_card(request, card);
          },
          [this](const assistant::AssistantRequest& request, const std::string& label) {
            return assistant_create_snapshot(request, label);
          },
          [this](const assistant::AssistantRequest& request, const assistant::AssistantSnapshotRequest& snapshot) {
            return assistant_apply_snapshot(request, snapshot);
          },
          [this](const assistant::AssistantRequest& request) { return assistant_list_task_history(request); },
          [this](const assistant::AssistantRequest& request, const assistant::AssistantGenerationRequest& task) {
            return assistant_run_generation(request, task);
          },
          [this](const assistant::AssistantRequest& request,
                 const assistant::AssistantGenerationCancelRequest& task) {
            return assistant_cancel_generation(request, task);
          },
          [this] { refresh_assistant_approvals(); }
      }) {}

ShellController::~ShellController() {
  stop_assistant_generation_worker();
  notifier_.reset();
  session_.reset();  // closes the connection; renderd exits
}

void ShellController::request_assistant() {
  if (!session_) {
    display_.post_notice(UserNotice{qtext("assistant.no-photo").toStdString(), true, false});
    if (changed_) changed_();
    return;
  }
  if (assistant_client_.busy()) {
    assistant_visible_ = true;
    assistant_status_ = qtext("assistant.status.busy");
    if (changed_) changed_();
    return;
  }
  assistant_visible_ = true;
  assistant_status_ = qtext("assistant.prompt");
  if (changed_) changed_();

  bool accepted = false;
  const QString message =
      QInputDialog::getText(nullptr, qtext("assistant.dialog.title"), qtext("assistant.dialog.prompt"), QLineEdit::Normal,
                            QString(), &accepted)
          .trimmed();
  if (!accepted || message.isEmpty()) {
    assistant_status_ = qtext("assistant.status.idle");
    if (changed_) changed_();
    return;
  }

  static std::atomic<std::uint64_t> sequence{0};
  const std::string image_id = session_->document().image_id();
  if (assistant_thread_id_.empty()) assistant_thread_id_ = "thread-" + image_id;
  const std::string request_id = "request-" + std::to_string(++sequence);
  QJsonObject image;
  image.insert(QStringLiteral("image_id"), QString::fromStdString(image_id));
  image.insert(QStringLiteral("editing_instance_id"), QString::fromStdString(session_->document().editing_instance_id()));
  image.insert(QStringLiteral("source_revision"), static_cast<qint64>(session_->document().revision()));
  QJsonObject request;
  request.insert(QStringLiteral("protocol_version"), 1);
  request.insert(QStringLiteral("request_id"), QString::fromStdString(request_id));
  request.insert(QStringLiteral("thread_id"), QString::fromStdString(assistant_thread_id_));
  request.insert(QStringLiteral("message"), message);
  request.insert(QStringLiteral("base_revision"), static_cast<qint64>(session_->document().revision()));
  request.insert(QStringLiteral("mode"), QStringLiteral("assistant"));
  request.insert(QStringLiteral("image"), image);

  append_assistant_row(QStringLiteral("user"), message);
  AssistantClient::Options options;
  options.stream_url = QUrl(qEnvironmentVariable(
      "PIXCAKE_ASSISTANT_URL", QStringLiteral("http://127.0.0.1:27461/threads/{thread_id}/runs/stream")));
  options.assistant_id = qEnvironmentVariable("PIXCAKE_ASSISTANT_ID", QStringLiteral("assistant"));
  QString diagnostic;
  view_.status = qtext("assistant.status.starting");
  assistant_status_ = view_.status;
  publish();
  if (changed_) changed_();
  if (!assistant_client_.stream(
          options, request,
          [this](const assistant::AssistantEvent& event) {
            append_assistant_row(event);
            view_.status = assistant_status_;
            publish();
            if (changed_) changed_();
          },
          [this](const QString& error) {
            append_assistant_row(QStringLiteral("error"), error);
            assistant_status_ = qtext("assistant.status.failed") + error;
            view_.status = assistant_status_;
            publish();
            if (changed_) changed_();
          },
          diagnostic)) {
    append_assistant_row(QStringLiteral("error"), diagnostic);
    assistant_status_ = qtext("assistant.status.failed") + diagnostic;
    view_.status = assistant_status_;
    publish();
    if (changed_) changed_();
  }
}

void ShellController::append_assistant_row(QString type, QString text) {
  type = type.trimmed();
  text = text.trimmed();
  if (type.isEmpty()) type = QStringLiteral("message");
  if (text.isEmpty()) text = qtext("assistant.no-content");
  // A broken or overly chatty sidecar must not grow the desktop process
  // without bound. The latest 80 rows are enough for the visible task log.
  if (assistant_transcript_.size() >= 80) assistant_transcript_.erase(assistant_transcript_.begin());
  assistant_transcript_.push_back({std::move(type), std::move(text)});
}

void ShellController::append_assistant_row(const assistant::AssistantEvent& event) {
  QString text = QString::fromStdString(event.data).trimmed();
  if (!text.isEmpty()) {
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &parse_error);
    if (parse_error.error == QJsonParseError::NoError) {
      if (document.isObject()) {
        const QJsonObject object = document.object();
        for (const char* key : {"message", "text", "content", "diagnostic", "detail", "title", "plan_id",
                                "card_type"}) {
          const auto value = object.value(QLatin1String(key));
          if (value.isString() && !value.toString().trimmed().isEmpty()) {
            text = value.toString().trimmed();
            break;
          }
        }
        if (text.startsWith(QLatin1Char('{')) || text.startsWith(QLatin1Char('['))) {
          text = QString::fromUtf8(document.toJson(QJsonDocument::Compact)).trimmed();
        }
      }
    }
  }
  if (text.size() > 512) text = text.left(509) + QStringLiteral("...");
  const QString type = QString::fromStdString(event.type).trimmed();
  append_assistant_row(type, text);
  if (type == QStringLiteral("completed") || type == QStringLiteral("done")) {
    assistant_status_ = !assistant_confirmations_.empty() ? qtext("assistant.status.approval")
                        : assistant_generation_busy_ ? qtext("assistant.generation.queued") : qtext("assistant.status.completed");
  } else if (type == QStringLiteral("approval.required")) {
    assistant_status_ = qtext("assistant.status.approval");
  } else if (type == QStringLiteral("preview.ready")) {
    assistant_status_ = qtext("assistant.status.preview");
  } else if (type == QStringLiteral("error")) {
    assistant_status_ = qtext("assistant.status.error");
  } else {
    assistant_status_ = qtext("assistant.status.event") + type;
  }
}

void ShellController::request_assistant_cancel() {
  if (!assistant_busy() && assistant_confirmations_.empty() && assistant_previews_.empty()) return;
  for (const auto& action : assistant_gateway_.pending_approvals()) {
    assistant_gateway_.resolve_approval(action.approval_id, false);
  }
  while (!assistant_previews_.empty()) {
    const auto preview = assistant_previews_.begin()->second;
    assistant_reject_plan(preview.request, preview.plan.plan_id);
  }
  assistant_gateway_.invalidate_approvals();
  assistant_confirmations_.clear();
  if (assistant_generation_busy_) {
    assistant_generation_cancel_requested_.store(true, std::memory_order_release);
    assistant_generation_entry_.status = "cancelling";
    generation_status_ = "cancelling";
  }
  assistant_client_.cancel();
  assistant_status_ = assistant_generation_busy_ ? qtext("assistant.generation.cancelling") : qtext("assistant.stopped");
  append_assistant_row(assistant_generation_busy_ ? QStringLiteral("task.progress") : QStringLiteral("cancelled"),
                       assistant_generation_busy_ ? assistant_status_ : qtext("assistant.cancelled"));
  view_.status = assistant_status_;
  publish();
  if (changed_) changed_();
}

void ShellController::request_assistant_hide() {
  assistant_visible_ = false;
  if (changed_) changed_();
}

assistant::GatewayReply ShellController::assistant_read_edit_context(
    const assistant::AssistantRequest& request) const {
  assistant::GatewayReply reply;
  reply.request_id = request.request_id;
  if (!session_) {
    reply.code = ResultCode::StateConflict;
    reply.diagnostic = "no photo is open";
    return reply;
  }
  const auto& document = session_->document();
  if (!request.image.image_id.empty() && request.image.image_id != document.image_id()) {
    reply.code = ResultCode::StateConflict;
    reply.diagnostic = "assistant image does not match the open photo";
    return reply;
  }
  reply.revision = document.revision();
  reply.image_id = document.image_id();
  reply.editing_instance_id = document.editing_instance_id();
  const auto registry = CapabilityRegistry::phase_one();
  for (const auto& descriptor : registry.descriptors()) {
    if (!descriptor.registered_for_drawing) continue;
    reply.capabilities.push_back(descriptor.id);
    assistant::AssistantCapabilityDescriptor advertised;
    advertised.capability_id = descriptor.id;
    advertised.module_label = descriptor.module_label;
    advertised.label = descriptor.label;
    advertised.can_use_in_preset = descriptor.can_use_in_preset;
    advertised.can_batch_sync = descriptor.can_batch_sync;
    advertised.batch_sync_blocked_reason = descriptor.batch_sync_blocked_reason;
    reply.capability_descriptors.push_back(std::move(advertised));
  }
  for (const auto& mode : phase_one_parameter_modes()) {
    const auto* capability = registry.find(mode.capability_id);
    if (capability == nullptr || !capability->registered_for_drawing) continue;
    assistant::AssistantParameterDescriptor advertised;
    advertised.capability_id = mode.capability_id;
    advertised.parameter_id = mode.parameter_id;
    advertised.minimum = mode.minimum;
    advertised.maximum = mode.maximum;
    advertised.step = mode.step;
    advertised.default_value = mode.default_value;
    advertised.render_stage = mode.render_stage;
    advertised.render_program = mode.render_program;
    advertised.render_channel = mode.render_channel;
    advertised.schema_version = mode.schema_version;
    advertised.order = mode.order;
    advertised.choices = mode.choices;
    advertised.default_choice = mode.default_choice;
    advertised.step_confirmed = mode.step_confirmed;
    advertised.default_confirmed = mode.default_confirmed;
    advertised.text = mode.text;
    advertised.max_length = mode.max_length;
    for (const auto& rule : mode.disable_rules) {
      advertised.disable_rules.push_back({rule.when_capability_id, rule.when_parameter_id, rule.when_choice,
                                          rule.reason, rule.when_zero});
    }
    reply.parameter_descriptors.push_back(std::move(advertised));
  }
  return reply;
}

assistant::GatewayReply ShellController::assistant_read_selection_context(
    const assistant::AssistantRequest& request) const {
  assistant::GatewayReply reply;
  reply.request_id = request.request_id;
  if (!session_) {
    reply.code = ResultCode::StateConflict;
    reply.diagnostic = "no photo is open";
    return reply;
  }
  if (!request.image.image_id.empty() && request.image.image_id != session_->document().image_id()) {
    reply.code = ResultCode::StateConflict;
    reply.diagnostic = "assistant image does not match the open photo";
    return reply;
  }
  reply.revision = session_->document().revision();
  reply.selection_id = request.selection.selection_id;
  reply.selection_kind = request.selection.kind.empty() ? "whole_image" : request.selection.kind;
  reply.selection_label = request.selection.label;
  return reply;
}

assistant::GatewayReply ShellController::assistant_preview_plan(const assistant::AssistantRequest& request,
                                                                const assistant::EditPlan& plan) {
  assistant::GatewayReply reply;
  reply.request_id = request.request_id;
  if (!assistant_context_matches(request)) {
    reply.code = ResultCode::StateConflict;
    reply.diagnostic = "assistant photo, editing instance or revision is stale";
    return reply;
  }
  if (!session_) {
    reply.code = ResultCode::StateConflict;
    reply.diagnostic = "no photo is open";
    return reply;
  }
  if (!request.image.image_id.empty() && request.image.image_id != session_->document().image_id()) {
    reply.code = ResultCode::StateConflict;
    reply.diagnostic = "assistant image does not match the open photo";
    return reply;
  }
  if (plan.base_revision != session_->document().revision()) {
    reply.code = ResultCode::StateConflict;
    reply.diagnostic = "assistant preview revision is stale";
    return reply;
  }
  if (plan.operations.size() != 1 || plan.operations.front().parameters.empty()) {
    reply.code = ResultCode::StateConflict;
    reply.diagnostic = "the first assistant adapter accepts one capability with one or more parameters per preview";
    return reply;
  }
  const auto& operation = plan.operations.front();
  if (!operation.target_selection_id.empty()) {
    reply.code = ResultCode::StateConflict;
    reply.diagnostic = "assistant target selection requires a registered selection adapter";
    return reply;
  }
  const auto key = request.request_id + ":" + plan.plan_id;
  if (assistant_previews_.find(key) != assistant_previews_.end()) {
    assistant_reject_plan(request, plan.plan_id);
  }
  AssistantPreviewState saved{request, plan, {}};
  for (const auto& parameter : operation.parameters) {
    if (!validate_parameter_display_value(operation.tool_id, parameter.name, parameter.value)) {
      reply.code = ResultCode::StateConflict;
      reply.diagnostic = "assistant parameter is invalid";
      return reply;
    }
    saved.previous.push_back(session_->document().preview(operation.tool_id, parameter.name));
  }
  std::string diagnostic;
  for (const auto& parameter : operation.parameters) {
    reply.code = session_->preview(operation.tool_id, parameter.name, parameter.value, diagnostic);
    if (reply.code != ResultCode::Ok) {
      assistant_previews_[key] = saved;
      assistant_reject_plan(request, plan.plan_id);
      reply.diagnostic = std::move(diagnostic);
      return reply;
    }
  }
  assistant_previews_[key] = std::move(saved);
  reply.preview_id = request.request_id + ":" + plan.plan_id;
  reply.revision = session_->document().revision();
  return reply;
}

assistant::GatewayReply ShellController::assistant_commit_plan(const assistant::AssistantRequest& request,
                                                               const assistant::EditPlan& plan) {
  assistant::GatewayReply reply;
  reply.request_id = request.request_id;
  if (!assistant_context_matches(request)) {
    reply.code = ResultCode::StateConflict;
    reply.diagnostic = "assistant photo, editing instance or revision is stale";
    return reply;
  }
  if (!session_) {
    reply.code = ResultCode::StateConflict;
    reply.diagnostic = "no photo is open";
    return reply;
  }
  if (!request.image.image_id.empty() && request.image.image_id != session_->document().image_id()) {
    reply.code = ResultCode::StateConflict;
    reply.diagnostic = "assistant image does not match the open photo";
    return reply;
  }
  if (plan.base_revision != session_->document().revision()) {
    reply.code = ResultCode::StateConflict;
    reply.diagnostic = "assistant commit revision is stale";
    return reply;
  }
  if (plan.operations.size() != 1 || plan.operations.front().parameters.empty()) {
    reply.code = ResultCode::StateConflict;
    reply.diagnostic = "the first assistant adapter accepts one capability with one or more parameters per commit";
    return reply;
  }
  const auto& operation = plan.operations.front();
  if (!operation.target_selection_id.empty()) {
    reply.code = ResultCode::StateConflict;
    reply.diagnostic = "assistant target selection requires a registered selection adapter";
    return reply;
  }
  std::string diagnostic;
  if (operation.parameters.size() == 1) {
    const auto& parameter = operation.parameters.front();
    reply.code = session_->commit(operation.tool_id, parameter.name, parameter.value, diagnostic);
  } else {
    for (const auto& parameter : operation.parameters) {
      reply.code = session_->preview(operation.tool_id, parameter.name, parameter.value, diagnostic);
      if (reply.code != ResultCode::Ok) break;
    }
    if (reply.code == ResultCode::Ok) {
      std::vector<std::pair<std::string, std::string>> values;
      values.reserve(operation.parameters.size());
      for (const auto& parameter : operation.parameters) values.emplace_back(parameter.name, parameter.value);
      reply.code = session_->commit_parameters(operation.tool_id, values, diagnostic);
    }
  }
  reply.diagnostic = std::move(diagnostic);
  reply.command_id = request.request_id + ":" + plan.plan_id;
  reply.revision = session_->document().revision();
  if (reply.code == ResultCode::Ok) {
    assistant_previews_.erase(request.request_id + ":" + plan.plan_id);
    reply.snapshot_id = reply.command_id;
    assistant_snapshots_[reply.snapshot_id] = {
        session_->document().image_id(), session_->history_current(), reply.revision, plan.summary};
    if (changed_) changed_();
  }
  return reply;
}

assistant::GatewayReply ShellController::assistant_reject_plan(const assistant::AssistantRequest& request,
                                                               const std::string& plan_id) {
  assistant::GatewayReply reply;
  reply.request_id = request.request_id;
  reply.command_id = request.request_id + ":reject:" + plan_id;
  const auto found = assistant_previews_.find(request.request_id + ":" + plan_id);
  if (found != assistant_previews_.end()) {
    if (assistant_context_matches(request)) {
      const auto& operation = found->second.plan.operations.front();
      bool restored = false;
      for (std::size_t index = 0; index < operation.parameters.size(); ++index) {
        const auto& parameter = operation.parameters[index];
        restored = session_->restore_preview_value(operation.tool_id, parameter.name, parameter.value,
                                                   found->second.previous[index]) || restored;
      }
      if (restored) {
        std::string diagnostic;
        session_->refresh_preview(diagnostic);
      }
    }
    assistant_previews_.erase(found);
  }
  reply.notices.push_back("assistant plan rejected");
  return reply;
}

assistant::GatewayReply ShellController::assistant_undo(const assistant::AssistantRequest& request,
                                                       const std::string& command_id) {
  assistant::GatewayReply reply;
  reply.request_id = request.request_id;
  if (!assistant_context_matches(request)) {
    reply.code = ResultCode::StateConflict;
    reply.diagnostic = "assistant photo, editing instance or revision is stale";
    return reply;
  }
  if (!session_) {
    reply.code = ResultCode::StateConflict;
    reply.diagnostic = "no photo is open";
    return reply;
  }
  if (!request.image.image_id.empty() && request.image.image_id != session_->document().image_id()) {
    reply.code = ResultCode::StateConflict;
    reply.diagnostic = "assistant image does not match the open photo";
    return reply;
  }
  const auto committed = assistant_snapshots_.find(command_id);
  if (committed == assistant_snapshots_.end() || committed->second.revision != session_->document().revision() ||
      committed->second.history_index != session_->history_current()) {
    reply.code = ResultCode::StateConflict;
    reply.diagnostic = "assistant undo no longer refers to the current edit";
    return reply;
  }
  std::string diagnostic;
  reply.code = session_->undo(diagnostic);
  reply.diagnostic = std::move(diagnostic);
  reply.command_id = request.request_id + ":undo:" + command_id;
  reply.revision = session_->document().revision();
  if (reply.code == ResultCode::Ok && changed_) changed_();
  return reply;
}

assistant::GatewayReply ShellController::assistant_request_card(const assistant::AssistantRequest& request,
                                                               const assistant::AssistantCard& card) {
  assistant::GatewayReply reply;
  reply.request_id = request.request_id;
  reply.card = card;
  reply.notices.push_back("structured card is awaiting UI input");
  return reply;
}

assistant::GatewayReply ShellController::assistant_create_snapshot(const assistant::AssistantRequest& request,
                                                                   const std::string& label) {
  assistant::GatewayReply reply;
  reply.request_id = request.request_id;
  if (!session_) {
    reply.code = ResultCode::StateConflict;
    reply.diagnostic = "no photo is open";
    return reply;
  }
  if (!request.image.image_id.empty() && request.image.image_id != session_->document().image_id()) {
    reply.code = ResultCode::StateConflict;
    reply.diagnostic = "assistant snapshot image does not match the open photo";
    return reply;
  }
  static std::atomic<std::uint64_t> sequence{0};
  reply.snapshot_id = request.request_id + ":snapshot:" + std::to_string(++sequence);
  reply.revision = session_->document().revision();
  assistant_snapshots_[reply.snapshot_id] = {
      session_->document().image_id(), session_->history_current(), reply.revision, label};
  return reply;
}

assistant::GatewayReply ShellController::assistant_apply_snapshot(
    const assistant::AssistantRequest& request, const assistant::AssistantSnapshotRequest& snapshot) {
  assistant::GatewayReply reply;
  reply.request_id = request.request_id;
  if (!assistant_context_matches(request)) {
    reply.code = ResultCode::StateConflict;
    reply.diagnostic = "assistant photo, editing instance or revision is stale";
    return reply;
  }
  if (!session_) {
    reply.code = ResultCode::StateConflict;
    reply.diagnostic = "no photo is open";
    return reply;
  }
  const auto found = assistant_snapshots_.find(snapshot.snapshot_id);
  if (found == assistant_snapshots_.end()) {
    reply.code = ResultCode::StateConflict;
    reply.diagnostic = "assistant snapshot is unavailable";
    return reply;
  }
  const std::string image_id = snapshot.image_id.empty() ? request.image.image_id : snapshot.image_id;
  if ((!image_id.empty() && image_id != session_->document().image_id()) ||
      found->second.image_id != session_->document().image_id()) {
    reply.code = ResultCode::StateConflict;
    reply.diagnostic = "assistant snapshot image does not match the open photo";
    return reply;
  }
  std::string diagnostic;
  reply.code = session_->select_history(found->second.history_index, diagnostic);
  reply.diagnostic = std::move(diagnostic);
  reply.snapshot_id = snapshot.snapshot_id;
  reply.revision = session_->document().revision();
  if (reply.code == ResultCode::Ok && changed_) changed_();
  return reply;
}

assistant::GatewayReply ShellController::assistant_list_task_history(
    const assistant::AssistantRequest& request) const {
  assistant::GatewayReply reply;
  reply.request_id = request.request_id;
  if (!generation_) {
    reply.code = ResultCode::StateConflict;
    reply.diagnostic = "generation task history is unavailable";
    return reply;
  }
  const auto& history = assistant_generation_busy_ ? assistant_generation_history_ : generation_->history();
  for (const auto& entry : history) {
    if (entry.task_id == assistant_generation_entry_.task_id) continue;
    reply.task_history.push_back({entry.task_id, entry.image_id, entry.tool_id, entry.status, entry.source_revision,
                                  static_cast<std::uint32_t>(entry.results.size())});
  }
  if (!assistant_generation_entry_.task_id.empty()) reply.task_history.push_back(assistant_generation_entry_);
  return reply;
}

void ShellController::reap_assistant_generation_worker() {
  if (assistant_generation_busy_ || !assistant_generation_worker_.joinable()) return;
  assistant_generation_worker_.join();
  discard_pending_generation_result();
}

void ShellController::discard_pending_generation_result() {
  const auto pending = assistant_generation_pending_;
  if (!pending || pending->handled) return;
  if (pending->code == ResultCode::Ok && !pending->progress.results.empty()) {
    LayerDocument rejected;
    std::string diagnostic;
    if (generation_ != nullptr) {
      (void)generation_->place_result(pending->verified, rejected, {}, diagnostic);
    }
    // If the generated child has already gone away, confirmation cannot reach
    // it; remove the verified files locally so a stale callback cannot leave
    // creative/ filled with orphaned pixels.
    for (const auto& result : pending->progress.results) generation_files_.remove_file(result.pixel_file);
  }
  pending->handled = true;
  assistant_generation_pending_.reset();
}

void ShellController::stop_assistant_generation_worker() {
  assistant_generation_cancel_requested_.store(true, std::memory_order_release);
  assistant_generation_lifetime_.reset();
  if (assistant_generation_worker_.joinable()) assistant_generation_worker_.join();
  discard_pending_generation_result();
  assistant_generation_busy_ = false;
  assistant_generation_task_id_.clear();
}

assistant::GatewayReply ShellController::assistant_run_generation(
    const assistant::AssistantRequest& request, const assistant::AssistantGenerationRequest& task_request) {
  assistant::GatewayReply reply;
  reply.request_id = request.request_id;
  if (!assistant_context_matches(request)) {
    reply.code = ResultCode::StateConflict;
    reply.diagnostic = "assistant photo, editing instance or revision is stale";
    return reply;
  }
  reap_assistant_generation_worker();
  if (!generation_ || !projects_ || !session_ || !current_project_ || !selected_photo_) {
    reply.code = ResultCode::StateConflict;
    reply.diagnostic = "generation is unavailable for the current photo";
    return reply;
  }
  if (assistant_generation_busy_) {
    reply.code = ResultCode::StateConflict;
    reply.diagnostic = "a generation task is already running";
    return reply;
  }
  if (!assistant_generation_fixture_ || task_request.tool_id != generate::kDevelopmentFixtureToolId) {
    reply.code = ResultCode::StateConflict;
    reply.diagnostic = "generation requires a configured provider quote and UI cost confirmation";
    return reply;
  }
  if (QCoreApplication::instance() == nullptr) {
    reply.code = ResultCode::StateConflict;
    reply.diagnostic = "generation requires the shell event loop";
    return reply;
  }
  const std::string image_id = projects_->image_id(*selected_photo_);
  if (task_request.image_id != image_id || image_id != session_->document().image_id() ||
      task_request.source_revision != session_->document().revision() ||
      (!request.image.editing_instance_id.empty() &&
       request.image.editing_instance_id != session_->document().editing_instance_id())) {
    reply.code = ResultCode::StateConflict;
    reply.diagnostic = "generation source image or revision is stale";
    return reply;
  }
  for (const auto& entry : generation_->history()) {
    if (entry.task_id == task_request.task_id) {
      reply.code = ResultCode::StateConflict;
      reply.diagnostic = "generation task id already exists in durable history";
      return reply;
    }
  }
  const auto* descriptor = generation_catalog_.find(task_request.tool_id);
  if (descriptor == nullptr) {
    reply.code = ResultCode::UnsupportedFormat;
    reply.diagnostic = "generation tool is unavailable";
    return reply;
  }

  std::vector<std::pair<std::string, std::string>> parameter_values;
  parameter_values.reserve(task_request.parameters.size());
  for (const auto& parameter : task_request.parameters) {
    parameter_values.emplace_back(parameter.name, parameter.value);
  }
  if (parameter_values.empty() && generation_catalog_.default_parameters(task_request.tool_id, parameter_values) !=
                                      ResultCode::Ok) {
    reply.code = ResultCode::StateConflict;
    reply.diagnostic = "generation defaults are unavailable";
    return reply;
  }
  std::vector<std::uint8_t> parameter_payload;
  if (generation_catalog_.encode_parameters(task_request.tool_id, parameter_values, parameter_payload) !=
      ResultCode::Ok) {
    reply.code = ResultCode::StateConflict;
    reply.diagnostic = "generation parameters are invalid";
    return reply;
  }

  generate::GenerationTask task;
  std::vector<std::string> mask_ids = task_request.mask_ids;
  std::vector<std::string> reference_image_ids = task_request.reference_image_ids;
  if (mask_ids.empty()) mask_ids = session_->document().mask_ids();
  if (reference_image_ids.empty()) reference_image_ids = session_->document().manual_refs();
  const ResultCode built = generation_catalog_.build_task(
      task_request.tool_id, task_request.task_id, image_id, task_request.source_revision,
      std::move(parameter_payload), std::move(mask_ids), std::move(reference_image_ids), task_request.idempotency_key,
      task);
  if (built != ResultCode::Ok) {
    reply.code = built;
    reply.diagnostic = "generation task is invalid";
    return reply;
  }

  TaskQuote quote;
  quote.task_id = task.task_id;
  quote.tool_id = task.tool.tool_id;
  quote.origin = kLedgerOriginPackage;
  quote.quoted_points = 0;  // Only the explicitly enabled development fixture reaches this path.
  const std::string layer_path = projects_->layer_document_path(*selected_photo_);
  const std::uint64_t source_revision = task.source_revision;
  const std::string task_id = task.task_id;
  const std::string task_image_id = task.image_id;
  const std::string tool_id = task.tool.tool_id;
  const std::string editing_instance_id = session_->document().editing_instance_id();
  const std::vector<std::pair<std::string, std::string>> draft_parameters = parameter_values;
  generation_retry_draft_ = GenerationDraft{tool_id, task_image_id, source_revision, draft_parameters,
                                            task.mask_ids, task.reference_image_ids};
  assistant_generation_busy_ = true;
  assistant_generation_task_id_ = task_id;
  assistant_generation_entry_ = {task_id, task_image_id, tool_id, "running", source_revision, 0};
  assistant_generation_history_ = generation_->history();
  assistant_generation_cancel_requested_.store(false, std::memory_order_release);
  if (!assistant_generation_lifetime_) assistant_generation_lifetime_ = std::make_shared<int>(0);
  generation_status_ = "queued";
  assistant_status_ = qtext("assistant.generation.queued");
  append_assistant_row(QStringLiteral("task.started"), assistant_status_);
  if (changed_) changed_();

  const std::weak_ptr<int> lifetime = assistant_generation_lifetime_;
  assistant_generation_worker_ = std::thread([this, lifetime, task = std::move(task), quote, layer_path, task_id,
                                               task_image_id, source_revision, editing_instance_id]() mutable {
    std::string diagnostic;
    ipc::GenerationProgressMessage progress;
    generation_session::VerifiedGenerationResult verified;
    const ResultCode code = generation_->generate(task, quote, progress, 30'000, diagnostic,
                                                 &assistant_generation_cancel_requested_, &verified);
    auto pending = std::make_shared<PendingGenerationResult>();
    pending->code = code;
    pending->diagnostic = std::move(diagnostic);
    pending->progress = std::move(progress);
    pending->verified = std::move(verified);
    assistant_generation_pending_ = pending;
    const auto reject_unplaced = [&] {
      if (pending->code != ResultCode::Ok || pending->progress.results.empty() || pending->handled) return;
      LayerDocument rejected;
      std::string reject_diagnostic;
      (void)generation_->place_result(pending->verified, rejected, {}, reject_diagnostic);
      pending->handled = true;
    };
    auto* application = QCoreApplication::instance();
    if (application == nullptr || lifetime.expired()) {
      reject_unplaced();
      return;
    }
    QMetaObject::invokeMethod(
        application,
        [this, lifetime, pending, task_id, task_image_id, source_revision, layer_path,
         editing_instance_id]() mutable {
          if (lifetime.expired()) return;
          // The worker has finished accessing the session before it posts this
          // callback. Join before exposing it to other shell operations.
          if (assistant_generation_worker_.joinable()) assistant_generation_worker_.join();
          const ResultCode code = pending->code;
          std::string& diagnostic = pending->diagnostic;
          auto& progress = pending->progress;
          auto& verified = pending->verified;
          assistant_generation_busy_ = false;
          assistant_generation_task_id_.clear();
          if (code == ResultCode::Cancelled) {
            generation_status_ = "cancelled";
          } else if (code != ResultCode::Ok) {
            generation_status_ = "failed";
            if (!diagnostic.empty()) qWarning("assistant generation: %s", diagnostic.c_str());
          } else if (assistant_generation_cancel_requested_.load(std::memory_order_acquire)) {
            if (!progress.results.empty()) {
              LayerDocument rejected;
              std::string reject_diagnostic;
              (void)generation_->place_result(verified, rejected, {}, reject_diagnostic);
            }
            generation_status_ = "succeeded-not-applied";
          } else if (!session_ || session_->document().image_id() != task_image_id ||
                     session_->document().editing_instance_id() != editing_instance_id ||
                     session_->document().revision() != source_revision) {
            if (!progress.results.empty()) {
              LayerDocument rejected;
              std::string reject_diagnostic;
              (void)generation_->place_result(verified, rejected, {}, reject_diagnostic);
            }
            generation_status_ = "succeeded-source-changed";
          } else {
            LayerDocument document;
            const LoadResult loaded = LayerDocument::load(document, generation_files_, layer_path);
            ResultCode placed = ResultCode::Ok;
            bool placement_attempted = false;
            if (loaded.absent) {
              auto created = LayerDocument::create(task_image_id, source_revision);
              if (created) document = std::move(*created);
              else placed = ResultCode::StateConflict;
            } else if (loaded.recovered_empty || !loaded.code || *loaded.code != ResultCode::Ok) {
              placed = loaded.code.value_or(ResultCode::DocumentCorrupt);
              if (placed == ResultCode::Ok) placed = ResultCode::DocumentCorrupt;
            }
            if (placed == ResultCode::Ok) {
              placement_attempted = true;
              placed = generation_->place_result(verified, document, layer_path, diagnostic);
            }
            if (placed != ResultCode::Ok) {
              if (!placement_attempted && !progress.results.empty()) {
                LayerDocument rejected;
                std::string reject_diagnostic;
                (void)generation_->place_result(verified, rejected, {}, reject_diagnostic);
              }
              generation_status_ = "succeeded-placement-failed";
            } else if (session_->refresh_layer_document(layer_path, diagnostic) != ResultCode::Ok) {
              generation_status_ = "succeeded-refresh-failed";
            } else {
              generation_status_ = "succeeded";
              generation_retry_draft_.reset();
              if (current_photo_) request_thumbnail(*current_photo_);
            }
            if (!diagnostic.empty()) qWarning("assistant generation placement: %s", diagnostic.c_str());
          }
          assistant_generation_entry_.status = generation_status_;
          assistant_generation_entry_.result_count = static_cast<std::uint32_t>(progress.results.size());
          if (session_ && session_->document().editing_instance_id() == editing_instance_id) {
            const char* key = generation_status_ == "succeeded" ? "assistant.generation.completed" :
                generation_status_ == "cancelled" ? "assistant.generation.cancelled" :
                generation_status_ == "failed" ? "assistant.generation.failed" : "assistant.generation.withheld";
            assistant_status_ = qtext(key);
            append_assistant_row(QStringLiteral("task.progress"), assistant_status_);
          }
          pending->handled = true;
          if (assistant_generation_pending_ == pending) assistant_generation_pending_.reset();
          if (changed_) changed_();
        },
        Qt::QueuedConnection);
  });

  reply.task_id = task_id;
  reply.task_status = "queued";
  reply.revision = source_revision;
  return reply;
}

assistant::GatewayReply ShellController::assistant_cancel_generation(
    const assistant::AssistantRequest& request, const assistant::AssistantGenerationCancelRequest& task) {
  assistant::GatewayReply reply;
  reply.request_id = request.request_id;
  if (task.task_id != assistant_generation_entry_.task_id ||
      (!request.image.image_id.empty() && request.image.image_id != assistant_generation_entry_.image_id)) {
    reply.code = ResultCode::StateConflict;
    reply.diagnostic = "assistant generation task is unknown or belongs to another image";
    return reply;
  }
  if (assistant_generation_busy_) {
    assistant_generation_cancel_requested_.store(true, std::memory_order_release);
    assistant_generation_entry_.status = "cancelling";
    generation_status_ = "cancelling";
    assistant_status_ = qtext("assistant.generation.cancelling");
    append_assistant_row(QStringLiteral("task.progress"), assistant_status_);
    if (changed_) changed_();
  }
  reply.task_id = task.task_id;
  reply.task_status = assistant_generation_entry_.status;
  return reply;
}

void ShellController::request_page(Page target) {
  if (display_.navigate(target) != ResultCode::Ok) return;
  if (target == Page::Home) close_project();
  // The library opens on the grid (as 像素蛋糕 does); refine shows the selected photo.
  if (target == Page::Library) display_.set_library_view("grid");
  if (target == Page::Refine) show_selected_photo();
  remember_place();
  if (changed_) changed_();
}

void ShellController::remember_place() {
  if (!projects_ || !current_project_) return;
  const Page page = display_.page();
  if (page != Page::Library && page != Page::Refine) return;  // creative is left through refine
  project_session::ProjectPlace place;
  place.workspace = page == Page::Library ? project_session::Workspace::Library : project_session::Workspace::Refine;
  place.photo_id = selected_photo_;
  projects_->remember_place(*current_project_, place);  // window state: a failure only loses the place
}

void ShellController::show_selected_photo() {
  if (!projects_ || !current_project_) return;
  if (!selected_photo_) {
    std::vector<PhotoRow> rows;
    if (projects_->list_photos(*current_project_, rows) != ResultCode::Ok || rows.empty()) return;
    selected_photo_ = rows.front().id;
  }
  if (current_photo_ != selected_photo_ || !session_) open_project_photo(*selected_photo_);
}

void ShellController::select_photo(std::int64_t photo_id) { select_photos({photo_id}, photo_id); }

std::vector<std::int64_t> ShellController::selected_photos() const {
  if (!selected_photo_) return {};
  if (std::find(selection_.begin(), selection_.end(), *selected_photo_) != selection_.end()) return selection_;
  return {*selected_photo_};
}

void ShellController::select_photos(const std::vector<std::int64_t>& photo_ids, std::int64_t primary) {
  if (!projects_ || !current_project_) return;
  selection_ = photo_ids;
  if (photo_ids.empty()) {
    selected_photo_.reset();
  } else {
    if (std::find(photo_ids.begin(), photo_ids.end(), primary) == photo_ids.end()) primary = photo_ids.back();
    selected_photo_ = primary;
  }
  remember_place();
  selection_changed();
}

void ShellController::watch_selection(QObject* owner, std::function<void()> changed) {
  selection_watchers_.emplace_back(owner, std::move(changed));
}

void ShellController::selection_changed() {
  for (auto it = selection_watchers_.begin(); it != selection_watchers_.end();) {
    if (it->first.isNull()) {
      it = selection_watchers_.erase(it);
      continue;
    }
    it->second();
    ++it;
  }
}

void ShellController::request_view_photo(std::int64_t photo_id) {
  if (!projects_ || !current_project_ || display_.page() != Page::Library) return;
  selected_photo_ = photo_id;
  display_.set_library_view("single");
  show_selected_photo();  // the large image is the rendered preview, edits included
  remember_place();
  if (changed_) changed_();
}

void ShellController::request_library_view(const std::string& view) {
  if (view != "grid" && view != "single") return;  // the filter view is not built yet
  if (display_.set_library_view(view) != ResultCode::Ok) return;
  if (view == "single") show_selected_photo();
  if (changed_) changed_();
}

void ShellController::close_photo() {
  // Switching photos remains non-blocking. The completion callback checks the
  // editing instance again before any layer write.
  if (assistant_generation_busy_) assistant_generation_cancel_requested_.store(true, std::memory_order_release);
  notifier_.reset();
  session_.reset();
  current_photo_.reset();
  clear_assistant_state();
  view_ = {};
  original_preview_visible_ = false;
  original_sequence_.reset();
  original_requests_.clear();
  original_edge_ = 0;
  original_image_ = {};
  last_edit_sequence_ = 0;
  last_edit_frame_sequence_ = 0;
  last_edit_interactive_ = false;
  histogram_refresh_gate_.reset();
  guides_.clear();
  guides_visible_ = true;
  guides_changed();
}

void ShellController::clear_assistant_state() {
  assistant_gateway_.invalidate_approvals();
  assistant_confirmations_.clear();
  assistant_client_.cancel();
  assistant_visible_ = false;
  assistant_status_.clear();
  assistant_transcript_.clear();
  assistant_thread_id_.clear();
  assistant_snapshots_.clear();
  assistant_previews_.clear();
}

bool ShellController::assistant_context_matches(const assistant::AssistantRequest& request) const {
  return session_ && request.image.image_id == session_->document().image_id() &&
         request.image.editing_instance_id == session_->document().editing_instance_id() &&
         request.base_revision == session_->document().revision();
}

void ShellController::refresh_assistant_approvals() {
  assistant_confirmations_.clear();
  const auto registry = CapabilityRegistry::phase_one();
  std::map<std::pair<std::string, std::string>, std::string> parameter_labels;
  for (const auto& rows : {ui::panels::refine_color_rows(), ui::panels::refine_post_color_rows(), ui::panels::refine_local_rows()}) {
    for (const auto& row : rows) {
      if (row.binding) parameter_labels[{row.binding->capability_id, row.binding->parameter_id}] = row.label;
    }
  }
  for (const auto& pending : assistant_gateway_.pending_approvals()) {
    AssistantConfirmationCard card;
    card.approval_id = qs(pending.approval_id);
    card.details = qtext("assistant.confirm.context").arg(pending.request.base_revision);
    switch (pending.kind) {
      case assistant::AssistantApprovalKind::Commit:
        card.title = qtext("assistant.confirm.edit");
        for (const auto& operation : pending.plan.operations) {
          const auto* descriptor = registry.find(operation.tool_id);
          card.details += QStringLiteral("\n") + qs(descriptor ? descriptor->label : operation.tool_id);
          for (const auto& parameter : operation.parameters) {
            const auto label = parameter_labels.find({operation.tool_id, parameter.name});
            card.details += QStringLiteral("\n%1 = %2").arg(qs(label == parameter_labels.end() ? parameter.name : label->second),
                                                            qs(parameter.value));
          }
        }
        card.details += QStringLiteral("\n") + qtext("assistant.confirm.local-cost");
        break;
      case assistant::AssistantApprovalKind::Undo:
        card.title = qtext("assistant.confirm.undo");
        card.details += QStringLiteral("\n") + qtext("assistant.confirm.undo-detail");
        break;
      case assistant::AssistantApprovalKind::ApplySnapshot: {
        card.title = qtext("assistant.confirm.snapshot");
        const auto found = assistant_snapshots_.find(pending.snapshot.snapshot_id);
        card.details += QStringLiteral("\n") + qtext("assistant.confirm.snapshot-detail");
        if (found != assistant_snapshots_.end() && !found->second.label.empty()) {
          card.details += QStringLiteral("\n") + qs(found->second.label);
        }
        break;
      }
      case assistant::AssistantApprovalKind::Generation:
        card.title = qtext("assistant.confirm.generation");
        card.can_confirm = assistant_generation_fixture_ && pending.generation.tool_id == generate::kDevelopmentFixtureToolId;
        card.details += QStringLiteral("\n") + qtext(card.can_confirm ? "assistant.confirm.fixture-cost"
                                                                       : "assistant.confirm.quote-unavailable");
        for (const auto& parameter : pending.generation.parameters) {
          card.details += QStringLiteral("\n%1 = %2").arg(qs(parameter.name), qs(parameter.value));
        }
        break;
    }
    assistant_confirmations_.push_back(std::move(card));
  }
  if (!assistant_confirmations_.empty()) {
    assistant_visible_ = true;
    assistant_status_ = qtext("assistant.status.approval");
  }
  if (changed_) changed_();
}

void ShellController::request_assistant_resolve(const QString& approval_id, bool approved) {
  // The button supplies only a C++-owned ID; never accept replacement payloads from UI or Python.
  const auto pending = assistant_gateway_.pending_approvals();
  const auto found = std::find_if(pending.begin(), pending.end(), [&](const auto& item) {
    return item.approval_id == approval_id.toStdString();
  });
  if (found == pending.end()) return;
  const auto reply = assistant_gateway_.resolve_approval(found->approval_id, approved);
  assistant_status_ = !approved ? qtext("assistant.confirm.rejected")
                     : reply.code != ResultCode::Ok ? qtext("assistant.confirm.failed")
                     : reply.task_status == "queued" ? qtext("assistant.generation.queued")
                                                       : qtext("assistant.confirm.applied");
  append_assistant_row(qtext("assistant.event"), assistant_status_);
  refresh_assistant_approvals();
}

void ShellController::close_project() {
  // Leaving the project closes the photo and the thumbnails (both renderd exit).
  close_photo();
  thumbnail_notifier_.reset();
  thumbnails_.reset();
  thumbnail_cache_.clear();
  thumbnail_requested_.clear();
  selected_photo_.reset();
  selection_.clear();
  current_project_.reset();
}

namespace {
constexpr std::uint32_t kThumbnailEdge = 320;  // 160-point cells on a 2x display
}

void ShellController::request_thumbnails() {
  if (!projects_ || !current_project_) return;
  std::vector<PhotoRow> rows;
  if (projects_->list_photos(*current_project_, rows) != ResultCode::Ok) return;
  for (const auto& row : rows) {
    if (thumbnail_requested_.count(row.id) == 0) request_thumbnail(row.id);
  }
}

void ShellController::request_thumbnail(std::int64_t photo_id) {
  if (!projects_) return;
  std::string diagnostic;
  if (!thumbnails_) {
    if (preview_channel::ThumbnailService::start(preview_options_.renderd_executable, preview_options_.log_directory,
                                                 preview_options_.connect_timeout_ms, thumbnails_,
                                                 diagnostic) != ResultCode::Ok) {
      qWarning("thumbnails unavailable: %s", diagnostic.c_str());
      return;
    }
    thumbnail_notifier_ = std::make_unique<QSocketNotifier>(thumbnails_->fd(), QSocketNotifier::Read);
    QObject::connect(thumbnail_notifier_.get(), &QSocketNotifier::activated, thumbnail_notifier_.get(),
                     [this] { read_thumbnails(); });
  }
  thumbnail_requested_.insert(photo_id);
  std::string path;
  if (projects_->resolve_photo(photo_id, path, diagnostic) != ResultCode::Ok) return;  // missing: no thumbnail
  std::vector<std::uint8_t> document;
  if (projects_->document_bytes(photo_id, document) != ResultCode::Ok) document.clear();  // show the original
  std::vector<std::uint8_t> layer_document;
  const std::string layer_path = projects_->layer_document_path(photo_id);
  if (generation_files_.exists(layer_path)) {
    if (generation_files_.read_file(layer_path, layer_document).ok == false) layer_document.clear();
  }
  if (thumbnails_->request(photo_id, projects_->image_id(photo_id), path,
                           std::move(document), kThumbnailEdge, diagnostic, std::move(layer_document)) != ResultCode::Ok) {
    qWarning("thumbnail request failed: %s", diagnostic.c_str());
  }
}

void ShellController::read_thumbnails() {
  if (!thumbnails_) return;
  for (;;) {
    preview_channel::ThumbnailService::Thumbnail thumbnail;
    std::string diagnostic;
    const ResultCode code = thumbnails_->take(thumbnail, diagnostic);
    if (code == ResultCode::Cancelled) break;
    if (code != ResultCode::Ok) {
      qWarning("thumbnail renderd went away: %s", diagnostic.c_str());
      thumbnail_notifier_.reset();
      thumbnails_.reset();
      thumbnail_requested_.clear();  // a later request starts a new one
      break;
    }
    if (thumbnail.code != ResultCode::Ok) {
      qWarning("thumbnail %lld failed: %s", static_cast<long long>(thumbnail.key), thumbnail.diagnostic.c_str());
      continue;
    }
    QImage image(thumbnail.rgba.data(), static_cast<int>(thumbnail.width), static_cast<int>(thumbnail.height),
                 static_cast<qsizetype>(thumbnail.stride), QImage::Format_RGBA8888_Premultiplied);
    const QImage owned = image.copy();
    thumbnail_cache_[thumbnail.key] = owned;
    for (auto it = thumbnail_watchers_.begin(); it != thumbnail_watchers_.end();) {
      if (!it->first) {
        it = thumbnail_watchers_.erase(it);
        continue;
      }
      it->second(thumbnail.key, owned);
      ++it;
    }
  }
}

QImage ShellController::thumbnail(std::int64_t photo_id) const {
  const auto found = thumbnail_cache_.find(photo_id);
  return found == thumbnail_cache_.end() ? QImage() : found->second;
}

void ShellController::watch_thumbnails(QObject* owner, std::function<void(std::int64_t, const QImage&)> show) {
  thumbnail_watchers_.emplace_back(owner, std::move(show));
}

void ShellController::show_notices(const std::vector<std::string>& notices, NoticeScene scene, ResultCode code) {
  for (const auto& text : notices) display_.post_notice(UserNotice{text, true, false});
  if (notices.empty() && code != ResultCode::Ok) display_.post_notice(notice_for(scene, code));
}

void ShellController::attach_projects(std::unique_ptr<project_session::ProjectSession> projects,
                                      preview_channel::PreviewSessionOptions preview) {
  projects_ = std::move(projects);
  preview_options_ = std::move(preview);
  if (preview_options_.max_edge == 2000 || preview_options_.max_edge == 3000 || preview_options_.max_edge == 4000) {
    preview_max_edge_ = preview_options_.max_edge;
  } else {
    preview_max_edge_ = 4000;
  }
  preview_options_.max_edge = preview_max_edge_;
  if (changed_) changed_();
}

void ShellController::request_import(const std::vector<std::string>& paths) {
  request_import_internal(paths, std::nullopt);
}

void ShellController::request_import_named(const std::vector<std::string>& paths, const std::string& name) {
  request_import_internal(paths, name.empty() ? std::nullopt : std::optional<std::string>(name));
}

void ShellController::request_import_internal(const std::vector<std::string>& paths,
                                               const std::optional<std::string>& name) {
  if (!projects_ || paths.empty()) return;
  const bool in_project = current_project_.has_value() && display_.in_project();
  const auto outcome = in_project ? projects_->import_into(*current_project_, paths)
                                  : (name ? projects_->import_new_project(paths, *name) : projects_->import_new_project(paths));
  if (!in_project && outcome.project_id != 0 && outcome.accepted > 0) {
    // Requirement 5.3: entering a project shows refine with the first photo.
    if (display_.open_project() == ResultCode::Ok) {
      current_project_ = outcome.project_id;
      filter_ = {};
      open_project_photo(outcome.photo_ids.front());
      remember_place();
    }
  }
  request_thumbnails();
  show_notices(outcome.notices, NoticeScene::Import, outcome.code);
  if (changed_) changed_();
}

ui::shell::IconDropAction ShellController::drop_on_app_icon(const std::vector<std::string>& paths, bool modal_open) {
  using ui::shell::IconDropAction;
  IconDropAction action = ui::shell::icon_drop_action(display_, modal_open);
  // A project page with no project behind it (development previews) has nothing to append to.
  if (!projects_ || paths.empty() || (action == IconDropAction::AppendToProject && !current_project_)) {
    action = IconDropAction::Ignore;
  }
  if (action != IconDropAction::Ignore) request_import(paths);  // new project on home, append in a project
  return action;
}

std::vector<ProjectEntry> ShellController::projects() const {
  std::vector<ProjectEntry> out;
  std::vector<ProjectRow> rows;
  if (projects_ && projects_->list_projects(rows) == ResultCode::Ok) {
    for (const auto& row : rows) out.push_back({row.id, row.name, row.origin, row.cloud_marked});
  }
  return out;
}

void ShellController::request_open_project(std::int64_t project_id) {
  // A project opens where it was left: the library (grid) or refine with the
  // selected photo; refine when nothing was remembered (requirement 5.3).
  // Evidence 2026-11-06-project-entry-observation.md.
  if (!projects_) return;
  const auto place = projects_->last_place(project_id);
  // The dedicated local-projects surface always enters the refine canvas;
  // the home project list keeps the user's last workspace.
  const Page landing = display_.page() == Page::LocalProjects
                           ? Page::Refine
                           : (place.workspace == project_session::Workspace::Library ? Page::Library : Page::Refine);
  if (display_.open_project(landing) != ResultCode::Ok) return;
  current_project_ = project_id;
  filter_ = {};
  selected_photo_ = place.photo_id;
  if (landing == Page::Library) {
    display_.set_library_view("grid");
  } else {
    show_selected_photo();
  }
  request_thumbnails();
  if (changed_) changed_();
}

std::vector<PhotoEntry> ShellController::photos() const {
  std::vector<PhotoEntry> out;
  std::vector<PhotoRow> rows;
  if (projects_ && current_project_ &&
      projects_->list_photos(*current_project_, rows, photo_filtered() ? &filter_ : nullptr) == ResultCode::Ok) {
    for (const auto& row : rows) {
      // Only an abnormal original (missing, delete pending) is worth a line.
      out.push_back({row.id, row.file_name, row.presence == kPresenceEditable ? std::string() : row.presence,
                     row.edit_status, projects_->copy_sequence(row.id), row.rating, row.flag, row.color_label});
    }
  }
  return out;
}

void ShellController::request_virtual_copy(const std::vector<std::int64_t>& photo_ids) {
  if (!projects_ || !current_project_ || photo_ids.empty()) return;
  std::vector<std::int64_t> created;
  for (const std::int64_t id : photo_ids) {
    std::int64_t copy = 0;
    std::string diagnostic;
    const ResultCode code = projects_->create_virtual_copy(id, copy, diagnostic);
    if (code != ResultCode::Ok) {
      qWarning("virtual copy: %s", diagnostic.c_str());
      show_notices({}, NoticeScene::OpenDocument, code);
      continue;
    }
    created.push_back(copy);
  }
  if (created.empty()) {
    if (changed_) changed_();
    return;
  }
  request_thumbnails();
  // 新副本成为选中的（图库里一眼看到它们在哪）。
  select_photos(created, created.back());
  if (changed_) changed_();
}

bool ShellController::photo_filtered() const {
  const PhotoQuery none;
  return filter_.rating_min != none.rating_min || filter_.rating_max != none.rating_max || !filter_.flags.empty() ||
         !filter_.colors.empty() || !filter_.formats.empty() || !filter_.edit_statuses.empty() ||
         !filter_.export_statuses.empty() || filter_.sort != none.sort || filter_.descending;
}

std::size_t ShellController::photo_total() const {
  std::vector<PhotoRow> rows;
  if (!projects_ || !current_project_ || projects_->list_photos(*current_project_, rows) != ResultCode::Ok) return 0;
  return static_cast<std::size_t>(std::count_if(rows.begin(), rows.end(), [](const PhotoRow& row) { return row.id > 0; }));
}

void ShellController::request_photo_filter(const PhotoQuery& query) {
  if (!projects_ || !current_project_) return;
  const PhotoQuery before = filter_;
  filter_ = query;
  std::vector<PhotoRow> rows;
  if (projects_->list_photos(*current_project_, rows, photo_filtered() ? &filter_ : nullptr) != ResultCode::Ok) {
    filter_ = before;  // 条件不合法：保持原来的
    show_notices({}, NoticeScene::OpenDocument, ResultCode::StateConflict);
    return;
  }
  // 被筛掉的照片不再选中；正在精修的那张不关（它还开着，只是不在列表里）。
  std::vector<std::int64_t> kept;
  for (const std::int64_t id : selection_) {
    if (std::any_of(rows.begin(), rows.end(), [id](const PhotoRow& row) { return row.id == id; })) kept.push_back(id);
  }
  if (kept.size() != selection_.size()) {
    selection_ = kept;
    selected_photo_ = kept.empty() ? std::nullopt : std::optional<std::int64_t>(kept.back());
    selection_changed();
  }
  request_thumbnails();
  if (changed_) changed_();
}

namespace {

// 历史步骤的名字：第一步用内核的名字，其余是“滑杆名 值”；找不到滑杆名就写能力和参数编号。
QString history_text(const preview_channel::PreviewSession::HistoryEntry& entry) {
  if (!entry.label.empty()) return QString::fromStdString(entry.label);
  if (entry.capability_id == preview_channel::PreviewSession::kPasteStep) return qtext("history.paste");
  if (entry.capability_id == preview_channel::PreviewSession::kRemoveMaskStep) return qtext("history.local.remove");
  if (entry.capability_id == preview_channel::PreviewSession::kPresetStep) {
    return qtext("history.preset").arg(QString::fromStdString(entry.display_value));
  }
  if (entry.capability_id == preview_channel::PreviewSession::kResetStep) {
    return qtext("history.reset").arg(QString::fromStdString(entry.display_value));
  }
  QString name = QString::fromStdString(entry.capability_id + "/" + entry.parameter_id);
  if (entry.capability_id == "color.local") {
    // 局部调色：带 m<N>: 前缀的是第 N 个蒙版的参数，名字取基础参数的滑杆名，前面加蒙版名。
    int instance = 0;
    std::string_view base;
    if (split_local_parameter(entry.parameter_id, instance, base)) {
      std::string_view leaf = base;
      int item = 0;
      split_local_item(base, item, leaf);
      if (leaf == "name") name = qtext("local.mask.rename");
      if (leaf == "invert") name = qtext("history.local.invert");
      if (leaf == "strokes") return qtext("history.local.strokes");  // 值是整串笔触，不写进历史名
      for (const auto& row : ui::panels::refine_local_rows()) {
        if (row.binding && row.binding->parameter_id == leaf) {
          name = qtext("local.mask.name").arg(instance) + QLatin1Char(' ') + QString::fromStdString(row.label);
          break;
        }
      }
    }
  }
  auto rows = ui::panels::refine_color_rows();
  for (auto& row : ui::panels::refine_post_color_rows()) rows.push_back(std::move(row));
  for (const auto& row : rows) {
    if (row.binding && row.binding->capability_id == entry.capability_id &&
        row.binding->parameter_id == entry.parameter_id) {
      name = QString::fromStdString(row.label);
      if (entry.capability_id == "color.post") name = qtext("history.post").arg(name);
      break;
    }
  }
  return qtext("history.step").arg(name, QString::fromStdString(entry.display_value));
}

}  // namespace

std::vector<HistoryRow> ShellController::history() const {
  std::vector<HistoryRow> out;
  if (!session_) return out;
  const std::size_t current = session_->history_current();
  const auto steps = session_->history();
  for (std::size_t index = 0; index < steps.size(); ++index) out.push_back({history_text(steps[index]), index == current});
  return out;
}

void ShellController::history_changed() {
  for (auto it = history_watchers_.begin(); it != history_watchers_.end();) {
    if (it->first.isNull()) {
      it = history_watchers_.erase(it);
      continue;
    }
    it->second();
    ++it;
  }
}

void ShellController::watch_history(QObject* owner, std::function<void()> changed) {
  history_watchers_.emplace_back(owner, std::move(changed));
}

void ShellController::request_history_step(std::size_t index) {
  if (!session_) return;
  std::string diagnostic;
  const ResultCode code = session_->select_history(index, diagnostic);
  if (code != ResultCode::Ok) qWarning("history: %s", diagnostic.c_str());
  else mark_high_quality_preview_request();
  view_.status = qtext("refine.preview.loading");
  if (changed_) changed_();  // the sliders show the document as it is now
}

void ShellController::request_undo() {
  if (!session_) return;
  std::string diagnostic;
  if (session_->undo(diagnostic) != ResultCode::Ok) return;  // nothing to undo is not an error
  mark_high_quality_preview_request();
  view_.status = qtext("refine.preview.loading");
  if (changed_) changed_();
}

void ShellController::request_redo() {
  if (!session_) return;
  std::string diagnostic;
  if (session_->redo(diagnostic) != ResultCode::Ok) return;
  mark_high_quality_preview_request();
  view_.status = qtext("refine.preview.loading");
  if (changed_) changed_();
}

void ShellController::request_reset(ResetScope scope) {
  if (!session_) return;
  std::vector<std::string> capabilities;
  const auto descriptors = CapabilityRegistry::phase_one().descriptors();
  const auto selected = [scope](const std::string& id) {
    switch (scope) {
      case ResetScope::All:
        return true;
      case ResetScope::Portrait:
        return id.rfind("portrait.", 0) == 0 || id == "manual.liquify";
      case ResetScope::Color:
        return id == "color.match" || id == "color.style" || id == "color.global" || id == "color.local" ||
               id == "detail";
      case ResetScope::Post:
        return id == "color.post";
    }
    return false;
  };
  for (const auto& descriptor : descriptors) {
    if (selected(descriptor.id)) capabilities.push_back(descriptor.id);
  }
  const char* label_key = "refine.reset.all";
  if (scope == ResetScope::Portrait) label_key = "refine.reset.portrait";
  if (scope == ResetScope::Color) label_key = "refine.reset.color";
  if (scope == ResetScope::Post) label_key = "refine.reset.post";
  std::string diagnostic;
  if (session_->reset_effects(capabilities, qtext(label_key).toStdString(), diagnostic) != ResultCode::Ok) {
    qWarning("reset effects: %s", diagnostic.c_str());
    return;
  }
  mark_high_quality_preview_request();
  view_.status = qtext("refine.preview.loading");
  if (changed_) changed_();
}

namespace {
bool contains(const std::vector<std::int64_t>& items, std::int64_t id) {
  return std::find(items.begin(), items.end(), id) != items.end();
}
}  // namespace

void ShellController::refresh_after_effect(const std::vector<std::int64_t>& items) {
  for (const std::int64_t id : items) request_thumbnail(id);
  if (changed_) changed_();
}

void ShellController::request_copy_effect() {
  if (!projects_ || !current_project_) return;
  const auto source = display_.page() == Page::Library ? selected_photo_ : current_photo_;
  if (!source) return;
  EffectClipboard clipboard;
  if (projects_->copy_effect(*source, clipboard) != ResultCode::Ok) {
    show_notices({}, NoticeScene::OpenDocument, ResultCode::StateConflict);
    if (changed_) changed_();
    return;
  }
  clipboard_ = std::move(clipboard);
}

std::vector<int> ShellController::local_masks() const {
  return session_ ? session_->local_mask_instances() : std::vector<int>{};
}

void ShellController::request_remove_local_mask(int instance) {
  if (!session_) return;
  std::string diagnostic;
  const ResultCode code = session_->remove_local_mask(instance, diagnostic);  // a step of its own in the history
  if (code != ResultCode::Ok) show_notices({}, NoticeScene::OpenDocument, code);
  else mark_high_quality_preview_request();
  view_.status = qtext("refine.preview.loading");
}

void ShellController::request_paste_effect() {
  if (!projects_ || !current_project_ || !clipboard_) return;
  const std::vector<std::int64_t> targets =
      display_.page() == Page::Library ? selected_photos()
                                       : (current_photo_ ? std::vector<std::int64_t>{*current_photo_}
                                                         : std::vector<std::int64_t>{});
  if (targets.empty()) return;
  std::vector<std::int64_t> others;
  ResultCode failure = ResultCode::Ok;
  for (const std::int64_t id : targets) {
    if (session_ && current_photo_ == id && display_.page() == Page::Refine) {
      std::string diagnostic;
      const ResultCode code = session_->paste(*clipboard_, diagnostic);  // a step of its own in the history
      if (code != ResultCode::Ok) failure = code;
      else mark_high_quality_preview_request();
      view_.status = qtext("refine.preview.loading");
    } else {
      others.push_back(id);
    }
  }
  std::vector<project_session::ProjectSession::EffectOutcome> outcomes;
  const bool reopen = session_ && current_photo_ && contains(others, *current_photo_);
  const auto reopened = current_photo_;
  if (reopen) close_photo();
  if (!others.empty() && projects_->paste_effect(*clipboard_, others, outcomes) != ResultCode::Ok) {
    failure = ResultCode::StateConflict;
  }
  if (failure != ResultCode::Ok) show_notices({}, NoticeScene::OpenDocument, failure);
  if (reopen && reopened) open_project_photo(*reopened);
  refresh_after_effect(others);
}

std::vector<project_session::ProjectSession::PresetRow> ShellController::presets() const {
  return projects_ ? projects_->list_presets() : std::vector<project_session::ProjectSession::PresetRow>{};
}

std::vector<PageActions::PresetCapability> ShellController::adjusted_preset_capabilities() const {
  std::vector<PresetCapability> out;
  const auto source = display_.page() == Page::Library ? selected_photo_ : current_photo_;
  if (!projects_ || !source) return out;
  static const auto registry = CapabilityRegistry::phase_one();
  for (const auto& id : projects_->adjusted_capabilities(*source)) {
    const auto* descriptor = registry.find(id);
    if (descriptor != nullptr) out.push_back({id, descriptor->module_label, descriptor->label});
  }
  return out;
}

bool ShellController::request_save_preset(const project_session::ProjectSession::PresetDraft& draft,
                                          std::string& problem) {
  problem.clear();
  const auto source = display_.page() == Page::Library ? selected_photo_ : current_photo_;
  if (!projects_ || !source) return false;
  const ResultCode code = projects_->save_preset(*source, draft, problem);
  if (code == ResultCode::Ok && changed_) changed_();
  return code == ResultCode::Ok;
}

void ShellController::request_rename_preset(const std::string& id, const std::string& name) {
  if (!projects_) return;
  const ResultCode code = projects_->rename_preset(id, name);
  if (code != ResultCode::Ok) show_notices({}, NoticeScene::OpenDocument, code);
  if (changed_) changed_();
}

void ShellController::request_delete_preset(const std::string& id) {
  if (!projects_) return;
  const ResultCode code = projects_->delete_preset(id);
  if (code != ResultCode::Ok) show_notices({}, NoticeScene::OpenDocument, code);
  if (changed_) changed_();
}

bool ShellController::preset_overwrites_color(const std::string& id) const {
  const auto source = display_.page() == Page::Library ? selected_photo_ : current_photo_;
  return projects_ && source && projects_->preset_overwrites_color(*source, id);
}

void ShellController::request_apply_preset(const std::string& id) {
  if (!projects_ || !current_project_) return;
  const Preset* preset = projects_->preset_library().find(id);
  if (preset == nullptr) return;
  const std::vector<std::int64_t> targets =
      display_.page() == Page::Library ? selected_photos()
                                       : (current_photo_ ? std::vector<std::int64_t>{*current_photo_}
                                                         : std::vector<std::int64_t>{});
  if (targets.empty()) return;
  std::vector<std::int64_t> others;
  ResultCode failure = ResultCode::Ok;
  for (const std::int64_t target : targets) {
    if (session_ && current_photo_ == target && display_.page() == Page::Refine) {
      std::string diagnostic;
      const ResultCode code = session_->apply_preset(projects_->preset_library(), *preset, diagnostic);
      if (code != ResultCode::Ok) failure = code;
      else mark_high_quality_preview_request();
      view_.status = qtext("refine.preview.loading");
    } else {
      others.push_back(target);
    }
  }
  std::vector<project_session::ProjectSession::EffectOutcome> outcomes;
  const bool reopen = session_ && current_photo_ && contains(others, *current_photo_);
  const auto reopened = current_photo_;
  if (reopen) close_photo();
  if (!others.empty() && projects_->apply_preset(id, others, outcomes) != ResultCode::Ok) failure = ResultCode::StateConflict;
  if (failure != ResultCode::Ok) show_notices({}, NoticeScene::OpenDocument, failure);
  if (reopen && reopened) open_project_photo(*reopened);
  refresh_after_effect(others);
}

std::size_t ShellController::sync_target_count() const {
  const auto source = display_.page() == Page::Library ? selected_photo_ : current_photo_;
  if (!source) return 0;
  std::size_t count = 0;
  for (const std::int64_t id : selection_) count += id != *source && projects_ != nullptr && projects_->source_photo(id) != 0;
  return count;
}

std::size_t ShellController::request_sync(const std::vector<std::string>& checked_ids) {
  if (!projects_ || !current_project_) return 0;
  const auto source = display_.page() == Page::Library ? selected_photo_ : current_photo_;
  if (!source) return 0;
  std::vector<std::int64_t> targets;
  for (const std::int64_t id : selection_) {
    if (id != *source) targets.push_back(id);
  }
  if (targets.empty()) return 0;
  const bool reopen = session_ && current_photo_ && contains(targets, *current_photo_);
  const auto reopened = current_photo_;
  if (reopen) close_photo();
  std::vector<project_session::ProjectSession::EffectOutcome> outcomes;
  projects_->sync_effect(*source, checked_ids, targets, outcomes);
  std::size_t failed = 0;
  for (const auto& outcome : outcomes) failed += outcome.code != ResultCode::Ok;
  if (failed != 0) show_notices({}, NoticeScene::OpenDocument, ResultCode::StateConflict);
  if (reopen && reopened) open_project_photo(*reopened);
  refresh_after_effect(targets);
  return failed;
}

void ShellController::request_rating(const std::vector<std::int64_t>& photo_ids, int rating) {
  if (!projects_ || photo_ids.empty()) return;
  const ResultCode code = projects_->set_rating(photo_ids, rating);
  if (code != ResultCode::Ok) show_notices({}, NoticeScene::OpenDocument, code);
  if (changed_) changed_();
}

void ShellController::request_flag(const std::vector<std::int64_t>& photo_ids, const std::string& flag) {
  if (!projects_ || photo_ids.empty()) return;
  const ResultCode code = projects_->set_flag(photo_ids, flag);
  if (code != ResultCode::Ok) show_notices({}, NoticeScene::OpenDocument, code);
  if (changed_) changed_();
}

void ShellController::request_color(const std::vector<std::int64_t>& photo_ids, const std::string& color) {
  if (!projects_ || photo_ids.empty()) return;
  const ResultCode code = projects_->set_color(photo_ids, color);
  if (code != ResultCode::Ok) show_notices({}, NoticeScene::OpenDocument, code);
  if (changed_) changed_();
}

void ShellController::request_delete_photos(const std::vector<std::int64_t>& photo_ids) {
  if (!projects_ || !current_project_ || photo_ids.empty()) return;
  std::string diagnostic;
  const ResultCode code = projects_->trash_items(photo_ids, diagnostic);
  if (code != ResultCode::Ok) show_notices({}, NoticeScene::OpenDocument, code);
  const auto gone = [&photo_ids](std::int64_t id) {
    return std::find(photo_ids.begin(), photo_ids.end(), id) != photo_ids.end();
  };
  // 原图删了，它的副本也跟着走：以列表为准，不在列表里的都不再选、不再打开。
  std::vector<std::int64_t> alive;
  for (const auto& entry : photos()) alive.push_back(entry.id);
  const auto present = [&alive](std::int64_t id) { return std::find(alive.begin(), alive.end(), id) != alive.end(); };
  if (current_photo_ && (gone(*current_photo_) || !present(*current_photo_))) close_photo();
  std::vector<std::int64_t> kept;
  for (const std::int64_t id : selection_) {
    if (present(id)) kept.push_back(id);
  }
  selection_ = kept;
  selected_photo_ = kept.empty() ? std::nullopt : std::optional<std::int64_t>(kept.back());
  remember_place();
  selection_changed();
  if (changed_) changed_();
}

void ShellController::request_delete_project(std::int64_t project_id) {
  if (!projects_) return;
  std::string diagnostic;
  const ResultCode code = projects_->trash_project(project_id, diagnostic);
  if (code != ResultCode::Ok) show_notices({}, NoticeScene::OpenDocument, code);
  if (changed_) changed_();
}

std::vector<TrashItem> ShellController::trash() const {
  std::vector<TrashItem> out;
  std::vector<project_session::ProjectSession::TrashEntry> entries;
  if (!projects_ || projects_->list_trash(entries) != ResultCode::Ok) return out;
  for (const auto& entry : entries) {
    TrashItem item;
    item.kind = entry.subject == kTrashProject ? TrashKind::Project
                                               : (entry.subject == kTrashVirtualCopy ? TrashKind::Copy : TrashKind::Photo);
    item.id = entry.id;
    item.name = entry.name;
    item.project = entry.project_name;
    item.copy = entry.copy;
    item.days_left = entry.days_left;
    out.push_back(std::move(item));
  }
  return out;
}

void ShellController::request_restore(TrashKind kind, std::int64_t id) {
  if (!projects_) return;
  project_session::ProjectSession::TrashEntry entry;
  entry.subject = kind == TrashKind::Project ? kTrashProject : (kind == TrashKind::Copy ? kTrashVirtualCopy : kTrashPhoto);
  entry.id = id;
  std::string diagnostic;
  const ResultCode code = projects_->restore(entry, diagnostic);
  if (code != ResultCode::Ok) show_notices({}, NoticeScene::OpenDocument, code);
  request_thumbnails();
  if (changed_) changed_();
}

void ShellController::request_erase_project(std::int64_t project_id) {
  if (!projects_) return;
  std::string diagnostic;
  const ResultCode code = projects_->erase_trashed_project(project_id, diagnostic);
  if (code != ResultCode::Ok) show_notices({}, NoticeScene::OpenDocument, code);
  if (changed_) changed_();
}

void ShellController::request_open_photo(std::int64_t photo_id) {
  if (!projects_ || !current_project_) return;
  // Navigate first: a page change clears notices, and opening may post one.
  if (display_.page() != Page::Refine) display_.navigate(Page::Refine);
  open_project_photo(photo_id);
  remember_place();
  if (changed_) changed_();
}

void ShellController::open_project_photo(std::int64_t photo_id) {
  close_photo();
  current_photo_ = photo_id;
  selected_photo_ = photo_id;
  std::string path;
  std::string diagnostic;
  const ResultCode resolved = projects_->resolve_photo(photo_id, path, diagnostic);
  if (resolved != ResultCode::Ok) {
    view_.status = qtext("refine.preview.failed") + QString::fromStdString(diagnostic);
    display_.post_notice(notice_for(NoticeScene::OpenDocument, resolved));
    return;
  }
  auto options = preview_options_;
  options.max_edge = preview_max_edge_;
  options.document_directory = projects_->document_directory();
  options.document_id = projects_->document_name(photo_id);
  options.layer_document_path = projects_->layer_document_path(photo_id);
  if (open_photo(path, options, diagnostic) == ResultCode::Ok) {
    session_->on_committed([this, photo_id] {
      projects_->mark_edited(photo_id);
      request_thumbnail(photo_id);  // the thumbnail follows the committed edit
      history_changed();
    });
  }
}

ResultCode ShellController::open_photo(const std::string& photo_path,
                                       const preview_channel::PreviewSessionOptions& options,
                                       std::string& diagnostic) {
  notifier_.reset();
  session_.reset();
  view_ = {};
  histogram_refresh_gate_.reset();
  guides_.clear();
  guides_visible_ = true;
  guides_changed();
  const ResultCode code = preview_channel::PreviewSession::open(photo_path, options, session_, diagnostic);
  if (code != ResultCode::Ok) {
    view_.status = qtext("refine.preview.failed") + QString::fromStdString(diagnostic);
    publish();
    return code;
  }
  notifier_ = std::make_unique<QSocketNotifier>(session_->fd(), QSocketNotifier::Read);
  QObject::connect(notifier_.get(), &QSocketNotifier::activated, notifier_.get(), [this] { read_frames(); });
  session_->on_committed([this] { history_changed(); });  // a project photo replaces this with its own
  // First frame: the committed document as it is.
  const ui::panels::SliderBinding exposure{"color.global", "exposure"};
  const auto current = parameter_value(exposure);
  request_parameter(exposure, current ? *current : ui::panels::slider_spec("", exposure).default_text, false);
  original_preview_visible_ = false;
  original_sequence_.reset();
  original_requests_.clear();
  original_edge_ = 0;
  original_image_ = {};
  last_edit_sequence_ = session_->latest_sequence();
  last_edit_frame_sequence_ = 0;
  last_edit_interactive_ = false;
  view_.original_image = {};
  view_.showing_original = false;
  view_.status = qtext("refine.preview.loading");
  publish();
  return ResultCode::Ok;
}

void ShellController::request_preview_max_edge(std::uint32_t max_edge) {
  if (max_edge != 2000 && max_edge != 3000 && max_edge != 4000) return;
  preview_max_edge_ = max_edge;
  preview_options_.max_edge = max_edge;
  if (session_) {
    session_->set_preview_max_edge(max_edge);
    std::string diagnostic;
    const ResultCode code = session_->refresh_preview(diagnostic);
    if (code != ResultCode::Ok) qWarning("preview max edge %u: %s", max_edge, diagnostic.c_str());
    else {
      last_edit_sequence_ = session_->latest_sequence();
      last_edit_interactive_ = false;
      histogram_refresh_gate_.request_high_quality(session_->latest_sequence());
    }
  }
  view_.histogram_frozen = histogram_refresh_gate_.frozen();
  if (changed_) changed_();
}

std::optional<std::string> ShellController::parameter_value(const ui::panels::SliderBinding& binding) const {
  if (!session_) return std::nullopt;
  return session_->document().effective_parameter(binding.capability_id, binding.parameter_id);
}

void ShellController::request_parameter(const ui::panels::SliderBinding& binding, const std::string& display_value,
                                        bool commit) {
  if (!session_) return;
  const bool first_request = session_->latest_sequence() == 0;
  std::string diagnostic;
  const ResultCode code = commit ? session_->commit(binding.capability_id, binding.parameter_id, display_value, diagnostic)
                                 : session_->preview(binding.capability_id, binding.parameter_id, display_value, diagnostic);
  if (code != ResultCode::Ok) {
    qWarning("preview %s/%s=%s: %s", binding.capability_id.c_str(), binding.parameter_id.c_str(),
             display_value.c_str(), diagnostic.c_str());
    return;
  }
  last_edit_sequence_ = session_->latest_sequence();
  last_edit_interactive_ = !commit && !first_request;
  histogram_refresh_gate_.request(session_->latest_sequence(), commit, first_request);
  const bool hint_changed = view_.histogram_frozen != histogram_refresh_gate_.frozen();
  view_.histogram_frozen = histogram_refresh_gate_.frozen();
  if (hint_changed) publish();
}

void ShellController::request_parameters(const std::vector<std::pair<ui::panels::SliderBinding, std::string>>& values) {
  if (!session_ || values.empty()) return;
  const bool first_request = session_->latest_sequence() == 0;
  const std::string capability = values.front().first.capability_id;
  std::vector<std::pair<std::string, std::string>> items;
  for (const auto& [binding, value] : values) {
    if (binding.capability_id != capability) return;  // 一次只提交一个能力
    items.emplace_back(binding.parameter_id, value);
  }
  std::string diagnostic;
  const ResultCode code = session_->commit_parameters(capability, items, diagnostic);
  if (code != ResultCode::Ok) {
    qWarning("commit %s: %s", capability.c_str(), diagnostic.c_str());
    return;
  }
  last_edit_sequence_ = session_->latest_sequence();
  last_edit_interactive_ = false;
  histogram_refresh_gate_.request(session_->latest_sequence(), true, first_request);
  const bool hint_changed = view_.histogram_frozen != histogram_refresh_gate_.frozen();
  view_.histogram_frozen = histogram_refresh_gate_.frozen();
  if (hint_changed) publish();
}

void ShellController::request_canvas_view_mode(CanvasViewMode mode) {
  if (canvas_view_mode_ == mode) return;
  canvas_view_mode_ = mode;
  if (mode != CanvasViewMode::Compare) {
    original_preview_visible_ = false;
    view_.showing_original = false;
    view_.original_image = original_image_;
    view_.histogram_frozen = histogram_refresh_gate_.frozen();
  }
  if (changed_) changed_();
}

void ShellController::request_original_frame(bool interactive) {
  if (!session_) return;
  std::string diagnostic;
  if (session_->request_original_preview(interactive, diagnostic) != ResultCode::Ok) {
    qWarning("original preview request failed: %s", diagnostic.c_str());
    return;
  }
  original_sequence_ = session_->latest_sequence();
  original_edge_ = interactive && preview_options_.interactive_max_edge != 0
                       ? std::min(preview_options_.interactive_max_edge, preview_max_edge_)
                       : preview_max_edge_;
  original_requests_[*original_sequence_] = original_edge_;
  while (original_requests_.size() > 8) original_requests_.erase(original_requests_.begin());
}

void ShellController::request_original_preview(bool visible) {
  if (canvas_view_mode_ != CanvasViewMode::Compare || !session_) return;
  if (!visible) {
    if (!original_preview_visible_) return;
    original_preview_visible_ = false;
    view_.showing_original = false;
    view_.original_image = original_image_;
    view_.histogram_frozen = histogram_refresh_gate_.frozen();
    // Promote any interactive edit request that happened while the baseline
    // was held to the selected high-quality edge before showing it again.
    if (last_edit_sequence_ != 0 && last_edit_interactive_) {
      std::string diagnostic;
      if (session_->refresh_preview(diagnostic) == ResultCode::Ok) {
        last_edit_sequence_ = session_->latest_sequence();
        last_edit_interactive_ = false;
        histogram_refresh_gate_.request_high_quality(last_edit_sequence_);
        view_.histogram_frozen = histogram_refresh_gate_.frozen();
      } else {
        qWarning("edit preview restore failed: %s", diagnostic.c_str());
      }
    }
    publish();
    return;
  }
  if (original_preview_visible_) return;
  original_preview_visible_ = true;
  const bool interactive = last_edit_interactive_;
  const std::uint32_t wanted_edge = interactive && preview_options_.interactive_max_edge != 0
                                        ? std::min(preview_options_.interactive_max_edge, preview_max_edge_)
                                        : preview_max_edge_;
  if (!original_image_.isNull() && original_edge_ == wanted_edge) {
    view_.original_image = original_image_;
    view_.showing_original = true;
    view_.histogram_frozen = true;
    publish();
    return;
  }
  if (original_edge_ != wanted_edge) original_image_ = {};
  view_.original_image = original_image_;
  view_.showing_original = !original_image_.isNull();
  view_.histogram_frozen = true;
  request_original_frame(interactive);
  publish();
}

void ShellController::request_create_guide(bool vertical) {
  if (!session_ || display_.page() != Page::Refine || !guides_visible_) return;
  // Every new guide starts at the center of the canvas.  Overlapping guides
  // are valid; the view selects the topmost one when several share a position.
  guides_.push_back(GuideLine{vertical, 50.0});
  guides_visible_ = true;
  guides_changed();
}

void ShellController::request_toggle_guides() {
  if (!session_ || display_.page() != Page::Refine) return;
  guides_visible_ = !guides_visible_;
  guides_changed();
}

void ShellController::request_move_guide(std::size_t index, double position) {
  if (!session_ || index >= guides_.size() || !std::isfinite(position)) return;
  guides_[index].position = std::clamp(position, 0.0, 100.0);
  guides_changed();
}

void ShellController::request_remove_guide(std::size_t index) {
  if (!session_ || index >= guides_.size()) return;
  guides_.erase(guides_.begin() + static_cast<std::ptrdiff_t>(index));
  guides_changed();
}

void ShellController::request_remove_all_guides() {
  if (!session_ || guides_.empty()) return;
  guides_.clear();
  guides_changed();
}

void ShellController::watch_guides(QObject* owner,
                                   std::function<void(const std::vector<GuideLine>&, bool)> changed) {
  guide_watchers_.emplace_back(owner, std::move(changed));
  guide_watchers_.back().second(guides_, guides_visible_);
}

void ShellController::guides_changed() {
  for (auto it = guide_watchers_.begin(); it != guide_watchers_.end();) {
    if (it->first.isNull()) {
      it = guide_watchers_.erase(it);
      continue;
    }
    it->second(guides_, guides_visible_);
    ++it;
  }
}

void ShellController::watch_preview(QObject* owner, std::function<void(const PreviewView&)> show) {
  watcher_owner_ = owner;
  watcher_ = std::move(show);
  // A rebuilt refine page gets a fresh histogram widget. Replay the cached
  // snapshot once on registration even when the last frame was already
  // published to the previous widget.
  view_.histogram_changed = true;
  publish();
}

void ShellController::mark_high_quality_preview_request() {
  if (!session_) return;
  last_edit_sequence_ = session_->latest_sequence();
  last_edit_interactive_ = false;
  histogram_refresh_gate_.request_high_quality(session_->latest_sequence());
  const bool hint_changed = view_.histogram_frozen != histogram_refresh_gate_.frozen();
  view_.histogram_frozen = histogram_refresh_gate_.frozen();
  if (hint_changed) publish();
}

void ShellController::publish() {
  if (watcher_ && watcher_owner_) {
    watcher_(view_);
    view_.histogram_changed = false;
  }
}

void ShellController::read_frames() {
  if (!session_) return;
  bool changed = false;
  std::optional<preview_channel::PreviewFrame> latest_frame;
  for (;;) {
    preview_channel::PreviewFrame frame;
    std::string diagnostic;
    const ResultCode code = session_->take_frame(frame, diagnostic);
    if (code == ResultCode::Cancelled) {
      // A stale frame was dropped: look for the next one; otherwise nothing is waiting.
      if (diagnostic.find("older") != std::string::npos) continue;
      break;
    }
    if (code != ResultCode::Ok) {
      // renderd went away: keep the last frame, say why, stop watching.
      notifier_.reset();
      view_.status = qtext("refine.preview.failed") + QString::fromStdString(diagnostic);
      changed = true;
      break;
    }
    if (frame.result.code != ResultCode::Ok) {
      const auto original_request = original_requests_.find(frame.result.client_sequence);
      const bool original = original_request != original_requests_.end();
      if (original) {
        const bool current_original = original_sequence_ && frame.result.client_sequence == *original_sequence_;
        original_requests_.erase(original_request);
        if (current_original) original_sequence_.reset();
        if (!original_preview_visible_) {
          view_.status = qtext("refine.preview.failed") + QString::fromStdString(frame.result.diagnostic);
        }
      } else {
        latest_frame.reset();
        view_.status = qtext("refine.preview.failed") +
                       QString::fromStdString(frame.result.diagnostic);
      }
      changed = true;
      continue;
    }
    const auto original_request = original_requests_.find(frame.result.client_sequence);
    if (original_request != original_requests_.end()) {
      const bool current_original = original_sequence_ && frame.result.client_sequence == *original_sequence_;
      const std::uint32_t request_edge = original_request->second;
      original_requests_.erase(original_request);
      if (!current_original) continue;
      const auto& header = frame.result.header;
      QImage image(frame.rgba.data(), static_cast<int>(header.width), static_cast<int>(header.height),
                   static_cast<qsizetype>(header.stride), QImage::Format_RGBA8888_Premultiplied);
      original_image_ = image.copy();
      original_sequence_.reset();
      original_edge_ = request_edge;
      view_.original_image = original_image_;
      view_.showing_original = original_preview_visible_;
      view_.histogram_frozen = true;
      view_.status.clear();
      changed = true;
      continue;
    }
    const std::uint32_t frame_edge = std::max(frame.result.header.width, frame.result.header.height);
    const bool high_quality_frame = preview_options_.interactive_max_edge == 0 ||
                                    frame_edge > preview_options_.interactive_max_edge;
    if (histogram_refresh_gate_.accept_frame(frame.result.client_sequence, high_quality_frame)) {
      view_.histogram = ui::panels::rgb_histogram(frame.rgba.data(), frame.result.header.width,
                                                  frame.result.header.height, frame.result.header.stride,
                                                  frame.rgba.size());
      view_.canvas_size = QSize(static_cast<int>(frame.result.header.width),
                                static_cast<int>(frame.result.header.height));
      view_.histogram_changed = true;
    }
    view_.histogram_frozen = original_preview_visible_ ? true : histogram_refresh_gate_.frozen();
    last_edit_frame_sequence_ = frame.result.client_sequence;
    // Drain all frames first and process only the newest one. During a fast
    // drag, copying a stale 12 MP image and rebuilding its histogram here can
    // otherwise keep the UI busy after renderd has already produced a newer
    // value. The preview channel already guarantees monotonically newer
    // sequences, so keeping the last successful frame preserves the latest
    // value while dropping obsolete display work.
    latest_frame = std::move(frame);
    changed = true;
  }
  if (latest_frame.has_value()) {
    const auto& frame = *latest_frame;
    const auto& header = frame.result.header;
    QImage image(frame.rgba.data(), static_cast<int>(header.width), static_cast<int>(header.height),
                 static_cast<qsizetype>(header.stride), QImage::Format_RGBA8888_Premultiplied);
    view_.image = image.copy();  // own the pixels; frame.rgba goes away
    view_.original_image = original_image_;
    view_.showing_original = original_preview_visible_ && !original_image_.isNull();
    view_.status.clear();
    for (const auto& note : frame.result.diagnostics) {
      qInfo("preview note %s: %s", note.capability_id.c_str(), note.reason.c_str());
    }
    ++frames_shown_;
  }
  if (changed) publish();
}

void ShellController::attach_exports(const SessionState& session, export_session::ExportOptions options) {
  if (!projects_) return;
  std::string diagnostic;
  if (export_session::ExportSession::open(*projects_, session, std::move(options), exports_, diagnostic) !=
      ResultCode::Ok) {
    qWarning("exports unavailable: %s", diagnostic.c_str());
    return;
  }
  // Tasks queued by an earlier session (or left by a crash, now failed and retryable).
  if (exports_->resume(diagnostic) != ResultCode::Ok) qWarning("export resume: %s", diagnostic.c_str());
  watch_exportd();
}

void ShellController::attach_generation(const SessionState& session,
                                        generation_session::GenerationSessionOptions options) {
  stop_assistant_generation_worker();
  assistant_generation_lifetime_ = std::make_shared<int>(0);
  assistant_generation_entry_ = {};
  assistant_generation_history_.clear();
  assistant_generation_fixture_ = options.process.fixture;
  generation_.reset();
  generation_status_.clear();
  generation_tool_id_.clear();
  generation_parameter_values_.clear();
  generation_retry_draft_.reset();
  generation_use_retry_draft_ = false;
  if (!projects_) return;
  std::string diagnostic;
  if (generation_session::GenerationSession::open(session, std::move(options), generation_, diagnostic) !=
      ResultCode::Ok) {
    qWarning("generation unavailable: %s", diagnostic.c_str());
    generation_status_ = "unavailable";
  } else if (!generation_catalog_.descriptors().empty()) {
    generation_tool_id_ = generation_catalog_.descriptors().front().spec.tool_id;
    (void)generation_catalog_.default_parameters(generation_tool_id_, generation_parameter_values_);
  }
}

std::vector<GenerationToolEntry> ShellController::generation_tools() const {
  std::vector<GenerationToolEntry> entries;
  if (!generation_) return entries;
  for (const auto& descriptor : generation_catalog_.descriptors()) {
    entries.push_back({descriptor.spec.tool_id, descriptor.title, descriptor.category,
                       descriptor.spec.tool_id == generation_tool_id_});
  }
  return entries;
}

void ShellController::request_generation_tool(const std::string& tool_id) {
  if (!generation_ || generation_catalog_.find(tool_id) == nullptr || tool_id == generation_tool_id_) return;
  std::vector<std::pair<std::string, std::string>> defaults;
  if (generation_catalog_.default_parameters(tool_id, defaults) != ResultCode::Ok) return;
  generation_tool_id_ = tool_id;
  generation_parameter_values_ = std::move(defaults);
  generation_status_.clear();
  if (changed_) changed_();
}

bool ShellController::generation_cancel_available() const {
  return generation_ != nullptr && assistant_generation_busy_ && !assistant_generation_task_id_.empty();
}

void ShellController::request_generation_cancel() {
  if (!generation_cancel_available()) return;
  assistant_generation_cancel_requested_.store(true, std::memory_order_release);
  generation_status_ = "cancelling";
  assistant_generation_entry_.status = "cancelling";
  if (changed_) changed_();
}

std::vector<GenerationTaskEntry> ShellController::generation_tasks() const {
  std::vector<GenerationTaskEntry> entries;
  if (!generation_) return entries;
  if (assistant_generation_busy_) {
    entries.push_back({assistant_generation_task_id_, assistant_generation_entry_.status});
    return entries;
  }
  for (const auto& [task_id, status] : generation_->task_statuses()) {
    entries.push_back({task_id, status});
  }
  return entries;
}

std::vector<GenerationHistoryEntry> ShellController::generation_history() const {
  std::vector<GenerationHistoryEntry> entries;
  if (!generation_) return entries;
  const auto& history = assistant_generation_busy_ ? assistant_generation_history_ : generation_->history();
  for (const auto& entry : history) {
    entries.push_back({entry.task_id, entry.tool_id, entry.status, entry.results.size(), entry.source_revision});
  }
  return entries;
}

bool ShellController::generation_retry_available() const {
  if (!generation_) return false;
  if (generation_status_ == "failed" && generation_retry_draft_ && projects_ && session_ && selected_photo_ &&
      generation_retry_draft_->image_id == projects_->image_id(*selected_photo_) &&
      generation_retry_draft_->source_revision == session_->document().revision()) {
    return true;
  }
  if (!projects_ || !session_ || !selected_photo_) return false;
  const std::string image_id = projects_->image_id(*selected_photo_);
  const std::uint64_t revision = session_->document().revision();
  const auto& history = generation_->history();
  for (auto it = history.rbegin(); it != history.rend(); ++it) {
    if (it->status != generation_session::kGenerationHistoryFailedStatus || it->image_id != image_id ||
        it->source_revision != revision ||
        generation_catalog_.find(it->tool_id) == nullptr) {
      continue;
    }
    std::vector<std::pair<std::string, std::string>> values;
    if (generation_catalog_.decode_parameters(it->tool_id, it->parameter_payload, values) == ResultCode::Ok) return true;
  }
  return false;
}

std::vector<GenerationParameterEntry> ShellController::generation_parameters() const {
  std::vector<GenerationParameterEntry> entries;
  if (!generation_) return entries;
  const auto* selected = generation_catalog_.find(generation_tool_id_);
  if (selected == nullptr) return entries;
  for (const auto& parameter : selected->parameters) {
    const auto found = std::find_if(generation_parameter_values_.begin(), generation_parameter_values_.end(),
                                    [&](const auto& value) { return value.first == parameter.id; });
    const std::string type = parameter.type == generate::ToolParameterType::Choice
                                 ? "choice"
                                 : parameter.type == generate::ToolParameterType::Number
                                       ? "number"
                                       : parameter.type == generate::ToolParameterType::Boolean ? "boolean" : "text";
    entries.push_back({parameter.id,
                       parameter.title,
                       type,
                       found == generation_parameter_values_.end() ? parameter.default_value : found->second,
                       parameter.choices,
                       parameter.max_length,
                       parameter.required});
  }
  return entries;
}

void ShellController::request_generation_parameter(const std::string& id, const std::string& value) {
  if (!generation_) return;
  auto values = generation_parameter_values_;
  if (values.empty()) {
    (void)generation_catalog_.default_parameters(generation_tool_id_, values);
  }
  bool found = false;
  for (auto& entry : values) {
    if (entry.first == id) {
      entry.second = value;
      found = true;
      break;
    }
  }
  if (!found) return;
  std::vector<std::uint8_t> encoded;
  if (generation_catalog_.encode_parameters(generation_tool_id_, values, encoded) != ResultCode::Ok) {
    return;
  }
  generation_parameter_values_ = std::move(values);
  if (changed_) changed_();
}

void ShellController::request_generation_retry() {
  if (!generation_ || !projects_ || !session_ || !selected_photo_) return;
  if (!generation_retry_draft_) {
    const std::string image_id = projects_->image_id(*selected_photo_);
    const std::uint64_t revision = session_->document().revision();
    const auto& history = generation_->history();
    for (auto it = history.rbegin(); it != history.rend(); ++it) {
      if (it->status != generation_session::kGenerationHistoryFailedStatus || it->image_id != image_id ||
          it->source_revision != revision) continue;
      std::vector<std::pair<std::string, std::string>> values;
      if (generation_catalog_.decode_parameters(it->tool_id, it->parameter_payload, values) != ResultCode::Ok) continue;
      generation_retry_draft_ = GenerationDraft{it->tool_id, it->image_id, it->source_revision, std::move(values),
                                                it->mask_ids, it->reference_image_ids};
      break;
    }
  }
  if (!generation_retry_draft_) {
    request_generation();
    return;
  }
  const auto& draft = *generation_retry_draft_;
  if (projects_->image_id(*selected_photo_) != draft.image_id || session_->document().revision() != draft.source_revision) {
    generation_status_ = "retry-source-changed";
    if (changed_) changed_();
    return;
  }
  generation_tool_id_ = draft.tool_id;
  generation_parameter_values_ = draft.parameters;
  generation_use_retry_draft_ = true;
  request_generation();
}

bool ShellController::generation_apply_available() const {
  if (!generation_ || !projects_ || !session_ || !selected_photo_) return false;
  LayerDocument document;
  const LoadResult loaded = LayerDocument::load(document, generation_files_, projects_->layer_document_path(*selected_photo_));
  return !loaded.absent && (!loaded.code || *loaded.code == ResultCode::Ok) && !loaded.recovered_empty &&
         document.source_image_id() == projects_->image_id(*selected_photo_) &&
         document.source_revision() == session_->document().revision() && !document.generations().empty();
}

void ShellController::request_generation_apply() {
  if (!generation_apply_available() || !projects_ || !selected_photo_ || !session_) return;
  std::string diagnostic;
  const ResultCode applied = session_->apply_layers(diagnostic);
  if (applied != ResultCode::Ok) {
    generation_status_ = "apply-failed";
    if (!diagnostic.empty()) qWarning("generation apply: %s", diagnostic.c_str());
    show_notices({}, NoticeScene::Generation, applied);
    if (changed_) changed_();
    return;
  }
  // The layer document stays alongside the photo, so renderd continues to
  // composite the verified generated pixels after the edit document records
  // the bake.  Enter Refine only after the command and redraw have succeeded.
  generation_status_ = "applied";
  display_.navigate(Page::Refine);
  if (changed_) changed_();
}

void ShellController::request_generation() {
  if (assistant_generation_busy_) {
    generation_status_ = "busy";
    if (changed_) changed_();
    return;
  }
  const bool use_retry_draft = generation_use_retry_draft_;
  generation_use_retry_draft_ = false;
  reap_assistant_generation_worker();
  if (!generation_ || generation_tool_id_.empty() || !projects_ || !current_project_ || !selected_photo_ || !session_) {
    generation_status_ = "no-photo";
    if (changed_) changed_();
    return;
  }

  const std::int64_t photo_id = *selected_photo_;
  const std::string layer_path = projects_->layer_document_path(photo_id);
  LayerDocument document;
  const LoadResult loaded = LayerDocument::load(document, generation_files_, layer_path);
  if (!loaded.absent && (!loaded.code || *loaded.code != ResultCode::Ok || loaded.recovered_empty)) {
    generation_status_ = "layer-document-unreadable";
    show_notices({}, NoticeScene::Generation, loaded.code.value_or(ResultCode::DocumentCorrupt));
    if (changed_) changed_();
    return;
  }
  if (loaded.absent) {
    auto created = LayerDocument::create(projects_->image_id(photo_id), session_->document().revision());
    if (!created) {
      generation_status_ = "layer-document-create-failed";
      show_notices({}, NoticeScene::Generation, ResultCode::StateConflict);
      if (changed_) changed_();
      return;
    }
    document = std::move(*created);
  }

  static std::atomic<std::uint64_t> sequence{0};
  const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
  std::vector<std::pair<std::string, std::string>> parameter_values = generation_parameter_values_;
  std::vector<std::uint8_t> parameter_payload;
  if (parameter_values.empty()) {
    (void)generation_catalog_.default_parameters(generation_tool_id_, parameter_values);
  }
  const ResultCode parameter_code = generation_catalog_.encode_parameters(
      generation_tool_id_, parameter_values, parameter_payload);
  if (parameter_code != ResultCode::Ok) {
    generation_status_ = "failed";
    show_notices({}, NoticeScene::Generation, parameter_code);
    if (changed_) changed_();
    return;
  }
  std::vector<std::string> mask_ids = session_->document().mask_ids();
  std::vector<std::string> reference_image_ids = session_->document().manual_refs();
  if (use_retry_draft && generation_retry_draft_ && generation_retry_draft_->tool_id == generation_tool_id_ &&
      generation_retry_draft_->image_id == projects_->image_id(photo_id) &&
      generation_retry_draft_->source_revision == session_->document().revision()) {
    mask_ids = generation_retry_draft_->mask_ids;
    reference_image_ids = generation_retry_draft_->reference_image_ids;
  }
  generate::GenerationTask task;
  const ResultCode task_code = generation_catalog_.build_task(
      generation_tool_id_,
      "creative-" + std::to_string(static_cast<long long>(now)) + "-" + std::to_string(++sequence),
      projects_->image_id(photo_id), session_->document().revision(), std::move(parameter_payload),
      std::move(mask_ids), std::move(reference_image_ids), {}, task);
  if (task_code != ResultCode::Ok) {
    generation_status_ = "failed";
    show_notices({}, NoticeScene::Generation, task_code);
    if (changed_) changed_();
    return;
  }

  generation_retry_draft_ = GenerationDraft{task.tool.tool_id,
                                            task.image_id,
                                            task.source_revision,
                                            parameter_values,
                                            task.mask_ids,
                                            task.reference_image_ids};

  TaskQuote quote;
  quote.task_id = task.task_id;
  quote.tool_id = task.tool.tool_id;
  quote.origin = kLedgerOriginPackage;
  quote.quoted_points = 0;  // The fixture is local and deliberately free.

  // Creative-page generation uses the same worker boundary as assistant
  // generation. The shell stays responsive while the provider process is
  // queueing, waiting, verifying and writing its result files.
  const std::string task_id = task.task_id;
  const std::string task_image_id = task.image_id;
  const std::uint64_t source_revision = task.source_revision;
  const std::string editing_instance_id = session_->document().editing_instance_id();
  assistant_generation_busy_ = true;
  assistant_generation_task_id_ = task_id;
  assistant_generation_entry_ = {task_id, task_image_id, task.tool.tool_id, "running", source_revision, 0};
  assistant_generation_history_ = generation_->history();
  assistant_generation_cancel_requested_.store(false, std::memory_order_release);
  if (!assistant_generation_lifetime_) assistant_generation_lifetime_ = std::make_shared<int>(0);
  const std::weak_ptr<int> lifetime = assistant_generation_lifetime_;
  generation_status_ = "queued";
  if (changed_) changed_();

  assistant_generation_worker_ = std::thread(
      [this, lifetime, task = std::move(task), quote, layer_path, photo_id, task_id, task_image_id, source_revision,
       editing_instance_id]() mutable {
        std::string diagnostic;
        ipc::GenerationProgressMessage progress;
        generation_session::VerifiedGenerationResult verified;
        const ResultCode code = generation_->generate(task, quote, progress, 30'000, diagnostic,
                                                     &assistant_generation_cancel_requested_, &verified);
        auto pending = std::make_shared<PendingGenerationResult>();
        pending->code = code;
        pending->diagnostic = std::move(diagnostic);
        pending->progress = std::move(progress);
        pending->verified = std::move(verified);
        assistant_generation_pending_ = pending;
        const auto reject_unplaced = [&] {
          if (pending->code != ResultCode::Ok || pending->progress.results.empty() || pending->handled) return;
          LayerDocument rejected;
          std::string reject_diagnostic;
          (void)generation_->place_result(pending->verified, rejected, {}, reject_diagnostic);
          pending->handled = true;
        };
        auto* application = QCoreApplication::instance();
        if (application == nullptr || lifetime.expired()) {
          reject_unplaced();
          return;
        }
        QMetaObject::invokeMethod(
            application,
            [this, lifetime, pending, task_id, task_image_id, source_revision, editing_instance_id, layer_path,
             photo_id]() mutable {
              if (lifetime.expired()) return;
              if (assistant_generation_worker_.joinable()) assistant_generation_worker_.join();
              const ResultCode code = pending->code;
              std::string& diagnostic = pending->diagnostic;
              auto& progress = pending->progress;
              auto& verified = pending->verified;
              assistant_generation_busy_ = false;
              assistant_generation_task_id_.clear();
              const auto reject = [&] {
                if (code != ResultCode::Ok || progress.results.empty()) return;
                LayerDocument rejected;
                std::string reject_diagnostic;
                (void)generation_->place_result(verified, rejected, {}, reject_diagnostic);
              };
              if (code == ResultCode::Cancelled) {
                generation_status_ = "cancelled";
              } else if (code != ResultCode::Ok) {
                generation_status_ = "failed";
                if (!diagnostic.empty()) qWarning("generation: %s", diagnostic.c_str());
                show_notices({}, NoticeScene::Generation, code);
              } else if (assistant_generation_cancel_requested_.load(std::memory_order_acquire)) {
                reject();
                generation_status_ = "succeeded-not-applied";
              } else if (!session_ || session_->document().image_id() != task_image_id ||
                         session_->document().editing_instance_id() != editing_instance_id ||
                         session_->document().revision() != source_revision) {
                reject();
                generation_status_ = "succeeded-source-changed";
              } else {
                LayerDocument document;
                const LoadResult loaded = LayerDocument::load(document, generation_files_, layer_path);
                ResultCode placed = ResultCode::Ok;
                bool placement_attempted = false;
                if (loaded.absent) {
                  auto created = LayerDocument::create(task_image_id, source_revision);
                  if (created) document = std::move(*created);
                  else placed = ResultCode::StateConflict;
                } else if (loaded.recovered_empty || !loaded.code || *loaded.code != ResultCode::Ok) {
                  placed = loaded.code.value_or(ResultCode::DocumentCorrupt);
                  if (placed == ResultCode::Ok) placed = ResultCode::DocumentCorrupt;
                }
                std::string placement_diagnostic = diagnostic;
                if (placed == ResultCode::Ok) {
                  placement_attempted = true;
                  placed = generation_->place_result(verified, document, layer_path, placement_diagnostic);
                }
                if (placed != ResultCode::Ok) {
                  // If loading the sidecar failed before place_result could
                  // confirm the result, reject it explicitly to reclaim the
                  // provider's temporary pixel files.
                  if (!placement_attempted && !progress.results.empty()) {
                    LayerDocument rejected;
                    std::string reject_diagnostic;
                    (void)generation_->place_result(verified, rejected, {}, reject_diagnostic);
                  }
                  generation_status_ = "succeeded-placement-failed";
                  show_notices({}, NoticeScene::Generation, placed);
                } else {
                  std::string refresh_diagnostic;
                  const ResultCode refreshed = session_->refresh_layer_document(layer_path, refresh_diagnostic);
                  if (refreshed != ResultCode::Ok) {
                    generation_status_ = "succeeded-refresh-failed";
                    if (!refresh_diagnostic.empty()) qWarning("generation preview refresh: %s",
                                                               refresh_diagnostic.c_str());
                    show_notices({}, NoticeScene::Generation, refreshed);
                  } else {
                    generation_status_ = "succeeded";
                    generation_retry_draft_.reset();
                    request_thumbnail(photo_id);
                  }
                }
                if (!placement_diagnostic.empty()) qWarning("generation placement: %s",
                                                            placement_diagnostic.c_str());
              }
              assistant_generation_entry_.status = generation_status_;
              assistant_generation_entry_.result_count = static_cast<std::uint32_t>(progress.results.size());
              pending->handled = true;
              if (assistant_generation_pending_ == pending) assistant_generation_pending_.reset();
              if (changed_) changed_();
            },
            Qt::QueuedConnection);
      });
}

std::optional<ExportTarget> ShellController::export_target() const {
  if (!exports_ || !current_project_ || !selected_photo_) return std::nullopt;
  ExportTarget target;
  // The library exports its selection; refine the photo it shows.
  target.photo_ids = display_.page() == Page::Library ? selected_photos() : std::vector<std::int64_t>{*selected_photo_};
  for (const auto& entry : projects()) {
    if (entry.id == *current_project_) target.project_name = entry.name;
  }
  std::string path;
  std::string diagnostic;
  if (projects_->resolve_photo(*selected_photo_, path, diagnostic) == ResultCode::Ok) {
    const auto slash = path.rfind('/');
    target.original_folder = slash == std::string::npos || slash == 0 ? "/" : path.substr(0, slash);
    QImageReader reader(QString::fromStdString(path));
    reader.setAutoTransform(true);
    QSize size = reader.size();
    if (size.isValid() && reader.transformation().testFlag(QImageIOHandler::TransformationRotate90)) size.transpose();
    if (size.isValid()) {
      target.product_width = size.width();
      target.product_height = size.height();
    }
  }
  target.defaults = exports_->defaults();
  target.estimate = exports_->estimate(target.photo_ids);
  target.remaining_sheets = exports_->remaining_sheets();
  return target;
}

std::vector<std::string> ShellController::request_export(const ExportRequest& request,
                                                         const ExportDefaults& defaults, bool& ok) {
  ok = false;
  std::vector<std::string> notices;
  if (!exports_) return notices;
  std::string diagnostic;
  if (exports_->remember_defaults(defaults) != ResultCode::Ok) qWarning("export defaults not saved");
  const ResultCode code = exports_->submit(request, notices, diagnostic);
  if (code != ResultCode::Ok && !diagnostic.empty()) qWarning("export: %s", diagnostic.c_str());
  ok = code == ResultCode::Ok;
  watch_exportd();
  exports_changed();
  return notices;
}

std::optional<std::int64_t> ShellController::remaining_sheets() const {
  if (!exports_) return std::nullopt;
  return exports_->remaining_sheets();
}

std::vector<ExportRow> ShellController::export_rows() const {
  return exports_ ? exports_->rows() : std::vector<ExportRow>{};
}

bool ShellController::store_watermark(const std::string& path, std::string& material, std::string& reason) {
  if (!exports_) {
    reason = qtext("export.queue.unavailable").toStdString();
    return false;
  }
  return exports_->store_watermark(path, material, reason) == ResultCode::Ok;
}

std::string ShellController::watermark_file(const std::string& material) const {
  return exports_ ? exports_->watermark_path(material) : std::string();
}

void ShellController::remember_export_defaults(const ExportDefaults& defaults) {
  if (exports_ && exports_->remember_defaults(defaults) != ResultCode::Ok) qWarning("export defaults not saved");
}

void ShellController::request_export_retry(const std::string& export_id) {
  if (!exports_) return;
  std::string diagnostic;
  if (exports_->retry(export_id, diagnostic) != ResultCode::Ok) qWarning("export retry: %s", diagnostic.c_str());
  watch_exportd();
  exports_changed();
}

void ShellController::request_export_cancel(const std::string& export_id) {
  if (!exports_) return;
  std::string diagnostic;
  if (exports_->cancel(export_id, diagnostic) != ResultCode::Ok) qWarning("export cancel: %s", diagnostic.c_str());
  exports_changed();
}

void ShellController::watch_exports(QObject* owner, std::function<void()> changed) {
  export_watchers_.emplace_back(owner, std::move(changed));
}

void ShellController::watch_exportd() {
  const int fd = exports_ ? exports_->fd() : -1;
  if (export_notifier_ && static_cast<int>(export_notifier_->socket()) == fd) return;
  if (export_notifier_) {
    // Possibly called from the notifier's own signal: let Qt delete it later.
    export_notifier_->setEnabled(false);
    export_notifier_.release()->deleteLater();
  }
  if (fd < 0) return;
  export_notifier_ = std::make_unique<QSocketNotifier>(fd, QSocketNotifier::Read);
  QObject::connect(export_notifier_.get(), &QSocketNotifier::activated, export_notifier_.get(),
                   [this] { read_exports(); });
}

void ShellController::read_exports() {
  if (!exports_) return;
  const bool changed = exports_->read_progress();
  watch_exportd();  // exportd may have gone
  if (changed) exports_changed();
}

void ShellController::exports_changed() {
  for (auto it = export_watchers_.begin(); it != export_watchers_.end();) {
    if (it->first.isNull()) {
      it = export_watchers_.erase(it);
      continue;
    }
    it->second();
    ++it;
  }
}

}  // namespace pixcake::shell
