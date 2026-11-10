#pragma once

#include "pixcake/generate/generation_tool_catalog.hpp"
#include "pixcake/generated/remote_tool.hpp"

#include <string>
#include <unordered_map>

namespace pixcake::generated {

// Provider-neutral routing table.  The tool catalog decides which tool id is
// available; this class only dispatches to an already constructed adapter.
class RemoteToolRegistry {
 public:
  ResultCode add(std::string tool_id, RemoteTool& tool);
  ResultCode add(const generate::ToolDescriptor& descriptor, RemoteTool& tool);
  RemoteTool* find(const std::string& tool_id) const;

 private:
  std::unordered_map<std::string, RemoteTool*> tools_;
};

class RoutedRemoteTool final : public RemoteTool {
 public:
  explicit RoutedRemoteTool(const RemoteToolRegistry& registry) : registry_(registry) {}
  RemoteReply run(const generate::GenerationTask& task) override;

 private:
  const RemoteToolRegistry& registry_;
};

}  // namespace pixcake::generated
