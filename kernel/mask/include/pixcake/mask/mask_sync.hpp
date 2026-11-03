#pragma once

#include <string>
#include <vector>

namespace pixcake {

struct MaskSyncRow {
  std::string id;
  std::string module_label;
  std::string label;
  bool can_batch_sync = false;
  std::string blocked_reason;
};

std::vector<MaskSyncRow> mask_sync_rows();

}  // namespace pixcake
