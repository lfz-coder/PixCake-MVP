#include "pixcake/ui/shell/cloud_transfer_view.hpp"
#include "pixcake/ui/shell/page_copy.hpp"
namespace pixcake::ui::shell {
const std::string& cloud_transfer_text(const CloudTransferView& view) {
  using cloud_sync::Phase;
  if (view.phase == Phase::Conflict) return page_text("cloud.sync.conflict");
  if (view.phase == Phase::Failed) {
    if (view.code == ResultCode::PermissionDenied) return page_text("cloud.sync.denied");
    if (view.code == ResultCode::NetworkFailed)
      return page_text(view.automatic_retry ? "cloud.sync.offline" : "cloud.sync.offline-manual");
    if (view.code == ResultCode::SourceMissing) return page_text("cloud.sync.missing");
    if (view.code == ResultCode::DiskFull) return page_text("cloud.sync.full");
    return page_text("cloud.sync.failed");
  }
  switch (view.phase) {
    case Phase::Preparing: return page_text("cloud.sync.preparing");
    case Phase::Uploading: return page_text("cloud.sync.uploading");
    case Phase::Downloading: return page_text("cloud.sync.downloading");
    case Phase::Restoring: return page_text("cloud.sync.restoring");
    case Phase::Paused: return page_text(view.busy ? "cloud.sync.pausing" : "cloud.sync.paused");
    case Phase::Complete: return page_text("cloud.sync.complete");
    default: return page_text("cloud.sync.ready");
  }
}
}
