#pragma once

#include <string>
#include <vector>

namespace pixcake {

struct CapabilitySyncRow {
  std::string id;
  std::string module_label;
  std::string label;
  std::string capability_id;
  bool enabled = false;
  std::string blocked_reason;
};

std::vector<CapabilitySyncRow> capability_sync_rows();

}  // namespace pixcake
