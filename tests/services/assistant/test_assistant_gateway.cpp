#include "pixcake/assistant/assistant_gateway.hpp"

#include <cstdlib>
#include <cstdio>
#include <string>

namespace {

using pixcake::ResultCode;
using pixcake::assistant::AssistantGateway;
using pixcake::assistant::AssistantRequest;
using pixcake::assistant::EditPlan;
using pixcake::assistant::GatewayHandlers;
using pixcake::assistant::GatewayReply;
using pixcake::assistant::Operation;

AssistantRequest request() {
  AssistantRequest value;
  value.request_id = "request-1";
  value.thread_id = "thread-1";
  value.message = "肤色亮半档";
  value.image.image_id = "image-1";
  value.image.editing_instance_id = "edit-1";
  value.base_revision = 3;
  return value;
}

EditPlan plan() {
  EditPlan value;
  value.plan_id = "plan-1";
  value.request_id = "request-1";
  value.summary = "preview a small skin adjustment";
  value.base_revision = 3;
  value.operations.push_back(Operation{"op-1", "portrait_skin", "", {}, false, true});
  return value;
}

void check(bool condition, int line) {
  if (!condition) { std::fprintf(stderr, "assistant gateway assertion at line %d\n", line); std::abort(); }
}
#define expect(condition) check((condition), __LINE__)

}  // namespace

int main() {
  int commits = 0;
  int undos = 0;
  int snapshots = 0;
  int generations = 0;
  int cancellations = 0;
  GatewayHandlers handlers;
  handlers.read_edit_context = [](const AssistantRequest& value) {
    GatewayReply reply;
    reply.request_id = value.request_id;
    reply.revision = value.base_revision;
    reply.capabilities = {"portrait_skin"};
    reply.capability_descriptors.push_back(
        {"portrait_skin", "portrait", "skin adjustment", true, true, ""});
    reply.parameter_descriptors.push_back(
        {"portrait_skin", "exposure", -1.0, 1.0, 0.1, 0.0, "tone", "pixcake.skin", "exposure", 1, 0, {},
         "", true, true, false, 0, {}});
    return reply;
  };
  handlers.preview_plan = [](const AssistantRequest&, const EditPlan&) {
    GatewayReply reply;
    reply.preview_id = "preview-1";
    return reply;
  };
  handlers.commit_plan = [&commits](const AssistantRequest&, const EditPlan&) {
    ++commits;
    GatewayReply reply;
    reply.revision = 4;
    reply.snapshot_id = "snapshot-1";
    return reply;
  };
  handlers.reject_plan = [](const AssistantRequest&, const std::string&) { return GatewayReply{}; };
  handlers.undo_edit = [&undos](const AssistantRequest&, const std::string&) {
    ++undos;
    return GatewayReply{};
  };
  handlers.create_snapshot = [&snapshots](const AssistantRequest&, const std::string&) {
    ++snapshots;
    GatewayReply reply;
    reply.snapshot_id = "snapshot-1";
    return reply;
  };
  handlers.apply_snapshot = [&snapshots](const AssistantRequest&, const pixcake::assistant::AssistantSnapshotRequest&) {
    ++snapshots;
    GatewayReply reply;
    reply.snapshot_id = "snapshot-1";
    return reply;
  };
  handlers.list_task_history = [](const AssistantRequest& value) {
    GatewayReply reply;
    reply.request_id = value.request_id;
    reply.task_history.push_back({"task-1", "image-1", "creative.fixture", "成功", 3, 1});
    return reply;
  };
  handlers.run_generation = [&generations](const AssistantRequest&, const pixcake::assistant::AssistantGenerationRequest& task) {
    ++generations;
    GatewayReply reply;
    reply.task_id = task.task_id;
    reply.task_status = "queued";
    return reply;
  };
  handlers.cancel_generation = [&cancellations](const AssistantRequest&,
                                                const pixcake::assistant::AssistantGenerationCancelRequest& task) {
    ++cancellations;
    GatewayReply reply;
    reply.task_id = task.task_id;
    reply.task_status = "cancelling";
    return reply;
  };

  AssistantGateway gateway(std::move(handlers));
  const auto context = gateway.read_edit_context(request());
  expect(context.code == ResultCode::Ok && context.revision == 3 && context.capability_descriptors.size() == 1 &&
         context.parameter_descriptors.size() == 1 && context.parameter_descriptors.front().parameter_id == "exposure");

  const auto bypass = gateway.commit(request(), plan(), true);
  expect(bypass.code == ResultCode::StateConflict && commits == 0);

  const auto preview = gateway.preview(request(), plan());
  expect(preview.code == ResultCode::Ok && preview.preview_id == "preview-1");

  const auto refused = gateway.commit(request(), plan(), false);
  expect(refused.code == ResultCode::StateConflict && commits == 0);

  auto altered = plan();
  altered.operations.front().tool_id = "color.global";
  const auto altered_commit = gateway.commit(request(), altered, true);
  expect(altered_commit.code == ResultCode::StateConflict && commits == 0);

  const auto forged = gateway.commit(request(), plan(), true);
  expect(forged.approval_id == refused.approval_id && forged.approval_status == "pending" && commits == 0);
  expect(gateway.pending_approvals().size() == 1);
  const auto committed = gateway.resolve_approval(forged.approval_id, true);
  expect(gateway.resolve_approval(forged.approval_id, true).code == ResultCode::Ok && commits == 1);
  expect(committed.code == ResultCode::Ok && committed.request_id == "request-1" && commits == 1);
  const auto replayed = gateway.commit(request(), plan(), true);
  expect(replayed.code == ResultCode::Ok && commits == 1);

  const auto unknown_undo = gateway.undo(request(), "other-command", true);
  expect(unknown_undo.code == ResultCode::StateConflict && undos == 0);
  const auto undo_pending = gateway.undo(request(), "request-1:plan-1", true);
  expect(undo_pending.approval_status == "pending" && undos == 0);
  const auto undone = gateway.resolve_approval(undo_pending.approval_id, true);
  expect(undone.code == ResultCode::Ok && undos == 1);
  const auto undo_replayed = gateway.undo(request(), "request-1:plan-1", true);
  expect(undo_replayed.code == ResultCode::Ok && undos == 1);

  const auto created_snapshot = gateway.create_snapshot(request(), "before later edit");
  expect(created_snapshot.code == ResultCode::Ok && created_snapshot.snapshot_id == "snapshot-1" && snapshots == 1);
  const auto created_snapshot_replay = gateway.create_snapshot(request(), "different label");
  expect(created_snapshot_replay.code == ResultCode::Ok && snapshots == 1);
  pixcake::assistant::AssistantSnapshotRequest snapshot_request;
  snapshot_request.snapshot_id = "snapshot-1";
  const auto refused_snapshot = gateway.apply_snapshot(request(), snapshot_request, false);
  expect(refused_snapshot.code == ResultCode::StateConflict && snapshots == 1);
  const auto pending_snapshot = gateway.apply_snapshot(request(), snapshot_request, true);
  expect(pending_snapshot.approval_status == "pending" && snapshots == 1);
  const auto applied_snapshot = gateway.resolve_approval(pending_snapshot.approval_id, true);
  expect(applied_snapshot.code == ResultCode::Ok && snapshots == 2);

  const auto history = gateway.list_task_history(request());
  expect(history.code == ResultCode::Ok && history.task_history.size() == 1 &&
         history.task_history.front().task_id == "task-1");

  pixcake::assistant::AssistantGenerationRequest generation;
  generation.task_id = "generation-1";
  generation.tool_id = "creative.fixture";
  generation.image_id = "image-1";
  generation.source_revision = 3;
  generation.idempotency_key = "generation-idempotency-1";
  generation.parameters.push_back({"prompt", "a studio portrait"});
  const auto generation_refused = gateway.run_generation(request(), generation, false);
  expect(generation_refused.code == ResultCode::StateConflict && generations == 0);
  const auto generation_pending = gateway.run_generation(request(), generation, true);
  expect(generation_pending.approval_status == "pending" && generations == 0);
  auto tampered = generation;
  tampered.parameters.front().value = "changed before approval";
  expect(gateway.run_generation(request(), tampered, true).code == ResultCode::StateConflict);
  const auto generation_queued = gateway.resolve_approval(generation_pending.approval_id, true);
  expect(generation_queued.code == ResultCode::Ok && generation_queued.task_id == "generation-1" && generations == 1);
  const auto generation_replayed = gateway.run_generation(request(), generation, true);
  expect(generation_replayed.code == ResultCode::Ok && generations == 1);
  auto next_request = request();
  next_request.request_id = "request-2";
  expect(gateway.run_generation(next_request, generation, true).code == ResultCode::Ok && generations == 1);
  auto changed_generation = generation;
  changed_generation.parameters.front().value = "different content";
  expect(gateway.run_generation(request(), changed_generation, true).code == ResultCode::StateConflict && generations == 1);
  changed_generation = generation;
  changed_generation.task_id = "generation-2";
  expect(gateway.run_generation(request(), changed_generation, true).code == ResultCode::StateConflict && generations == 1);
  changed_generation = generation;
  changed_generation.source_revision = 2;
  expect(gateway.run_generation(request(), changed_generation, true).code == ResultCode::StateConflict && generations == 1);
  changed_generation = generation;
  changed_generation.parameters.push_back(changed_generation.parameters.front());
  expect(gateway.run_generation(request(), changed_generation, true).code == ResultCode::StateConflict && generations == 1);
  changed_generation = generation;
  changed_generation.image_id = "another-image";
  expect(gateway.run_generation(request(), changed_generation, true).code == ResultCode::StateConflict && generations == 1);

  const auto cancelled = gateway.cancel_generation(request(), {"generation-1"});
  expect(cancelled.code == ResultCode::Ok && cancelled.task_status == "cancelling" && cancellations == 1);
  expect(gateway.cancel_generation(request(), {"generation-1"}).code == ResultCode::Ok && cancellations == 1);
  expect(gateway.cancel_generation(request(), {"../invalid"}).code == ResultCode::StateConflict && cancellations == 1);

  expect(gateway.approval_status(request(), generation_pending.approval_id).approval_status == "approved");
  auto other_context = request();
  other_context.image.editing_instance_id = "other-edit";
  expect(gateway.approval_status(other_context, generation_pending.approval_id).approval_id.empty());
  generation.task_id = "denied-task";
  generation.idempotency_key.clear();
  const auto denied = gateway.run_generation(request(), generation, true);
  expect(gateway.resolve_approval(denied.approval_id, false).approval_status == "rejected");
  expect(gateway.resolve_approval(denied.approval_id, true).approval_status == "rejected" && generations == 1);
  expect(gateway.run_generation(request(), generation, true).approval_status == "rejected");
  generation.task_id = "expired-task";
  const auto expired = gateway.run_generation(request(), generation, true);
  gateway.invalidate_approvals();
  expect(gateway.resolve_approval(expired.approval_id, true).approval_status == "expired" && generations == 1);
  expect(gateway.pending_approvals().empty());

  generation.task_id = "cancel-before-approval";
  const auto cancelled_pending = gateway.run_generation(request(), generation, true);
  expect(gateway.cancel_generation(request(), {generation.task_id}).task_status == "cancelled");
  expect(gateway.cancel_generation(request(), {generation.task_id}).task_status == "cancelled");
  expect(gateway.resolve_approval(cancelled_pending.approval_id, true).approval_status == "rejected");
  expect(generations == 1 && cancellations == 1);
  const auto decisions = gateway.read_edit_context(request());
  expect(!decisions.approval_history.empty() && decisions.approval_history.front().status == "rejected");

  auto stale = plan();
  stale.base_revision = 2;
  const auto stale_reply = gateway.preview(request(), stale);
  expect(stale_reply.code == ResultCode::StateConflict);
  return 0;
}
