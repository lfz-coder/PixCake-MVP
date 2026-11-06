#include "pixcake/sync/sync_batch.hpp"

#include "pixcake/capabilities/module_set.hpp"
#include "pixcake/capability/capability_sync.hpp"
#include "pixcake/mask/mask_sync.hpp"
#include "pixcake/persist/write_lock.hpp"

#include <cstddef>
#include <map>
#include <optional>
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

std::optional<FragmentBytes> bytes_of(const std::optional<FragmentView>& view) {
  if (!view) return std::nullopt;
  return FragmentBytes{view->version, view->inner};
}

std::optional<FragmentBytes> bytes_of(const FragmentView& view) { return FragmentBytes{view.version, view.inner}; }

ResultCode apply_one(const EditDocument& source, const std::string& source_path,
                     const std::vector<std::string>& capability_ids, const SyncTarget& target) {
  if (target.document == nullptr || target.files == nullptr || target.path.empty() ||
      target.path == source_path) {
    return ResultCode::StateConflict;
  }
  std::vector<FragmentView> replacements;
  std::vector<std::string> removable_ids;
  for (const auto& id : capability_ids) {
    // 经能力模块写入（架构 7.2）：目标片段比本能力新时保留；来源迁移、提取、取值校验，不通过则整项失败。
    const FragmentTransfer moved = transfer_fragment(id, bytes_of(source.fragment(id)), bytes_of(target.document->fragment(id)));
    if (moved.code != ResultCode::Ok) return moved.code;
    if (moved.keep_target) continue;
    removable_ids.push_back(id);
    if (moved.fragment) replacements.push_back(FragmentView{id, moved.fragment->version, moved.fragment->inner});
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
  std::vector<FragmentView> replacements;
  ids.reserve(clipboard.fragments.size());
  for (const auto& fragment : clipboard.fragments) {
    if (fragment.capability_id.empty()) {
      return ResultCode::StateConflict;
    }
    const FragmentTransfer moved = transfer_fragment(fragment.capability_id, bytes_of(fragment),
                                                     bytes_of(target.fragment(fragment.capability_id)));
    if (moved.code != ResultCode::Ok) return moved.code;
    if (moved.keep_target) continue;
    ids.push_back(fragment.capability_id);
    if (moved.fragment) {
      replacements.push_back(FragmentView{fragment.capability_id, moved.fragment->version, moved.fragment->inner});
    }
  }
  if (ids.empty()) return ResultCode::Ok;
  WriteScope scope(files, path, latch);
  if (!scope) return scope.code();
  return target.commit_fragment_replacement(expected_revision, ids, replacements, files, path);
}

}  // namespace pixcake
