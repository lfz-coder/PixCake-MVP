#include "pixcake/generate/generation_task.hpp"

#include <array>

namespace pixcake::generate {
namespace {

bool valid_identifier(const std::string& value) {
  return !value.empty() && value.find('\0') == std::string::npos;
}

bool valid_shape(ResultShape shape) {
  switch (shape) {
    case ResultShape::NewLayer:
    case ResultShape::ReplaceCurrentGeneratedLayer:
    case ResultShape::DetailAndEffectLayers:
    case ResultShape::LayerGroup:
    case ResultShape::EditableMaskLayer:
    case ResultShape::TransformableLayer:
      return true;
  }
  return false;
}

}  // namespace

const char* result_shape_name(ResultShape shape) {
  switch (shape) {
    case ResultShape::NewLayer:
      return "新图层";
    case ResultShape::ReplaceCurrentGeneratedLayer:
      return "替换当前生成图层";
    case ResultShape::DetailAndEffectLayers:
      return "细节层加效果层";
    case ResultShape::LayerGroup:
      return "图层组";
    case ResultShape::EditableMaskLayer:
      return "可编辑蒙版的图层";
    case ResultShape::TransformableLayer:
      return "可改位置和大小的图层";
  }
  return "";
}

const char* task_state_name(TaskState state) {
  switch (state) {
    case TaskState::Quoted:
      return "已报价";
    case TaskState::Queued:
      return "排队中";
    case TaskState::Running:
      return "生成中";
    case TaskState::Succeeded:
      return "成功";
    case TaskState::Failed:
      return "失败";
    case TaskState::CancelledQueued:
      return "排队取消";
    case TaskState::CancelledRunning:
      return "生成中取消";
  }
  return "";
}

ResultCode validate_tool_spec(const ToolSpec& spec) {
  if (!valid_identifier(spec.tool_id) || !valid_identifier(spec.parameter_mode_id) ||
      !valid_identifier(spec.billing_rule_id) || !valid_shape(spec.result_shape) || spec.output_count == 0) {
    return ResultCode::StateConflict;
  }
  return ResultCode::Ok;
}

ResultCode validate_task(const GenerationTask& task) {
  if (!valid_identifier(task.task_id) || !valid_identifier(task.image_id)) {
    return ResultCode::StateConflict;
  }
  return validate_tool_spec(task.tool);
}

ResultCode validate_result(const GenerationTask& task, const GenerationResult& result) {
  if (validate_task(task) != ResultCode::Ok || !valid_identifier(result.task_id) ||
      !valid_identifier(result.result_id) || !valid_identifier(result.pixel_file) ||
      !valid_identifier(result.checksum) || !valid_shape(result.shape) || result.width == 0 || result.height == 0) {
    return ResultCode::StateConflict;
  }
  if (result.task_id != task.task_id || result.shape != task.tool.result_shape ||
      result.ordinal >= task.tool.output_count) {
    return ResultCode::StateConflict;
  }
  return ResultCode::Ok;
}

ResultCode transition(TaskState current, TaskEvent event, TaskState& next) {
  next = current;
  switch (current) {
    case TaskState::Quoted:
      if (event == TaskEvent::Enqueue) {
        next = TaskState::Queued;
        return ResultCode::Ok;
      }
      return ResultCode::StateConflict;
    case TaskState::Queued:
      if (event == TaskEvent::Enqueue) {
        return ResultCode::Ok;
      }
      if (event == TaskEvent::Start) {
        next = TaskState::Running;
        return ResultCode::Ok;
      }
      if (event == TaskEvent::Cancel) {
        next = TaskState::CancelledQueued;
        return ResultCode::Ok;
      }
      if (event == TaskEvent::Fail) {
        next = TaskState::Failed;
        return ResultCode::Ok;
      }
      return ResultCode::StateConflict;
    case TaskState::Running:
      if (event == TaskEvent::Start) {
        return ResultCode::Ok;
      }
      if (event == TaskEvent::Cancel) {
        next = TaskState::CancelledRunning;
        return ResultCode::Ok;
      }
      if (event == TaskEvent::ResultReady) {
        next = TaskState::Succeeded;
        return ResultCode::Ok;
      }
      if (event == TaskEvent::Fail || event == TaskEvent::Crash) {
        next = TaskState::Failed;
        return ResultCode::Ok;
      }
      return ResultCode::StateConflict;
    case TaskState::Succeeded:
      return event == TaskEvent::ResultReady ? ResultCode::Ok : ResultCode::StateConflict;
    case TaskState::Failed:
      return (event == TaskEvent::Fail || event == TaskEvent::Crash) ? ResultCode::Ok
                                                                      : ResultCode::StateConflict;
    case TaskState::CancelledQueued:
    case TaskState::CancelledRunning:
      return ResultCode::StateConflict;
  }
  return ResultCode::StateConflict;
}

}  // namespace pixcake::generate
