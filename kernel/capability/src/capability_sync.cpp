#include "pixcake/capability/capability_sync.hpp"

#include "pixcake/capability/capability_registry.hpp"

#include <utility>

namespace pixcake {
namespace {

}  // namespace

std::vector<CapabilitySyncRow> capability_sync_rows() {
  std::vector<CapabilitySyncRow> rows;
  const auto registry = CapabilityRegistry::phase_one();
  rows.reserve(registry.descriptors().size() + 2);
  for (const auto& descriptor : registry.descriptors()) {
    CapabilitySyncRow item;
    item.id = descriptor.id;
    item.module_label = descriptor.module_label;
    item.label = descriptor.label;
    item.capability_id = descriptor.id;
    item.enabled = descriptor.can_batch_sync;
    item.blocked_reason = descriptor.batch_sync_blocked_reason;
    rows.push_back(std::move(item));

    if (descriptor.id == "color.match") {
      CapabilitySyncRow local_match;
      local_match.id = "color.match.local";
      local_match.module_label = "AI 色彩";
      local_match.label = "局部追色";
      local_match.enabled = false;
      local_match.blocked_reason = "局部追色不能批量同步";
      rows.push_back(std::move(local_match));
    }

    if (descriptor.id == "color.post") {
      CapabilitySyncRow detail;
      detail.id = "detail";
      detail.module_label = "色彩";
      detail.label = "细节";
      detail.capability_id = "detail";
      detail.enabled = true;
      rows.push_back(std::move(detail));
    }
  }

  return rows;
}

}  // namespace pixcake
