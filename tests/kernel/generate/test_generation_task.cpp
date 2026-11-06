#include "pixcake/generate/generation_task.hpp"

#include <cstdio>
#include <string>

namespace {

int g_failures = 0;

void expect(bool condition, const char* expression, int line) {
  if (!condition) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, expression);
    ++g_failures;
  }
}

#define EXPECT(expression) expect(static_cast<bool>(expression), #expression, __LINE__)

pixcake::generate::GenerationTask task() {
  pixcake::generate::GenerationTask value;
  value.task_id = "task-1";
  value.image_id = "image-1";
  value.source_revision = 7;
  value.tool.tool_id = "tool-a";
  value.tool.parameter_mode_id = "mode-a";
  value.tool.result_shape = pixcake::generate::ResultShape::EditableMaskLayer;
  value.tool.billing_rule_id = "billing-a";
  value.tool.output_count = 2;
  value.tool.cancellable = true;
  return value;
}

void test_shape_names_and_task_validation() {
  EXPECT(std::string(pixcake::generate::result_shape_name(pixcake::generate::ResultShape::NewLayer)) == "新图层");
  EXPECT(std::string(pixcake::generate::result_shape_name(
             pixcake::generate::ResultShape::TransformableLayer)) == "可改位置和大小的图层");
  EXPECT(pixcake::generate::validate_task(task()) == pixcake::ResultCode::Ok);

  auto broken = task();
  broken.tool.output_count = 0;
  EXPECT(pixcake::generate::validate_task(broken) == pixcake::ResultCode::StateConflict);
  broken = task();
  broken.task_id.clear();
  EXPECT(pixcake::generate::validate_task(broken) == pixcake::ResultCode::StateConflict);
  broken = task();
  broken.tool.tool_id.push_back('\0');
  EXPECT(pixcake::generate::validate_task(broken) == pixcake::ResultCode::StateConflict);
}

void test_result_binding_and_bounds() {
  const auto value = task();
  pixcake::generate::GenerationResult result;
  result.task_id = "task-1";
  result.result_id = "result-1";
  result.shape = pixcake::generate::ResultShape::EditableMaskLayer;
  result.ordinal = 1;
  result.pixel_file = "creative/result-1.png";
  result.checksum = "sha256:abc";
  result.width = 120;
  result.height = 80;
  EXPECT(pixcake::generate::validate_result(value, result) == pixcake::ResultCode::Ok);

  result.ordinal = 2;
  EXPECT(pixcake::generate::validate_result(value, result) == pixcake::ResultCode::StateConflict);
  result.ordinal = 0;
  result.task_id = "other-task";
  EXPECT(pixcake::generate::validate_result(value, result) == pixcake::ResultCode::StateConflict);
  result.task_id = "task-1";
  result.shape = pixcake::generate::ResultShape::LayerGroup;
  EXPECT(pixcake::generate::validate_result(value, result) == pixcake::ResultCode::StateConflict);
  result.shape = pixcake::generate::ResultShape::EditableMaskLayer;
  result.width = 0;
  EXPECT(pixcake::generate::validate_result(value, result) == pixcake::ResultCode::StateConflict);
}

void test_state_machine_matches_ledger_contract() {
  using pixcake::ResultCode;
  using pixcake::generate::TaskEvent;
  using pixcake::generate::TaskState;
  TaskState state = TaskState::Quoted;
  EXPECT(pixcake::generate::transition(state, TaskEvent::Enqueue, state) == ResultCode::Ok);
  EXPECT(state == TaskState::Queued);
  EXPECT(pixcake::generate::transition(state, TaskEvent::Enqueue, state) == ResultCode::Ok);
  EXPECT(pixcake::generate::transition(state, TaskEvent::Start, state) == ResultCode::Ok);
  EXPECT(state == TaskState::Running);
  EXPECT(pixcake::generate::transition(state, TaskEvent::Start, state) == ResultCode::Ok);
  EXPECT(pixcake::generate::transition(state, TaskEvent::Cancel, state) == ResultCode::Ok);
  EXPECT(state == TaskState::CancelledRunning);
  EXPECT(pixcake::generate::transition(state, TaskEvent::ResultReady, state) == ResultCode::StateConflict);

  state = TaskState::Queued;
  EXPECT(pixcake::generate::transition(state, TaskEvent::Cancel, state) == ResultCode::Ok);
  EXPECT(state == TaskState::CancelledQueued);
  EXPECT(pixcake::generate::transition(state, TaskEvent::Start, state) == ResultCode::StateConflict);

  state = TaskState::Running;
  EXPECT(pixcake::generate::transition(state, TaskEvent::Crash, state) == ResultCode::Ok);
  EXPECT(state == TaskState::Failed);
  EXPECT(pixcake::generate::transition(state, TaskEvent::Crash, state) == ResultCode::Ok);
  EXPECT(pixcake::generate::transition(state, TaskEvent::ResultReady, state) == ResultCode::StateConflict);

  state = TaskState::Running;
  EXPECT(pixcake::generate::transition(state, TaskEvent::ResultReady, state) == ResultCode::Ok);
  EXPECT(state == TaskState::Succeeded);
  EXPECT(pixcake::generate::transition(state, TaskEvent::ResultReady, state) == ResultCode::Ok);
}

}  // namespace

int main() {
  test_shape_names_and_task_validation();
  test_result_binding_and_bounds();
  test_state_machine_matches_ledger_contract();
  return g_failures == 0 ? 0 : 1;
}
