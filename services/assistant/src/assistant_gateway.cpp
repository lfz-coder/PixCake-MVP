#include "pixcake/assistant/assistant_gateway.hpp"

#include <algorithm>
#include <cctype>

namespace pixcake::assistant {
namespace {

constexpr std::size_t kMaxPendingAssistantPreviews = 128;
constexpr std::size_t kMaxAssistantGenerationReferences = 64;

bool valid_text(const std::string& value, std::size_t max, bool required) {
  return (!required || !value.empty()) && value.size() <= max && value.find('\0') == std::string::npos;
}

bool valid_id(const std::string& value, bool required = true) {
  if (!valid_text(value, kMaxAssistantId, required)) return false;
  if (value.empty()) return !required;
  if (!std::isalnum(static_cast<unsigned char>(value.front()))) return false;
  return std::all_of(value.begin() + 1, value.end(), [](unsigned char c) {
    return std::isalnum(c) || c == '.' || c == '_' || c == ':' || c == '-';
  });
}

bool same_plan(const EditPlan& left, const EditPlan& right) {
  if (left.protocol_version != right.protocol_version || left.plan_id != right.plan_id ||
      left.request_id != right.request_id || left.summary != right.summary ||
      left.base_revision != right.base_revision || left.estimated_points != right.estimated_points ||
      left.requires_confirmation != right.requires_confirmation || left.operations.size() != right.operations.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.operations.size(); ++index) {
    const auto& a = left.operations[index];
    const auto& b = right.operations[index];
    if (a.operation_id != b.operation_id || a.tool_id != b.tool_id ||
        a.target_selection_id != b.target_selection_id || a.requires_confirmation != b.requires_confirmation ||
        a.reversible != b.reversible || a.parameters.size() != b.parameters.size()) {
      return false;
    }
    for (std::size_t parameter = 0; parameter < a.parameters.size(); ++parameter) {
      if (a.parameters[parameter].name != b.parameters[parameter].name ||
          a.parameters[parameter].value != b.parameters[parameter].value) {
        return false;
      }
    }
  }
  return true;
}

bool same_generation(const AssistantGenerationRequest& a, const AssistantGenerationRequest& b) {
  if (a.tool_id != b.tool_id || a.image_id != b.image_id || a.source_revision != b.source_revision ||
      a.idempotency_key != b.idempotency_key || a.mask_ids != b.mask_ids ||
      a.reference_image_ids != b.reference_image_ids || a.parameters.size() != b.parameters.size()) return false;
  for (std::size_t index = 0; index < a.parameters.size(); ++index) {
    if (a.parameters[index].name != b.parameters[index].name || a.parameters[index].value != b.parameters[index].value) {
      return false;
    }
  }
  return true;
}

bool same_context(const AssistantRequest& a, const AssistantRequest& b) {
  return a.thread_id == b.thread_id && a.image.image_id == b.image.image_id &&
         a.image.project_id == b.image.project_id && a.image.editing_instance_id == b.image.editing_instance_id;
}

bool same_approval(const AssistantApproval& a, const AssistantApproval& b) {
  if (a.kind != b.kind || !same_context(a.request, b.request) ||
      a.request.base_revision != b.request.base_revision || a.request.mode != b.request.mode ||
      a.request.selection.selection_id != b.request.selection.selection_id) return false;
  switch (a.kind) {
    case AssistantApprovalKind::Commit: return same_plan(a.plan, b.plan);
    case AssistantApprovalKind::Undo: return a.command_id == b.command_id;
    case AssistantApprovalKind::ApplySnapshot:
      return a.snapshot.snapshot_id == b.snapshot.snapshot_id && a.snapshot.image_id == b.snapshot.image_id;
    case AssistantApprovalKind::Generation: return same_generation(a.generation, b.generation);
  }
  return false;
}

}  // namespace

AssistantGateway::AssistantGateway(GatewayHandlers handlers) : handlers_(std::move(handlers)) {}

GatewayReply AssistantGateway::notify_approval(GatewayReply reply) {
  if (reply.approval_status == "pending" && handlers_.approval_required) handlers_.approval_required();
  return reply;
}

GatewayReply AssistantGateway::queue_approval(const std::string& key, AssistantApproval action) {
  for (const auto& entry : approvals_) {
    if (entry.key != key) continue;
    if (!same_approval(entry.action, action)) {
      return failure(ResultCode::StateConflict, "pending action was reused with different inputs");
    }
    auto reply = entry.reply;
    reply.request_id = action.request.request_id;
    return reply;
  }
  // Retain terminal decisions too: rejected IDs cannot be retried as fresh work.
  if (approvals_.size() >= kMaxPendingAssistantPreviews) {
    return failure(ResultCode::StateConflict, "assistant approval session limit reached");
  }
  action.approval_id = "approval-" + std::to_string(++approval_sequence_);
  auto reply = failure(ResultCode::StateConflict, "desktop confirmation is required; no action has executed");
  reply.request_id = action.request.request_id;
  reply.command_id = key;
  reply.approval_id = action.approval_id;
  reply.approval_status = "pending";
  reply.task_id = action.generation.task_id;
  approvals_.push_back({std::move(action), key, reply});
  return reply;
}

std::vector<AssistantApproval> AssistantGateway::pending_approvals() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<AssistantApproval> pending;
  for (const auto& entry : approvals_) {
    if (entry.reply.approval_status == "pending") pending.push_back(entry.action);
  }
  return pending;
}

GatewayReply AssistantGateway::approval_status(const AssistantRequest& request, const std::string& id) const {
  std::string diagnostic;
  if (validate_request(request, diagnostic) != ResultCode::Ok || !valid_id(id)) {
    return failure(ResultCode::StateConflict, "invalid approval query");
  }
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& entry : approvals_) {
    if (entry.action.approval_id == id && same_context(entry.action.request, request)) {
      auto reply = entry.reply;
      reply.request_id = request.request_id;
      return reply;
    }
  }
  return failure(ResultCode::StateConflict, "assistant approval is unavailable in this context");
}

GatewayReply AssistantGateway::resolve_approval(const std::string& id, bool approved) {
  AssistantApproval action;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = std::find_if(approvals_.begin(), approvals_.end(), [&](const auto& entry) {
      return entry.action.approval_id == id;
    });
    if (found == approvals_.end()) return failure(ResultCode::StateConflict, "assistant approval is unavailable");
    if (found->reply.approval_status != "pending") return found->reply;
    action = found->action;
    found->reply.approval_status = approved ? "executing" : "rejected";
    found->reply.diagnostic = approved ? "desktop action is executing" : "desktop user rejected the action";
  }
  GatewayReply reply;
  if (!approved) {
    if (action.kind == AssistantApprovalKind::Commit) reject(action.request, action.plan.plan_id);
    reply = failure(ResultCode::StateConflict, "desktop user rejected the action");
  } else {
    switch (action.kind) {
      case AssistantApprovalKind::Commit: reply = commit_impl(action.request, action.plan, true); break;
      case AssistantApprovalKind::Undo: reply = undo_impl(action.request, action.command_id, true); break;
      case AssistantApprovalKind::ApplySnapshot: reply = apply_snapshot_impl(action.request, action.snapshot, true); break;
      case AssistantApprovalKind::Generation: reply = run_generation_impl(action.request, action.generation, true); break;
    }
  }
  reply.approval_id = id;
  reply.request_id = action.request.request_id;
  reply.approval_status = !approved ? "rejected" : reply.code == ResultCode::Ok ? "approved" : "failed";
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& entry : approvals_) {
    if (entry.action.approval_id == id) entry.reply = reply;
  }
  return reply;
}

void AssistantGateway::invalidate_approvals() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& entry : approvals_) {
    if (entry.reply.approval_status != "pending") continue;
    entry.reply.approval_status = "expired";
    entry.reply.diagnostic = "assistant context changed or request was cancelled";
  }
  pending_previews_.clear();
}

GatewayReply AssistantGateway::failure(ResultCode code, std::string diagnostic) {
  GatewayReply reply;
  reply.code = code;
  reply.diagnostic = std::move(diagnostic);
  return reply;
}

ResultCode AssistantGateway::validate_request(const AssistantRequest& request, std::string& diagnostic) {
  if (request.protocol_version != kAssistantProtocolVersion) {
    diagnostic = "unsupported assistant protocol version";
    return ResultCode::StateConflict;
  }
  if (!valid_id(request.request_id) || !valid_id(request.thread_id) ||
      !valid_text(request.message, kMaxAssistantMessage, true) || !valid_text(request.mode, 64, true)) {
    diagnostic = "assistant request contains an invalid id, message, or mode";
    return ResultCode::StateConflict;
  }
  if (!request.image.image_id.empty() && !valid_id(request.image.image_id)) {
    diagnostic = "assistant image id is invalid";
    return ResultCode::StateConflict;
  }
  if (!request.image.project_id.empty() && !valid_id(request.image.project_id)) {
    diagnostic = "assistant project id is invalid";
    return ResultCode::StateConflict;
  }
  if (!request.image.editing_instance_id.empty() && !valid_id(request.image.editing_instance_id)) {
    diagnostic = "assistant editing instance id is invalid";
    return ResultCode::StateConflict;
  }
  if (!valid_text(request.image.preview_ref, 4096, false)) {
    diagnostic = "assistant preview reference is invalid";
    return ResultCode::StateConflict;
  }
  if (!request.selection.selection_id.empty() && !valid_id(request.selection.selection_id)) {
    diagnostic = "assistant selection id is invalid";
    return ResultCode::StateConflict;
  }
  diagnostic.clear();
  return ResultCode::Ok;
}

ResultCode AssistantGateway::validate_plan(const EditPlan& plan, std::string& diagnostic) {
  if (plan.protocol_version != kAssistantProtocolVersion || !valid_id(plan.plan_id) ||
      !valid_id(plan.request_id) || !valid_text(plan.summary, 4096, true) || plan.operations.empty() ||
      plan.operations.size() > kMaxAssistantOperations || plan.estimated_points > 1000000000ULL) {
    diagnostic = "assistant edit plan is malformed";
    return ResultCode::StateConflict;
  }
  std::size_t parameter_bytes = 0;
  for (const auto& operation : plan.operations) {
    if (!valid_id(operation.operation_id) || !valid_id(operation.tool_id) ||
        !valid_id(operation.target_selection_id, false)) {
      diagnostic = "assistant edit operation id is invalid";
      return ResultCode::StateConflict;
    }
    std::unordered_set<std::string> parameter_names;
    for (const auto& parameter : operation.parameters) {
      if (!parameter_names.insert(parameter.name).second || !valid_text(parameter.name, 256, true) || !valid_text(parameter.value, kMaxAssistantMessage, true)) {
        diagnostic = "assistant edit parameter is invalid";
        return ResultCode::StateConflict;
      }
      parameter_bytes += parameter.name.size() + parameter.value.size();
      if (parameter_bytes > kMaxAssistantParameters) {
        diagnostic = "assistant edit parameters are too large";
        return ResultCode::StateConflict;
      }
    }
  }
  diagnostic.clear();
  return ResultCode::Ok;
}

GatewayReply AssistantGateway::read_edit_context(const AssistantRequest& request) const {
  std::string diagnostic;
  if (const ResultCode code = validate_request(request, diagnostic); code != ResultCode::Ok) {
    return failure(code, std::move(diagnostic));
  }
  if (!handlers_.read_edit_context) return failure(ResultCode::StateConflict, "edit context is unavailable");
  GatewayReply reply = handlers_.read_edit_context(request);
  if (reply.request_id.empty()) reply.request_id = request.request_id;
  if (reply.code == ResultCode::Ok) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto entry = approvals_.rbegin(); entry != approvals_.rend() && reply.approval_history.size() < 16; ++entry) {
      if (!same_context(entry->action.request, request)) continue;
      const auto& result = entry->reply;
      reply.approval_history.push_back({result.approval_id, result.approval_status, result.command_id,
                                       result.snapshot_id, result.task_id, result.revision});
    }
  }
  return reply;
}

GatewayReply AssistantGateway::read_selection_context(const AssistantRequest& request) const {
  std::string diagnostic;
  if (const ResultCode code = validate_request(request, diagnostic); code != ResultCode::Ok) {
    return failure(code, std::move(diagnostic));
  }
  if (!handlers_.read_selection_context) return failure(ResultCode::StateConflict, "selection context is unavailable");
  GatewayReply reply = handlers_.read_selection_context(request);
  if (reply.request_id.empty()) reply.request_id = request.request_id;
  return reply;
}

GatewayReply AssistantGateway::cached_or(const std::string& command_id,
                                         const std::function<GatewayReply()>& invoke) {
  if (!valid_id(command_id)) return failure(ResultCode::StateConflict, "command id is invalid");
  // Hold the gate while invoking the main-process callback.  The callback is
  // deliberately synchronous and must not call back into this gate; keeping
  // the critical section makes concurrent retries of one command exactly-once.
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = completed_commands_.find(command_id);
  if (found != completed_commands_.end()) return found->second;
  GatewayReply reply = invoke();
  reply.command_id = command_id;
  if (reply.code == ResultCode::Ok) {
    completed_commands_.emplace(command_id, reply);
  }
  return reply;
}

GatewayReply AssistantGateway::preview(const AssistantRequest& request, const EditPlan& plan) {
  std::string diagnostic;
  if (const ResultCode code = validate_request(request, diagnostic); code != ResultCode::Ok) {
    return failure(code, std::move(diagnostic));
  }
  if (const ResultCode code = validate_plan(plan, diagnostic); code != ResultCode::Ok) {
    return failure(code, std::move(diagnostic));
  }
  if (plan.request_id != request.request_id || plan.base_revision != request.base_revision) {
    return failure(ResultCode::StateConflict, "assistant plan does not match request revision");
  }
  if (!handlers_.preview_plan) return failure(ResultCode::StateConflict, "preview handler is unavailable");
  GatewayReply reply = handlers_.preview_plan(request, plan);
  if (reply.request_id.empty()) reply.request_id = request.request_id;
  if (reply.code == ResultCode::Ok) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pending_previews_.size() >= kMaxPendingAssistantPreviews) pending_previews_.erase(pending_previews_.begin());
    pending_previews_[request.request_id + ":" + plan.plan_id] = plan;
  }
  return reply;
}

GatewayReply AssistantGateway::commit(const AssistantRequest& request, const EditPlan& plan,
                                           bool /*untrusted_confirmation*/) {
  return notify_approval(commit_impl(request, plan, false));
}

GatewayReply AssistantGateway::commit_impl(const AssistantRequest& request, const EditPlan& plan,
                                                bool desktop_confirmed) {
  std::string diagnostic;
  if (const ResultCode code = validate_request(request, diagnostic); code != ResultCode::Ok) {
    return failure(code, std::move(diagnostic));
  }
  if (const ResultCode code = validate_plan(plan, diagnostic); code != ResultCode::Ok) {
    return failure(code, std::move(diagnostic));
  }
  if (plan.request_id != request.request_id || plan.base_revision != request.base_revision) {
    return failure(ResultCode::StateConflict, "assistant plan does not match request revision");
  }
  const std::string command_id = request.request_id + ":" + plan.plan_id;
  if (!handlers_.commit_plan) return failure(ResultCode::StateConflict, "commit handler is unavailable");
  GatewayReply reply = cached_or(command_id, [&] {
    const auto pending = pending_previews_.find(command_id);
    if (pending == pending_previews_.end()) {
      return failure(ResultCode::StateConflict, "assistant plan must be previewed before commit");
    }
    if (!same_plan(pending->second, plan)) {
      return failure(ResultCode::StateConflict, "assistant commit does not match the previewed plan");
    }
    if (!desktop_confirmed) {
      AssistantApproval action;
      action.kind = AssistantApprovalKind::Commit;
      action.request = request;
      action.plan = plan;
      return queue_approval(command_id, std::move(action));
    }
    GatewayReply result = handlers_.commit_plan(request, plan);
    if (result.code == ResultCode::Ok) {
      pending_previews_.erase(pending);
      if (committed_commands_.size() >= kMaxPendingAssistantPreviews) committed_commands_.erase(committed_commands_.begin());
      committed_commands_.insert(command_id);
    }
    return result;
  });
  if (reply.request_id.empty()) reply.request_id = request.request_id;
  return reply;
}

GatewayReply AssistantGateway::reject(const AssistantRequest& request, const std::string& plan_id) {
  std::string diagnostic;
  if (const ResultCode code = validate_request(request, diagnostic); code != ResultCode::Ok) {
    return failure(code, std::move(diagnostic));
  }
  if (!valid_id(plan_id)) return failure(ResultCode::StateConflict, "plan id is invalid");
  if (!handlers_.reject_plan) return failure(ResultCode::StateConflict, "reject handler is unavailable");
  const std::string command_id = request.request_id + ":reject:" + plan_id;
  GatewayReply reply = cached_or(command_id, [&] {
    GatewayReply result = handlers_.reject_plan(request, plan_id);
    if (result.code == ResultCode::Ok) {
      const auto key = request.request_id + ":" + plan_id;
      pending_previews_.erase(key);
      for (auto& entry : approvals_) {
        if (entry.key == key && entry.reply.approval_status == "pending") {
          entry.reply.approval_status = "rejected";
          entry.reply.diagnostic = "assistant plan was rejected";
        }
      }
    }
    return result;
  });
  if (reply.request_id.empty()) reply.request_id = request.request_id;
  if (reply.code == ResultCode::Ok && handlers_.approval_required) handlers_.approval_required();
  return reply;
}

GatewayReply AssistantGateway::undo(const AssistantRequest& request, const std::string& command_id,
                                           bool /*untrusted_confirmation*/) {
  return notify_approval(undo_impl(request, command_id, false));
}

GatewayReply AssistantGateway::undo_impl(const AssistantRequest& request, const std::string& command_id,
                                                bool desktop_confirmed) {
  std::string diagnostic;
  if (const ResultCode code = validate_request(request, diagnostic); code != ResultCode::Ok) {
    return failure(code, std::move(diagnostic));
  }
  if (!valid_id(command_id)) return failure(ResultCode::StateConflict, "command id is invalid");
  if (!handlers_.undo_edit) return failure(ResultCode::StateConflict, "undo handler is unavailable");
  const std::string undo_id = request.request_id + ":undo:" + command_id;
  GatewayReply reply = cached_or(undo_id, [&] {
    if (committed_commands_.find(command_id) == committed_commands_.end()) {
      return failure(ResultCode::StateConflict, "assistant command is unknown or was not committed");
    }
    if (!desktop_confirmed) {
      AssistantApproval action;
      action.kind = AssistantApprovalKind::Undo;
      action.request = request;
      action.command_id = command_id;
      return queue_approval(undo_id, std::move(action));
    }
    return handlers_.undo_edit(request, command_id);
  });
  if (reply.request_id.empty()) reply.request_id = request.request_id;
  return reply;
}

GatewayReply AssistantGateway::request_card(const AssistantRequest& request, const AssistantCard& card) {
  std::string diagnostic;
  if (const ResultCode code = validate_request(request, diagnostic); code != ResultCode::Ok) {
    return failure(code, std::move(diagnostic));
  }
  if (!valid_id(card.card_id) || !valid_text(card.card_type, 128, true) || card.fields.size() > 64) {
    return failure(ResultCode::StateConflict, "assistant card is malformed");
  }
  for (const auto& field : card.fields) {
    if (!valid_id(field.field_id) || !valid_text(field.type, 128, true) ||
        !valid_text(field.label, 512, true) || !valid_text(field.value, kMaxAssistantMessage, false)) {
      return failure(ResultCode::StateConflict, "assistant card field is malformed");
    }
  }
  if (!handlers_.request_card) return failure(ResultCode::StateConflict, "card handler is unavailable");
  GatewayReply reply = handlers_.request_card(request, card);
  if (reply.request_id.empty()) reply.request_id = request.request_id;
  return reply;
}

GatewayReply AssistantGateway::create_snapshot(const AssistantRequest& request, const std::string& label) {
  std::string diagnostic;
  if (const ResultCode code = validate_request(request, diagnostic); code != ResultCode::Ok) {
    return failure(code, std::move(diagnostic));
  }
  if (!valid_text(label, 512, false)) return failure(ResultCode::StateConflict, "snapshot label is invalid");
  if (!handlers_.create_snapshot) return failure(ResultCode::StateConflict, "snapshot handler is unavailable");
  const std::string command_id = request.request_id + ":snapshot:create";
  GatewayReply reply = cached_or(command_id, [&] { return handlers_.create_snapshot(request, label); });
  if (reply.code == ResultCode::Ok && !valid_id(reply.snapshot_id)) {
    return failure(ResultCode::StateConflict, "snapshot handler returned an invalid snapshot id");
  }
  if (reply.request_id.empty()) reply.request_id = request.request_id;
  return reply;
}

GatewayReply AssistantGateway::apply_snapshot(const AssistantRequest& request, const AssistantSnapshotRequest& snapshot,
                                           bool /*untrusted_confirmation*/) {
  return notify_approval(apply_snapshot_impl(request, snapshot, false));
}

GatewayReply AssistantGateway::apply_snapshot_impl(const AssistantRequest& request, const AssistantSnapshotRequest& snapshot,
                                                bool desktop_confirmed) {
  std::string diagnostic;
  if (const ResultCode code = validate_request(request, diagnostic); code != ResultCode::Ok) {
    return failure(code, std::move(diagnostic));
  }
  if (!valid_id(snapshot.snapshot_id) || !valid_id(snapshot.image_id, false)) {
    return failure(ResultCode::StateConflict, "snapshot id or image id is invalid");
  }
  if (!handlers_.apply_snapshot) return failure(ResultCode::StateConflict, "snapshot handler is unavailable");
  const std::string command_id = request.request_id + ":snapshot:apply:" + snapshot.snapshot_id;
  GatewayReply reply = cached_or(command_id, [&] {
    if (!desktop_confirmed) {
      AssistantApproval action;
      action.kind = AssistantApprovalKind::ApplySnapshot;
      action.request = request;
      action.snapshot = snapshot;
      return queue_approval(command_id, std::move(action));
    }
    return handlers_.apply_snapshot(request, snapshot);
  });
  if (reply.code == ResultCode::Ok && !valid_id(reply.snapshot_id)) {
    return failure(ResultCode::StateConflict, "snapshot handler returned an invalid snapshot id");
  }
  if (reply.request_id.empty()) reply.request_id = request.request_id;
  return reply;
}

GatewayReply AssistantGateway::list_task_history(const AssistantRequest& request) const {
  std::string diagnostic;
  if (const ResultCode code = validate_request(request, diagnostic); code != ResultCode::Ok) {
    return failure(code, std::move(diagnostic));
  }
  if (!handlers_.list_task_history) return failure(ResultCode::StateConflict, "task history is unavailable");
  GatewayReply reply = handlers_.list_task_history(request);
  if (reply.request_id.empty()) reply.request_id = request.request_id;
  return reply;
}

GatewayReply AssistantGateway::run_generation(const AssistantRequest& request, const AssistantGenerationRequest& task,
                                           bool /*untrusted_confirmation*/) {
  return notify_approval(run_generation_impl(request, task, false));
}

GatewayReply AssistantGateway::run_generation_impl(const AssistantRequest& request, const AssistantGenerationRequest& task,
                                                bool desktop_confirmed) {
  std::string diagnostic;
  if (const ResultCode code = validate_request(request, diagnostic); code != ResultCode::Ok) {
    return failure(code, std::move(diagnostic));
  }
  if (!valid_id(task.task_id) || !valid_id(task.tool_id) || !valid_id(task.image_id) ||
      !valid_id(task.idempotency_key, false) || task.source_revision != request.base_revision ||
      task.parameters.size() > kMaxAssistantOperations || task.mask_ids.size() > kMaxAssistantGenerationReferences ||
      task.reference_image_ids.size() > kMaxAssistantGenerationReferences) {
    return failure(ResultCode::StateConflict, "assistant generation task is malformed or stale");
  }
  if (!request.image.image_id.empty() && request.image.image_id != task.image_id) {
    return failure(ResultCode::StateConflict, "generation task image does not match the request image");
  }
  std::size_t parameter_bytes = 0;
  std::unordered_set<std::string> names;
  for (const auto& parameter : task.parameters) {
    parameter_bytes += parameter.name.size() + parameter.value.size();
    if (!valid_text(parameter.name, 256, true) || !valid_text(parameter.value, kMaxAssistantMessage, false) ||
        parameter_bytes > kMaxAssistantParameters || !names.insert(parameter.name).second) {
      return failure(ResultCode::StateConflict, "assistant generation parameter is invalid");
    }
  }
  for (const auto& id : task.mask_ids) {
    if (!valid_id(id)) return failure(ResultCode::StateConflict, "assistant generation mask id is invalid");
  }
  for (const auto& id : task.reference_image_ids) {
    if (!valid_id(id)) return failure(ResultCode::StateConflict, "assistant generation reference id is invalid");
  }
  if (!handlers_.run_generation) return failure(ResultCode::StateConflict, "generation handler is unavailable");
  const std::string command_id = "generation:" + task.task_id;
  std::lock_guard<std::mutex> lock(mutex_);
  const auto previous = generation_requests_.find(task.task_id);
  if (previous != generation_requests_.end()) {
    if (!same_generation(previous->second, task)) {
      return failure(ResultCode::StateConflict, "generation task id was reused with different inputs");
    }
    GatewayReply replay = completed_commands_.at(command_id);
    replay.request_id = request.request_id;
    return replay;
  }
  for (const auto& entry : generation_requests_) {
    if (!task.idempotency_key.empty() && entry.second.idempotency_key == task.idempotency_key) {
      return failure(ResultCode::StateConflict, "generation idempotency key is already bound to another task");
    }
  }
  if (generation_requests_.size() >= kMaxPendingAssistantPreviews) {
    return failure(ResultCode::StateConflict, "assistant generation session limit reached");
  }
  if (!desktop_confirmed) {
    AssistantApproval action;
    action.kind = AssistantApprovalKind::Generation;
    action.request = request;
    action.generation = task;
    return queue_approval(command_id, std::move(action));
  }
  GatewayReply reply = handlers_.run_generation(request, task);
  if (reply.code == ResultCode::Ok && reply.task_id != task.task_id) {
    return failure(ResultCode::StateConflict, "generation handler returned an invalid task id");
  }
  reply.request_id = request.request_id;
  reply.command_id = command_id;
  if (reply.task_id.empty()) reply.task_id = task.task_id;
  if (reply.code == ResultCode::Ok) {
    generation_requests_.emplace(task.task_id, task);
    completed_commands_.emplace(command_id, reply);
  }
  return reply;
}

GatewayReply AssistantGateway::cancel_generation(const AssistantRequest& request,
                                                 const AssistantGenerationCancelRequest& task) {
  std::string diagnostic;
  if (const ResultCode code = validate_request(request, diagnostic); code != ResultCode::Ok) {
    return failure(code, std::move(diagnostic));
  }
  if (!valid_id(task.task_id)) return failure(ResultCode::StateConflict, "assistant generation task id is invalid");
  std::string pending_id;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& entry : approvals_) {
      if (entry.action.kind != AssistantApprovalKind::Generation || entry.action.generation.task_id != task.task_id ||
          !same_context(entry.action.request, request)) continue;
      if (entry.reply.approval_status == "pending" || entry.reply.approval_status == "rejected" ||
          entry.reply.approval_status == "expired") pending_id = entry.action.approval_id;
    }
  }
  if (!pending_id.empty()) {
    resolve_approval(pending_id, false);
    if (handlers_.approval_required) handlers_.approval_required();
    GatewayReply reply;
    reply.request_id = request.request_id;
    reply.task_id = task.task_id;
    reply.task_status = "cancelled";
    return reply;
  }
  if (!handlers_.cancel_generation) {
    return failure(ResultCode::StateConflict, "generation cancellation handler is unavailable");
  }
  const std::string command_id = request.request_id + ":generation:cancel:" + task.task_id;
  GatewayReply reply = cached_or(command_id, [&] { return handlers_.cancel_generation(request, task); });
  if (reply.code == ResultCode::Ok && (reply.task_id.empty() || !valid_id(reply.task_id))) {
    return failure(ResultCode::StateConflict, "generation cancellation handler returned an invalid task id");
  }
  if (reply.request_id.empty()) reply.request_id = request.request_id;
  if (reply.task_id.empty()) reply.task_id = task.task_id;
  return reply;
}

}  // namespace pixcake::assistant
