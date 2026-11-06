#pragma once

#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pixcake::generate {

enum class ResultShape {
  NewLayer,
  ReplaceCurrentGeneratedLayer,
  DetailAndEffectLayers,
  LayerGroup,
  EditableMaskLayer,
  TransformableLayer,
};

enum class TaskState {
  Quoted,
  Queued,
  Running,
  Succeeded,
  Failed,
  CancelledQueued,
  CancelledRunning,
};

enum class TaskEvent {
  Enqueue,
  Start,
  Cancel,
  ResultReady,
  Fail,
  Crash,
};

struct ToolSpec {
  std::string tool_id;
  std::string parameter_mode_id;
  ResultShape result_shape = ResultShape::NewLayer;
  std::string billing_rule_id;
  std::uint32_t output_count = 1;
  bool cancellable = false;
};

struct GenerationTask {
  std::string task_id;
  std::string image_id;
  std::uint64_t source_revision = 0;
  ToolSpec tool;
};

struct GenerationResult {
  std::string task_id;
  std::string result_id;
  ResultShape shape = ResultShape::NewLayer;
  std::uint32_t ordinal = 0;
  std::string pixel_file;
  std::string checksum;
  // Opaque parameters that the layer document may expose for continued editing.
  // The kernel contract carries them but does not interpret a tool-specific schema.
  std::vector<std::uint8_t> continuation_parameters;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

const char* result_shape_name(ResultShape shape);
const char* task_state_name(TaskState state);

ResultCode validate_tool_spec(const ToolSpec& spec);
ResultCode validate_task(const GenerationTask& task);
ResultCode validate_result(const GenerationTask& task, const GenerationResult& result);

ResultCode transition(TaskState current, TaskEvent event, TaskState& next);

}  // namespace pixcake::generate
