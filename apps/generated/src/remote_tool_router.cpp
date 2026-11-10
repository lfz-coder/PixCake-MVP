#include "pixcake/generated/remote_tool_router.hpp"

namespace pixcake::generated {

ResultCode RemoteToolRegistry::add(std::string tool_id, RemoteTool& tool) {
  if (tool_id.empty() || tool_id.find('\0') != std::string::npos || tools_.find(tool_id) != tools_.end()) {
    return ResultCode::StateConflict;
  }
  tools_.emplace(std::move(tool_id), &tool);
  return ResultCode::Ok;
}

ResultCode RemoteToolRegistry::add(const generate::ToolDescriptor& descriptor, RemoteTool& tool) {
  if (validate_tool_spec(descriptor.spec) != ResultCode::Ok || descriptor.adapter_id.empty() ||
      descriptor.adapter_id.find('\0') != std::string::npos) {
    return ResultCode::StateConflict;
  }
  return add(descriptor.spec.tool_id, tool);
}

RemoteTool* RemoteToolRegistry::find(const std::string& tool_id) const {
  const auto found = tools_.find(tool_id);
  return found == tools_.end() ? nullptr : found->second;
}

RemoteReply RoutedRemoteTool::run(const generate::GenerationTask& task) {
  RemoteTool* tool = registry_.find(task.tool.tool_id);
  if (tool == nullptr) {
    RemoteReply reply;
    reply.code = ResultCode::UnsupportedFormat;
    return reply;
  }
  return tool->run(task);
}

}  // namespace pixcake::generated
