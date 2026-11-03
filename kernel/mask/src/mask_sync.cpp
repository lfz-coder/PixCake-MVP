#include "pixcake/mask/mask_sync.hpp"

namespace pixcake {

std::vector<MaskSyncRow> mask_sync_rows() {
  MaskSyncRow brush;
  brush.id = "mask.brush";
  brush.module_label = "色彩";
  brush.label = "自定义画笔蒙版";
  brush.can_batch_sync = false;
  brush.blocked_reason = "自定义里的画笔不能同步";
  return {brush};
}

}  // namespace pixcake
