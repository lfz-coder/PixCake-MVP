#include "pixcake/sync/sync_batch.hpp"

#include "pixcake/capability/capability_sync.hpp"
#include "pixcake/mask/mask_sync.hpp"
#include "pixcake/persist/write_lock.hpp"

#include <cstddef>
#include <map>
#include <utility>
#include <vector>

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

bool checked_capabilities(const std::vector<std::string>& checked, std::vector<std::string>& capability_ids) {
  if (checked.empty()) {
    return false;
  }
  for (std::size_t i = 0; i < checked.size(); ++i) {
    for (std::size_t j = i + 1; j < checked.size(); ++j) {
      if (checked[i] == checked[j]) {
        return false;
      }
    }
  }
  const auto tree = sync_tree();
  for (const auto& id : checked) {
    const SyncItem* found = nullptr;
    for (const auto& item : tree) {
      if (item.id == id) {
        found = &item;
        break;
      }
    }
    if (found == nullptr || !found->enabled || found->capability_id.empty()) {
      return false;
    }
    capability_ids.push_back(found->capability_id);
  }
  return true;
}

ResultCode apply_one(const EditDocument& source, const std::string& source_path,
                     const std::vector<std::string>& capability_ids, const SyncTarget& target) {
  if (target.document == nullptr || target.files == nullptr || target.path.empty() ||
      target.path == source_path) {
    return ResultCode::StateConflict;
  }
  std::vector<FragmentView> replacements;
  std::vector<std::string> removable_ids;
  for (const auto& id : capability_ids) {
    const auto existing = target.document->fragment(id);
    const auto source_fragment = source.fragment(id);
    if (existing && existing->version > 1) {
      // Preserve a fragment from a newer producer even when the selected
      // source has no replacement for it.
      continue;
    }
    removable_ids.push_back(id);
    if (source_fragment) replacements.push_back(*source_fragment);
  }
  if (removable_ids.empty() && replacements.empty()) return ResultCode::Ok;
  WriteScope scope(*target.files, target.path, target.latch);
  if (!scope) return scope.code();
  return target.document->commit_fragment_replacement(target.expected_revision, removable_ids, replacements,
                                                      *target.files, target.path);
}

std::vector<SyncItemResult> results_for(const std::vector<SyncTarget>& targets, ResultCode code) {
  std::vector<SyncItemResult> results;
  results.reserve(targets.size());
  for (const auto& target : targets) {
    SyncItemResult result;
    result.id = target.id;
    result.path = target.path;
    result.code = code;
    results.push_back(std::move(result));
  }
  return results;
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

std::vector<SyncItemResult> apply_checked(const EditDocument& source, const std::string& source_path,
                                         const std::vector<std::string>& checked_ids,
                                         const std::vector<SyncTarget>& targets) {
  std::vector<std::string> capability_ids;
  if (!checked_capabilities(checked_ids, capability_ids)) {
    return results_for(targets, ResultCode::StateConflict);
  }
  std::vector<SyncItemResult> results;
  results.reserve(targets.size());
  for (const auto& target : targets) {
    SyncItemResult result;
    result.id = target.id;
    result.path = target.path;
    result.code = apply_one(source, source_path, capability_ids, target);
    results.push_back(std::move(result));
  }
  return results;
}

std::vector<SyncItemResult> retry_failed(const EditDocument& source, const std::string& source_path,
                                        const std::vector<std::string>& checked_ids,
                                        const std::vector<SyncTarget>& targets,
                                        const std::vector<SyncItemResult>& previous) {
  if (previous.size() != targets.size()) {
    return results_for(targets, ResultCode::StateConflict);
  }
  std::vector<std::string> capability_ids;
  const bool selection_ok = checked_capabilities(checked_ids, capability_ids);
  std::map<std::string, ResultCode> previous_by_id;
  for (const auto& result : previous) {
    const std::string id = result.id.empty() ? result.path : result.id;
    if (!previous_by_id.emplace(id, result.code).second) {
      return results_for(targets, ResultCode::StateConflict);
    }
  }
  std::vector<SyncItemResult> results;
  results.reserve(targets.size());
  for (const auto& target : targets) {
    SyncItemResult result;
    result.path = target.path;
    const std::string id = target.id.empty() ? target.path : target.id;
    const auto prior = previous_by_id.find(id);
    if (prior == previous_by_id.end()) {
      result.code = ResultCode::StateConflict;
    } else if (prior->second == ResultCode::Ok) {
      result.code = ResultCode::Ok;
    } else if (!selection_ok) {
      result.code = ResultCode::StateConflict;
    } else {
      result.code = apply_one(source, source_path, capability_ids, target);
    }
    results.push_back(std::move(result));
  }
  return results;
}

EffectClipboard copy_committed(const EditDocument& source) {
  EffectClipboard clipboard;
  clipboard.fragments = source.fragments();
  return clipboard;
}

ResultCode paste_committed(EditDocument& target, const EffectClipboard& clipboard,
                           std::uint64_t expected_revision, FileSystem& files, const std::string& path,
                           const FastExportLatch* latch) {
  if (clipboard.fragments.empty()) {
    return ResultCode::Ok;
  }
  std::vector<std::string> ids;
  ids.reserve(clipboard.fragments.size());
  for (const auto& fragment : clipboard.fragments) {
    if (fragment.capability_id.empty()) {
      return ResultCode::StateConflict;
    }
    ids.push_back(fragment.capability_id);
  }
  WriteScope scope(files, path, latch);
  if (!scope) return scope.code();
  return target.commit_fragment_replacement(expected_revision, ids, clipboard.fragments, files, path);
}

}  // namespace pixcake
