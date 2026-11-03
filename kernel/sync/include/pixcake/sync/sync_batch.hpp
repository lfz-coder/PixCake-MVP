#pragma once

#include "pixcake/editdoc/edit_document.hpp"
#include "pixcake/persist/file_system.hpp"
#include "pixcake/support/result_code.hpp"

#include <cstdint>
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

struct SyncTarget {
  EditDocument* document = nullptr;
  std::uint64_t expected_revision = 0;
  FileSystem* files = nullptr;
  std::string path;
};

struct SyncItemResult {
  std::string path;
  ResultCode code = ResultCode::Ok;
};

struct EffectClipboard {
  std::vector<FragmentView> fragments;
};

std::vector<SyncItem> sync_tree();
std::vector<SyncItemResult> apply_checked(const EditDocument& source, const std::string& source_path,
                                         const std::vector<std::string>& checked_ids,
                                         const std::vector<SyncTarget>& targets);
std::vector<SyncItemResult> retry_failed(const EditDocument& source, const std::string& source_path,
                                        const std::vector<std::string>& checked_ids,
                                        const std::vector<SyncTarget>& targets,
                                        const std::vector<SyncItemResult>& previous);
EffectClipboard copy_committed(const EditDocument& source);
ResultCode paste_committed(EditDocument& target, const EffectClipboard& clipboard,
                           std::uint64_t expected_revision, FileSystem& files, const std::string& path);

}  // namespace pixcake
