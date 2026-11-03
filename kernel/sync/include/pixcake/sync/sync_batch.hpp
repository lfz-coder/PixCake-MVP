#pragma once

#include <string>
#include <vector>

namespace pixcake {

struct SyncItem {
  std::string id;
  std::string module_label;
  std::string label;
  std::string capability_id;
  bool enabled = false;
  std::string blocked_reason;
};

std::vector<SyncItem> sync_tree();

}  // namespace pixcake
