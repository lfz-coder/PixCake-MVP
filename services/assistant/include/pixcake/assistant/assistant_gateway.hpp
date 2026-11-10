#pragma once

#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pixcake::assistant {

inline constexpr std::uint32_t kAssistantProtocolVersion = 1;
inline constexpr std::size_t kMaxAssistantId = 256;
inline constexpr std::size_t kMaxAssistantMessage = 16 * 1024;
inline constexpr std::size_t kMaxAssistantOperations = 64;
inline constexpr std::size_t kMaxAssistantParameters = 32 * 1024;

struct ImageContext {
  std::string image_id;
  std::string project_id;
  std::string editing_instance_id;
  std::string preview_ref;
  std::uint64_t source_revision = 0;
};

struct SelectionContext {
  std::string selection_id;
  std::string kind;
  std::string label;
};

struct AssistantRequest {
  std::uint32_t protocol_version = kAssistantProtocolVersion;
  std::string request_id;
  std::string thread_id;
  std::string message;
  ImageContext image;
  SelectionContext selection;
  std::uint64_t base_revision = 0;
  std::string mode = "assistant";
};

struct OperationParameter {
  std::string name;
  std::string value;
};

struct Operation {
  std::string operation_id;
  std::string tool_id;
  std::string target_selection_id;
  std::vector<OperationParameter> parameters;
  bool requires_confirmation = false;
  bool reversible = true;
};

struct EditPlan {
  std::uint32_t protocol_version = kAssistantProtocolVersion;
  std::string plan_id;
  std::string request_id;
  std::string summary;
  std::uint64_t base_revision = 0;
  std::uint64_t estimated_points = 0;
  std::vector<Operation> operations;
  bool requires_confirmation = true;
};

struct AssistantCardField {
  std::string field_id;
  std::string type;
  std::string label;
  std::string value;
  bool required = false;
};

struct AssistantCard {
  std::string card_id;
  std::string card_type;
  std::vector<AssistantCardField> fields;
  bool confirmation_required = true;
};

struct AssistantSnapshotRequest {
  std::string snapshot_id;
  std::string image_id;
  std::string label;
};

struct AssistantTaskHistoryEntry {
  std::string task_id;
  std::string image_id;
  std::string tool_id;
  std::string status;
  std::uint64_t source_revision = 0;
  std::uint32_t result_count = 0;
};

struct AssistantGenerationRequest {
  std::string task_id;
  std::string tool_id;
  std::string image_id;
  std::uint64_t source_revision = 0;
  std::vector<OperationParameter> parameters;
  std::vector<std::string> mask_ids;
  std::vector<std::string> reference_image_ids;
  std::string idempotency_key;
};

struct AssistantGenerationCancelRequest {
  std::string task_id;
};

enum class AssistantApprovalKind { Commit, Undo, ApplySnapshot, Generation };

// Immutable intent owned by C++. A model-supplied flag/card never grants approval.
struct AssistantApproval {
  std::string approval_id;
  AssistantApprovalKind kind = AssistantApprovalKind::Commit;
  AssistantRequest request;
  EditPlan plan;
  std::string command_id;
  AssistantSnapshotRequest snapshot;
  AssistantGenerationRequest generation;
};

// Read-only capability metadata exposed to the sidecar.  These records are
// intentionally provider-neutral: the main process owns the registry and the
// sidecar may use the values to form a plan, but it cannot change them.
struct AssistantCapabilityDescriptor {
  std::string capability_id;
  std::string module_label;
  std::string label;
  bool can_use_in_preset = true;
  bool can_batch_sync = true;
  std::string batch_sync_blocked_reason;
};

struct AssistantParameterDisableRule {
  std::string capability_id;
  std::string parameter_id;
  std::string when_choice;
  std::string reason;
  bool when_zero = false;
};

struct AssistantParameterDescriptor {
  std::string capability_id;
  std::string parameter_id;
  double minimum = 0.0;
  double maximum = 0.0;
  double step = 1.0;
  double default_value = 0.0;
  std::string render_stage;
  std::string render_program;
  std::string render_channel;
  std::uint32_t schema_version = 1;
  std::uint32_t order = 0;
  std::vector<std::string> choices;
  std::string default_choice;
  bool step_confirmed = true;
  bool default_confirmed = true;
  bool text = false;
  std::uint32_t max_length = 0;
  std::vector<AssistantParameterDisableRule> disable_rules;
};

struct AssistantApprovalResult {
  std::string approval_id;
  std::string status;
  std::string command_id;
  std::string snapshot_id;
  std::string task_id;
  std::uint64_t revision = 0;
};

struct GatewayReply {
  ResultCode code = ResultCode::Ok;
  std::string diagnostic;
  std::string request_id;
  std::string command_id;
  std::string image_id;
  std::string editing_instance_id;
  std::string preview_id;
  std::string snapshot_id;
  std::string selection_id;
  std::string selection_kind;
  std::string selection_label;
  std::string task_id;
  std::string task_status;
  std::string approval_id;
  std::string approval_status;
  std::uint64_t revision = 0;
  std::vector<std::string> capabilities;
  std::vector<AssistantCapabilityDescriptor> capability_descriptors;
  std::vector<AssistantParameterDescriptor> parameter_descriptors;
  std::vector<AssistantTaskHistoryEntry> task_history;
  std::vector<AssistantApprovalResult> approval_history;
  std::vector<std::string> notices;
  AssistantCard card;
};

struct GatewayHandlers {
  // These callbacks are owned by the main process.  They are the only place
  // that knows EditDocument, CommandGate, renderd, generated or the ledger.
  std::function<GatewayReply(const AssistantRequest&)> read_edit_context;
  std::function<GatewayReply(const AssistantRequest&)> read_selection_context;
  std::function<GatewayReply(const AssistantRequest&, const EditPlan&)> preview_plan;
  std::function<GatewayReply(const AssistantRequest&, const EditPlan&)> commit_plan;
  std::function<GatewayReply(const AssistantRequest&, const std::string&)> reject_plan;
  std::function<GatewayReply(const AssistantRequest&, const std::string&)> undo_edit;
  std::function<GatewayReply(const AssistantRequest&, const AssistantCard&)> request_card;
  std::function<GatewayReply(const AssistantRequest&, const std::string&)> create_snapshot;
  std::function<GatewayReply(const AssistantRequest&, const AssistantSnapshotRequest&)> apply_snapshot;
  std::function<GatewayReply(const AssistantRequest&)> list_task_history;
  std::function<GatewayReply(const AssistantRequest&, const AssistantGenerationRequest&)> run_generation;
  std::function<GatewayReply(const AssistantRequest&, const AssistantGenerationCancelRequest&)>
      cancel_generation;
  // Called outside the gateway lock, so the UI can read pending_approvals().
  std::function<void()> approval_required;
};

enum class GatewayAction { Preview, Commit, Reject, Undo };

// A deterministic domain gate for Python/HTTP/socket adapters.  It validates
// bounds and desktop-owned approval before invoking a main-process callback, and caches
// command results so a retried JSON request cannot commit twice.
class AssistantGateway final {
 public:
  explicit AssistantGateway(GatewayHandlers handlers);

  GatewayReply read_edit_context(const AssistantRequest& request) const;
  GatewayReply read_selection_context(const AssistantRequest& request) const;
  GatewayReply preview(const AssistantRequest& request, const EditPlan& plan);
  // The optional boolean is a legacy wire hint, never an authorization grant.
  GatewayReply commit(const AssistantRequest& request, const EditPlan& plan, bool untrusted_confirmation = false);
  GatewayReply reject(const AssistantRequest& request, const std::string& plan_id);
  GatewayReply undo(const AssistantRequest& request, const std::string& command_id, bool untrusted_confirmation = false);
  GatewayReply request_card(const AssistantRequest& request, const AssistantCard& card);
  GatewayReply create_snapshot(const AssistantRequest& request, const std::string& label);
  GatewayReply apply_snapshot(const AssistantRequest& request, const AssistantSnapshotRequest& snapshot,
                              bool untrusted_confirmation = false);
  GatewayReply list_task_history(const AssistantRequest& request) const;
  GatewayReply run_generation(const AssistantRequest& request, const AssistantGenerationRequest& task,
                              bool untrusted_confirmation = false);
  GatewayReply cancel_generation(const AssistantRequest& request,
                                 const AssistantGenerationCancelRequest& task);

  std::vector<AssistantApproval> pending_approvals() const;
  GatewayReply approval_status(const AssistantRequest& request, const std::string& approval_id) const;
  // Trusted desktop entry point. Never expose this through JSON/socket/tool adapters.
  GatewayReply resolve_approval(const std::string& approval_id, bool approved);
  void invalidate_approvals();

  static ResultCode validate_request(const AssistantRequest& request, std::string& diagnostic);
  static ResultCode validate_plan(const EditPlan& plan, std::string& diagnostic);

 private:
  GatewayReply commit_impl(const AssistantRequest&, const EditPlan&, bool desktop_confirmed);
  GatewayReply undo_impl(const AssistantRequest&, const std::string&, bool desktop_confirmed);
  GatewayReply apply_snapshot_impl(const AssistantRequest&, const AssistantSnapshotRequest&, bool desktop_confirmed);
  GatewayReply run_generation_impl(const AssistantRequest&, const AssistantGenerationRequest&, bool desktop_confirmed);
  GatewayReply queue_approval(const std::string& key, AssistantApproval approval);  // mutex_ held
  GatewayReply notify_approval(GatewayReply reply);
  struct ApprovalState {
    AssistantApproval action;
    std::string key;
    GatewayReply reply;
  };
  GatewayReply cached_or(const std::string& command_id, const std::function<GatewayReply()>& invoke);
  static GatewayReply failure(ResultCode code, std::string diagnostic);

  GatewayHandlers handlers_;
  mutable std::mutex mutex_;
  std::unordered_map<std::string, GatewayReply> completed_commands_;
  std::unordered_map<std::string, EditPlan> pending_previews_;
  std::unordered_set<std::string> committed_commands_;
  std::unordered_map<std::string, AssistantGenerationRequest> generation_requests_;
  std::vector<ApprovalState> approvals_;
  std::uint64_t approval_sequence_ = 0;
};

}  // namespace pixcake::assistant
