#include "pixcake/sync/sync_batch.hpp"

#include "pixcake/capability/capability_sync.hpp"
#include "pixcake/mask/mask_sync.hpp"

namespace pixcake {
namespace {

SyncItem item_from(const CapabilitySyncRow& row) {
  SyncItem item;
  item.id = row.id;
  item.module_label = row.module_label;
  item.label = row.label;
  item.capability_id = row.capability_id;
  item.enabled = row.enabled;
  item.blocked_reason = row.blocked_reason;
  return item;
}

SyncItem item_from(const MaskSyncRow& row) {
  SyncItem item;
  item.id = row.id;
  item.module_label = row.module_label;
  item.label = row.label;
  item.enabled = row.can_batch_sync;
  item.blocked_reason = row.blocked_reason;
  return item;
}

}  // namespace

std::vector<SyncItem> sync_tree() {
  std::vector<SyncItem> tree;
  for (const auto& row : capability_sync_rows()) {
    tree.push_back(item_from(row));
  }
  for (const auto& row : mask_sync_rows()) {
    tree.push_back(item_from(row));
  }
  return tree;
}

}  // namespace pixcake
