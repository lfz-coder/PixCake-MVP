#include "assistant_gateway_server.hpp"

#include "pixcake/assistant/assistant_channel.hpp"
#include "pixcake/support/result_code.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace pixcake::shell {
namespace {

using assistant::AssistantCard;
using assistant::AssistantCardField;
using assistant::AssistantGenerationCancelRequest;
using assistant::AssistantGenerationRequest;
using assistant::AssistantRequest;
using assistant::AssistantSnapshotRequest;
using assistant::EditPlan;
using assistant::GatewayReply;
using assistant::Operation;
using assistant::OperationParameter;

constexpr std::size_t kFrameHeaderBytes = 4;

QString wire_code(ResultCode code) {
  switch (code) {
    case ResultCode::Ok:
      return QStringLiteral("ok");
    case ResultCode::UnsupportedFormat:
      return QStringLiteral("unsupported_format");
    case ResultCode::ImageCorrupt:
      return QStringLiteral("image_corrupt");
    case ResultCode::PermissionDenied:
      return QStringLiteral("permission_denied");
    case ResultCode::DiskFull:
      return QStringLiteral("disk_full");
    case ResultCode::SourceMissing:
      return QStringLiteral("source_missing");
    case ResultCode::DocumentCorrupt:
      return QStringLiteral("document_corrupt");
    case ResultCode::CapabilityTooNew:
      return QStringLiteral("capability_too_new");
    case ResultCode::Cancelled:
      return QStringLiteral("cancelled");
    case ResultCode::NetworkFailed:
      return QStringLiteral("network_failed");
    case ResultCode::InsufficientBalance:
      return QStringLiteral("insufficient_balance");
    case ResultCode::FastExportBusy:
      return QStringLiteral("fast_export_busy");
    case ResultCode::StateConflict:
      return QStringLiteral("state_conflict");
    case ResultCode::IoFailed:
      return QStringLiteral("io_failed");
  }
  return QStringLiteral("unknown");
}

GatewayReply failure(ResultCode code, const QString& diagnostic) {
  GatewayReply reply;
  reply.code = code;
  reply.diagnostic = diagnostic.toStdString();
  return reply;
}

bool string_value(const QJsonObject& object, const char* key, std::string& out, bool required, QString& diagnostic) {
  const QJsonValue value = object.value(QLatin1String(key));
  if (value.isUndefined() || value.isNull()) {
    if (!required) {
      out.clear();
      return true;
    }
    diagnostic = QStringLiteral("missing assistant field: ") + QLatin1String(key);
    return false;
  }
  if (!value.isString()) {
    diagnostic = QStringLiteral("assistant field must be a string: ") + QLatin1String(key);
    return false;
  }
  out = value.toString().toStdString();
  return true;
}

bool uint_value(const QJsonObject& object, const char* key, std::uint64_t& out, bool required, QString& diagnostic) {
  const QJsonValue value = object.value(QLatin1String(key));
  if (value.isUndefined() || value.isNull()) {
    if (!required) {
      out = 0;
      return true;
    }
    diagnostic = QStringLiteral("missing assistant integer: ") + QLatin1String(key);
    return false;
  }
  if (!value.isDouble()) {
    diagnostic = QStringLiteral("assistant field must be a non-negative integer: ") + QLatin1String(key);
    return false;
  }
  const double number = value.toDouble();
  if (!std::isfinite(number) || number < 0 || std::floor(number) != number ||
      number > static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
    diagnostic = QStringLiteral("assistant field must be a non-negative integer: ") + QLatin1String(key);
    return false;
  }
  out = static_cast<std::uint64_t>(number);
  return true;
}

bool bool_value(const QJsonObject& object, const char* key, bool& out, bool default_value, QString& diagnostic) {
  const QJsonValue value = object.value(QLatin1String(key));
  if (value.isUndefined() || value.isNull()) {
    out = default_value;
    return true;
  }
  if (!value.isBool()) {
    diagnostic = QStringLiteral("assistant field must be boolean: ") + QLatin1String(key);
    return false;
  }
  out = value.toBool();
  return true;
}

bool decode_request(const QJsonObject& object, AssistantRequest& out, QString& diagnostic) {
  out = {};
  std::uint64_t revision = 0;
  std::uint64_t protocol = 0;
  if (!uint_value(object, "protocol_version", protocol, false, diagnostic)) return false;
  out.protocol_version = protocol == 0 ? assistant::kAssistantProtocolVersion : static_cast<std::uint32_t>(protocol);
  if (!string_value(object, "request_id", out.request_id, true, diagnostic) ||
      !string_value(object, "thread_id", out.thread_id, true, diagnostic) ||
      !string_value(object, "message", out.message, true, diagnostic) ||
      !string_value(object, "mode", out.mode, false, diagnostic) ||
      !uint_value(object, "base_revision", revision, false, diagnostic)) {
    return false;
  }
  out.base_revision = revision;
  if (out.mode.empty()) out.mode = "assistant";

  const QJsonObject image = object.value(QStringLiteral("image")).toObject();
  if (!image.isEmpty()) {
    if (!string_value(image, "image_id", out.image.image_id, true, diagnostic) ||
        !string_value(image, "project_id", out.image.project_id, false, diagnostic) ||
        !string_value(image, "editing_instance_id", out.image.editing_instance_id, false, diagnostic) ||
        !string_value(image, "preview_ref", out.image.preview_ref, false, diagnostic) ||
        !uint_value(image, "source_revision", out.image.source_revision, false, diagnostic)) {
      return false;
    }
  }
  const QJsonObject selection = object.value(QStringLiteral("selection")).toObject();
  if (!selection.isEmpty()) {
    if (!string_value(selection, "selection_id", out.selection.selection_id, true, diagnostic) ||
        !string_value(selection, "kind", out.selection.kind, false, diagnostic) ||
        !string_value(selection, "label", out.selection.label, false, diagnostic)) {
      return false;
    }
  }
  return true;
}

std::string json_scalar(const QJsonValue& value) {
  if (value.isString()) return value.toString().toStdString();
  if (value.isBool()) return value.toBool() ? "true" : "false";
  if (value.isDouble()) return QByteArray::number(value.toDouble(), 'g', 17).toStdString();
  return QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact).toStdString();
}

bool decode_plan(const QJsonObject& object, EditPlan& out, QString& diagnostic) {
  out = {};
  std::uint64_t protocol = 0;
  std::uint64_t revision = 0;
  std::uint64_t points = 0;
  if (!uint_value(object, "protocol_version", protocol, false, diagnostic) ||
      !uint_value(object, "base_revision", revision, false, diagnostic) ||
      !uint_value(object, "estimated_points", points, false, diagnostic) ||
      !string_value(object, "plan_id", out.plan_id, true, diagnostic) ||
      !string_value(object, "request_id", out.request_id, true, diagnostic) ||
      !string_value(object, "summary", out.summary, true, diagnostic)) {
    return false;
  }
  out.protocol_version = protocol == 0 ? assistant::kAssistantProtocolVersion : static_cast<std::uint32_t>(protocol);
  out.base_revision = revision;
  out.estimated_points = points;
  if (!bool_value(object, "requires_confirmation", out.requires_confirmation, true, diagnostic)) return false;
  const QJsonArray operations = object.value(QStringLiteral("operations")).toArray();
  for (const QJsonValue& raw : operations) {
    const QJsonObject value = raw.toObject();
    if (value.isEmpty()) {
      diagnostic = QStringLiteral("assistant operation must be an object");
      return false;
    }
    Operation operation;
    if (!string_value(value, "operation_id", operation.operation_id, true, diagnostic) ||
        !string_value(value, "tool_id", operation.tool_id, true, diagnostic) ||
        !string_value(value, "target_selection_id", operation.target_selection_id, false, diagnostic) ||
        !bool_value(value, "requires_confirmation", operation.requires_confirmation, false, diagnostic) ||
        !bool_value(value, "reversible", operation.reversible, true, diagnostic)) {
      return false;
    }
    const QJsonObject parameters = value.value(QStringLiteral("parameters")).toObject();
    for (auto it = parameters.begin(); it != parameters.end(); ++it) {
      operation.parameters.push_back(OperationParameter{it.key().toStdString(), json_scalar(it.value())});
    }
    out.operations.push_back(std::move(operation));
  }
  return true;
}

bool decode_card(const QJsonObject& object, AssistantCard& out, QString& diagnostic) {
  out = {};
  if (!string_value(object, "card_id", out.card_id, false, diagnostic) ||
      !string_value(object, "card_type", out.card_type, true, diagnostic) ||
      !bool_value(object, "confirmation_required", out.confirmation_required, true, diagnostic)) {
    return false;
  }
  const QJsonArray fields = object.value(QStringLiteral("fields")).toArray();
  for (const QJsonValue& raw : fields) {
    const QJsonObject value = raw.toObject();
    AssistantCardField field;
    if (!string_value(value, "field_id", field.field_id, true, diagnostic) ||
        !string_value(value, "type", field.type, true, diagnostic) ||
        !string_value(value, "label", field.label, true, diagnostic) ||
        !string_value(value, "value", field.value, false, diagnostic) ||
        !bool_value(value, "required", field.required, false, diagnostic)) {
      return false;
    }
    out.fields.push_back(std::move(field));
  }
  if (out.card_id.empty()) out.card_id = "card";
  return true;
}

bool string_array_value(const QJsonObject& object, const char* key, std::vector<std::string>& out,
                        QString& diagnostic) {
  const QJsonValue raw = object.value(QLatin1String(key));
  if (raw.isUndefined() || raw.isNull()) {
    out.clear();
    return true;
  }
  if (!raw.isArray()) {
    diagnostic = QStringLiteral("assistant field must be an array: ") + QLatin1String(key);
    return false;
  }
  out.clear();
  for (const auto& item : raw.toArray()) {
    if (!item.isString()) {
      diagnostic = QStringLiteral("assistant array items must be strings: ") + QLatin1String(key);
      return false;
    }
    out.push_back(item.toString().toStdString());
  }
  return true;
}

bool decode_generation_request(const QJsonObject& object, const AssistantRequest& request,
                               AssistantGenerationRequest& out, QString& diagnostic) {
  out = {};
  if (object.contains(QStringLiteral("task")) && !object.value(QStringLiteral("task")).isObject()) {
    diagnostic = QStringLiteral("assistant generation task must be an object");
    return false;
  }
  const QJsonObject task = object.value(QStringLiteral("task")).toObject().isEmpty()
                               ? object
                               : object.value(QStringLiteral("task")).toObject();
  std::uint64_t source_revision = request.base_revision;
  if (!string_value(task, "task_id", out.task_id, true, diagnostic) ||
      !string_value(task, "tool_id", out.tool_id, true, diagnostic) ||
      !string_value(task, "image_id", out.image_id, false, diagnostic) ||
      !string_value(task, "idempotency_key", out.idempotency_key, false, diagnostic) ||
      !uint_value(task, "source_revision", source_revision, false, diagnostic) ||
      !string_array_value(task, "mask_ids", out.mask_ids, diagnostic) ||
      !string_array_value(task, "reference_image_ids", out.reference_image_ids, diagnostic)) {
    return false;
  }
  if (out.image_id.empty()) out.image_id = request.image.image_id;
  out.source_revision = task.contains(QStringLiteral("source_revision")) ? source_revision : request.base_revision;
  if (task.contains(QStringLiteral("parameters")) && !task.value(QStringLiteral("parameters")).isObject()) {
    diagnostic = QStringLiteral("assistant generation parameters must be an object");
    return false;
  }
  const QJsonObject parameters = task.value(QStringLiteral("parameters")).toObject();
  for (auto it = parameters.begin(); it != parameters.end(); ++it) {
    if (!it.value().isString() && !it.value().isBool() && !it.value().isDouble()) {
      diagnostic = QStringLiteral("assistant generation parameters must be scalar values");
      return false;
    }
    out.parameters.push_back({it.key().toStdString(), json_scalar(it.value())});
  }
  return true;
}

bool decode_generation_cancel_request(const QJsonObject& object, AssistantGenerationCancelRequest& out,
                                      QString& diagnostic) {
  out = {};
  const QJsonObject task = object.value(QStringLiteral("task")).toObject().isEmpty()
                               ? object
                               : object.value(QStringLiteral("task")).toObject();
  return string_value(task, "task_id", out.task_id, true, diagnostic);
}

QJsonObject card_json(const AssistantCard& card) {
  QJsonObject value;
  value.insert(QStringLiteral("card_id"), QString::fromStdString(card.card_id));
  value.insert(QStringLiteral("card_type"), QString::fromStdString(card.card_type));
  value.insert(QStringLiteral("confirmation_required"), card.confirmation_required);
  QJsonArray fields;
  for (const auto& field : card.fields) {
    QJsonObject item;
    item.insert(QStringLiteral("field_id"), QString::fromStdString(field.field_id));
    item.insert(QStringLiteral("type"), QString::fromStdString(field.type));
    item.insert(QStringLiteral("label"), QString::fromStdString(field.label));
    item.insert(QStringLiteral("value"), QString::fromStdString(field.value));
    item.insert(QStringLiteral("required"), field.required);
    fields.push_back(item);
  }
  value.insert(QStringLiteral("fields"), fields);
  return value;
}

QJsonObject capability_descriptor_json(const assistant::AssistantCapabilityDescriptor& descriptor) {
  QJsonObject value;
  value.insert(QStringLiteral("capability_id"), QString::fromStdString(descriptor.capability_id));
  value.insert(QStringLiteral("module_label"), QString::fromStdString(descriptor.module_label));
  value.insert(QStringLiteral("label"), QString::fromStdString(descriptor.label));
  value.insert(QStringLiteral("can_use_in_preset"), descriptor.can_use_in_preset);
  value.insert(QStringLiteral("can_batch_sync"), descriptor.can_batch_sync);
  value.insert(QStringLiteral("batch_sync_blocked_reason"),
               QString::fromStdString(descriptor.batch_sync_blocked_reason));
  return value;
}

QJsonObject parameter_disable_rule_json(const assistant::AssistantParameterDisableRule& rule) {
  QJsonObject value;
  value.insert(QStringLiteral("capability_id"), QString::fromStdString(rule.capability_id));
  value.insert(QStringLiteral("parameter_id"), QString::fromStdString(rule.parameter_id));
  value.insert(QStringLiteral("when_choice"), QString::fromStdString(rule.when_choice));
  value.insert(QStringLiteral("reason"), QString::fromStdString(rule.reason));
  value.insert(QStringLiteral("when_zero"), rule.when_zero);
  return value;
}

QJsonObject parameter_descriptor_json(const assistant::AssistantParameterDescriptor& descriptor) {
  QJsonObject value;
  value.insert(QStringLiteral("capability_id"), QString::fromStdString(descriptor.capability_id));
  value.insert(QStringLiteral("parameter_id"), QString::fromStdString(descriptor.parameter_id));
  value.insert(QStringLiteral("minimum"), descriptor.minimum);
  value.insert(QStringLiteral("maximum"), descriptor.maximum);
  value.insert(QStringLiteral("step"), descriptor.step);
  value.insert(QStringLiteral("default_value"), descriptor.default_value);
  value.insert(QStringLiteral("render_stage"), QString::fromStdString(descriptor.render_stage));
  value.insert(QStringLiteral("render_program"), QString::fromStdString(descriptor.render_program));
  value.insert(QStringLiteral("render_channel"), QString::fromStdString(descriptor.render_channel));
  value.insert(QStringLiteral("schema_version"), static_cast<qint64>(descriptor.schema_version));
  value.insert(QStringLiteral("order"), static_cast<qint64>(descriptor.order));
  value.insert(QStringLiteral("default_choice"), QString::fromStdString(descriptor.default_choice));
  value.insert(QStringLiteral("step_confirmed"), descriptor.step_confirmed);
  value.insert(QStringLiteral("default_confirmed"), descriptor.default_confirmed);
  value.insert(QStringLiteral("text"), descriptor.text);
  value.insert(QStringLiteral("max_length"), static_cast<qint64>(descriptor.max_length));
  QJsonArray choices;
  for (const auto& choice : descriptor.choices) choices.push_back(QString::fromStdString(choice));
  value.insert(QStringLiteral("choices"), choices);
  QJsonArray disable_rules;
  for (const auto& rule : descriptor.disable_rules) disable_rules.push_back(parameter_disable_rule_json(rule));
  value.insert(QStringLiteral("disable_rules"), disable_rules);
  return value;
}

QByteArray reply_json(const GatewayReply& reply) {
  QJsonObject payload;
  payload.insert(QStringLiteral("request_id"), QString::fromStdString(reply.request_id));
  payload.insert(QStringLiteral("command_id"), QString::fromStdString(reply.command_id));
  payload.insert(QStringLiteral("image_id"), QString::fromStdString(reply.image_id));
  payload.insert(QStringLiteral("editing_instance_id"), QString::fromStdString(reply.editing_instance_id));
  payload.insert(QStringLiteral("preview_id"), QString::fromStdString(reply.preview_id));
  payload.insert(QStringLiteral("snapshot_id"), QString::fromStdString(reply.snapshot_id));
  payload.insert(QStringLiteral("task_id"), QString::fromStdString(reply.task_id));
  payload.insert(QStringLiteral("task_status"), QString::fromStdString(reply.task_status));
  payload.insert(QStringLiteral("approval_id"), QString::fromStdString(reply.approval_id));
  payload.insert(QStringLiteral("approval_status"), QString::fromStdString(reply.approval_status));
  payload.insert(QStringLiteral("selection_id"), QString::fromStdString(reply.selection_id));
  payload.insert(QStringLiteral("selection_kind"), QString::fromStdString(reply.selection_kind));
  payload.insert(QStringLiteral("selection_label"), QString::fromStdString(reply.selection_label));
  payload.insert(QStringLiteral("revision"), static_cast<qint64>(reply.revision));
  QJsonArray capabilities;
  for (const auto& capability : reply.capabilities) capabilities.push_back(QString::fromStdString(capability));
  payload.insert(QStringLiteral("capabilities"), capabilities);
  QJsonArray capability_descriptors;
  for (const auto& descriptor : reply.capability_descriptors) {
    capability_descriptors.push_back(capability_descriptor_json(descriptor));
  }
  payload.insert(QStringLiteral("capability_descriptors"), capability_descriptors);
  QJsonArray parameter_descriptors;
  for (const auto& descriptor : reply.parameter_descriptors) {
    parameter_descriptors.push_back(parameter_descriptor_json(descriptor));
  }
  payload.insert(QStringLiteral("parameter_descriptors"), parameter_descriptors);
  QJsonArray task_history;
  for (const auto& entry : reply.task_history) {
    QJsonObject item;
    item.insert(QStringLiteral("task_id"), QString::fromStdString(entry.task_id));
    item.insert(QStringLiteral("image_id"), QString::fromStdString(entry.image_id));
    item.insert(QStringLiteral("tool_id"), QString::fromStdString(entry.tool_id));
    item.insert(QStringLiteral("status"), QString::fromStdString(entry.status));
    item.insert(QStringLiteral("source_revision"), static_cast<qint64>(entry.source_revision));
    item.insert(QStringLiteral("result_count"), static_cast<qint64>(entry.result_count));
    task_history.push_back(item);
  }
  payload.insert(QStringLiteral("task_history"), task_history);
  QJsonArray approvals;
  for (const auto& entry : reply.approval_history) {
    approvals.push_back(QJsonObject{{"approval_id", QString::fromStdString(entry.approval_id)},
                                   {"status", QString::fromStdString(entry.status)},
                                   {"command_id", QString::fromStdString(entry.command_id)},
                                   {"snapshot_id", QString::fromStdString(entry.snapshot_id)},
                                   {"task_id", QString::fromStdString(entry.task_id)},
                                   {"revision", static_cast<qint64>(entry.revision)}});
  }
  payload.insert(QStringLiteral("approval_history"), approvals);
  QJsonArray notices;
  for (const auto& notice : reply.notices) notices.push_back(QString::fromStdString(notice));
  payload.insert(QStringLiteral("notices"), notices);
  if (!reply.card.card_id.empty()) payload.insert(QStringLiteral("card"), card_json(reply.card));

  QJsonObject result;
  result.insert(QStringLiteral("ok"), reply.code == ResultCode::Ok);
  result.insert(QStringLiteral("code"), wire_code(reply.code));
  result.insert(QStringLiteral("diagnostic"), QString::fromStdString(reply.diagnostic));
  result.insert(QStringLiteral("payload"), payload);
  return QJsonDocument(result).toJson(QJsonDocument::Compact);
}

}  // namespace

AssistantGatewayServer::AssistantGatewayServer(assistant::AssistantGateway& gateway, QObject* owner)
    : gateway_(gateway), server_(nullptr) {
  (void)owner;
  QObject::connect(&server_, &QLocalServer::newConnection, &server_, [this] { accept_connections(); });
}

AssistantGatewayServer::~AssistantGatewayServer() { stop(); }

bool AssistantGatewayServer::start(const QString& socket_path, QString& diagnostic) {
  stop();
  if (socket_path.isEmpty()) {
    diagnostic = QStringLiteral("assistant socket path is empty");
    return false;
  }
  server_.setSocketOptions(QLocalServer::UserAccessOption);
  if (!server_.listen(socket_path)) {
    diagnostic = server_.errorString();
    return false;
  }
  socket_path_ = socket_path;
  diagnostic.clear();
  return true;
}

void AssistantGatewayServer::stop() {
  server_.close();
  const auto sockets = buffers_.keys();
  for (QLocalSocket* socket : sockets) {
    if (socket != nullptr) socket->disconnectFromServer();
  }
  buffers_.clear();
  if (!socket_path_.isEmpty()) QLocalServer::removeServer(socket_path_);
  socket_path_.clear();
}

void AssistantGatewayServer::accept_connections() {
  while (server_.hasPendingConnections()) {
    QLocalSocket* socket = server_.nextPendingConnection();
    if (socket == nullptr) continue;
    buffers_.insert(socket, {});
    QObject::connect(socket, &QLocalSocket::readyRead, socket, [this, socket] { read_socket(socket); });
    QObject::connect(socket, &QLocalSocket::disconnected, socket, [this, socket] { remove_socket(socket); });
  }
}

void AssistantGatewayServer::read_socket(QLocalSocket* socket) {
  if (socket == nullptr || !buffers_.contains(socket)) return;
  QByteArray& buffer = buffers_[socket];
  buffer += socket->readAll();
  if (buffer.size() > static_cast<qsizetype>(assistant::kMaxAssistantFrameBytes + kFrameHeaderBytes)) {
    close_socket(socket);
    return;
  }
  while (buffer.size() >= static_cast<qsizetype>(kFrameHeaderBytes)) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(buffer.constData());
    const std::size_t size = static_cast<std::size_t>(bytes[0]) |
                             (static_cast<std::size_t>(bytes[1]) << 8) |
                             (static_cast<std::size_t>(bytes[2]) << 16) |
                             (static_cast<std::size_t>(bytes[3]) << 24);
    if (size > assistant::kMaxAssistantFrameBytes) {
      close_socket(socket);
      return;
    }
    if (buffer.size() < static_cast<qsizetype>(kFrameHeaderBytes + size)) return;
    const QByteArray payload = buffer.mid(static_cast<qsizetype>(kFrameHeaderBytes), static_cast<qsizetype>(size));
    buffer.remove(0, static_cast<qsizetype>(kFrameHeaderBytes + size));
    handle_payload(socket, payload);
    return;  // one request per connection
  }
}

void AssistantGatewayServer::remove_socket(QLocalSocket* socket) {
  buffers_.remove(socket);
  if (socket != nullptr) socket->deleteLater();
}

void AssistantGatewayServer::handle_payload(QLocalSocket* socket, const QByteArray& payload) {
  QJsonParseError parse_error{};
  const QJsonDocument document = QJsonDocument::fromJson(payload, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    send_reply(socket, reply_json(failure(ResultCode::StateConflict, QStringLiteral("assistant JSON is malformed"))));
    return;
  }
  const QJsonObject body = document.object();
  std::string command;
  QString diagnostic;
  if (!string_value(body, "command", command, true, diagnostic)) {
    send_reply(socket, reply_json(failure(ResultCode::StateConflict, diagnostic)));
    return;
  }
  AssistantRequest request;
  if (!decode_request(body.value(QStringLiteral("request")).toObject(), request, diagnostic)) {
    send_reply(socket, reply_json(failure(ResultCode::StateConflict, diagnostic)));
    return;
  }
  const QJsonObject payload_object = body.value(QStringLiteral("payload")).toObject();
  GatewayReply reply;
  if (command == "get_edit_context") {
    reply = gateway_.read_edit_context(request);
  } else if (command == "get_selection_context") {
    reply = gateway_.read_selection_context(request);
  } else if (command == "preview_edit_plan" || command == "commit_edit_plan") {
    const QJsonObject plan_object = payload_object.value(QStringLiteral("plan")).toObject().isEmpty()
                                        ? payload_object
                                        : payload_object.value(QStringLiteral("plan")).toObject();
    EditPlan plan;
    if (!decode_plan(plan_object, plan, diagnostic)) {
      reply = failure(ResultCode::StateConflict, diagnostic);
    } else if (command == "preview_edit_plan") {
      reply = gateway_.preview(request, plan);
    } else {
      bool confirmed = false;
      if (!bool_value(payload_object, "user_confirmed", confirmed, false, diagnostic)) {
        reply = failure(ResultCode::StateConflict, diagnostic);
      } else {
        reply = gateway_.commit(request, plan, confirmed);
      }
    }
  } else if (command == "reject_edit_plan") {
    std::string plan_id;
    reply = string_value(payload_object, "plan_id", plan_id, true, diagnostic)
                ? gateway_.reject(request, plan_id)
                : failure(ResultCode::StateConflict, diagnostic);
  } else if (command == "undo_edit") {
    std::string command_id;
    bool confirmed = false;
    if (!string_value(payload_object, "command_id", command_id, true, diagnostic) ||
        !bool_value(payload_object, "user_confirmed", confirmed, false, diagnostic)) {
      reply = failure(ResultCode::StateConflict, diagnostic);
    } else {
      reply = gateway_.undo(request, command_id, confirmed);
    }
  } else if (command == "request_card") {
    AssistantCard card;
    if (!decode_card(payload_object, card, diagnostic)) {
      reply = failure(ResultCode::StateConflict, diagnostic);
    } else {
      reply = gateway_.request_card(request, card);
    }
  } else if (command == "run_generation") {
    AssistantGenerationRequest task;
    bool confirmed = false;
    if (!decode_generation_request(payload_object, request, task, diagnostic) ||
        !bool_value(payload_object, "user_confirmed", confirmed, false, diagnostic)) {
      reply = failure(ResultCode::StateConflict, diagnostic);
    } else {
      reply = gateway_.run_generation(request, task, confirmed);
    }
  } else if (command == "cancel_generation") {
    AssistantGenerationCancelRequest task;
    if (!decode_generation_cancel_request(payload_object, task, diagnostic)) {
      reply = failure(ResultCode::StateConflict, diagnostic);
    } else {
      reply = gateway_.cancel_generation(request, task);
    }
  } else if (command == "create_snapshot") {
    std::string label;
    reply = string_value(payload_object, "label", label, false, diagnostic)
                ? gateway_.create_snapshot(request, label)
                : failure(ResultCode::StateConflict, diagnostic);
  } else if (command == "apply_snapshot") {
    AssistantSnapshotRequest snapshot;
    bool confirmed = false;
    if (!string_value(payload_object, "snapshot_id", snapshot.snapshot_id, true, diagnostic) ||
        !string_value(payload_object, "image_id", snapshot.image_id, false, diagnostic) ||
        !bool_value(payload_object, "user_confirmed", confirmed, false, diagnostic)) {
      reply = failure(ResultCode::StateConflict, diagnostic);
    } else {
      reply = gateway_.apply_snapshot(request, snapshot, confirmed);
    }
  } else if (command == "get_approval_status") {
    std::string approval_id;
    if (!string_value(payload_object, "approval_id", approval_id, true, diagnostic)) {
      reply = failure(ResultCode::StateConflict, diagnostic);
    } else {
      reply = gateway_.approval_status(request, approval_id);
    }
  } else if (command == "list_task_history") {
    reply = gateway_.list_task_history(request);
  } else {
    reply = failure(ResultCode::StateConflict, QStringLiteral("assistant command is not implemented: ") +
                                                    QString::fromStdString(command));
  }
  send_reply(socket, reply_json(reply));
}

void AssistantGatewayServer::send_reply(QLocalSocket* socket, const QByteArray& payload) {
  if (socket == nullptr || !buffers_.contains(socket) || payload.size() > static_cast<qsizetype>(assistant::kMaxAssistantFrameBytes)) {
    close_socket(socket);
    return;
  }
  QByteArray frame;
  frame.resize(static_cast<qsizetype>(kFrameHeaderBytes));
  const std::uint32_t size = static_cast<std::uint32_t>(payload.size());
  frame[0] = static_cast<char>(size & 0xff);
  frame[1] = static_cast<char>((size >> 8) & 0xff);
  frame[2] = static_cast<char>((size >> 16) & 0xff);
  frame[3] = static_cast<char>((size >> 24) & 0xff);
  frame += payload;
  socket->write(frame);
  QObject::connect(socket, &QLocalSocket::bytesWritten, socket, [socket](qint64) {
    if (socket->bytesToWrite() == 0) socket->disconnectFromServer();
  });
  if (socket->bytesToWrite() == 0) socket->disconnectFromServer();
}

void AssistantGatewayServer::close_socket(QLocalSocket* socket) {
  if (socket == nullptr) return;
  socket->abort();
  remove_socket(socket);
}

}  // namespace pixcake::shell
