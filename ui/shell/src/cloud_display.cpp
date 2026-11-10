#include "pixcake/ui/shell/cloud_display.hpp"
#include "pixcake/ui/shell/page_copy.hpp"

namespace pixcake::ui::shell {
const std::string& cloud_status_text(const CloudView& view) {
  switch (view.state) {
    case CloudState::NotConfigured: return page_text("cloud.unconfigured");
    case CloudState::SignedOut: return page_text("cloud.signed-out");
    case CloudState::Idle: return page_text("cloud.idle");
    case CloudState::Loading: return page_text("cloud.loading");
    case CloudState::Denied: return page_text("cloud.denied");
    case CloudState::Failed:
      return page_text(view.code == ResultCode::StateConflict ? "cloud.invalid-config" :
                       view.has_snapshot ? "cloud.failed-retained" : "cloud.failed");
    case CloudState::Ready:
      if (view.spaces.empty()) return page_text("cloud.empty-spaces");
      if (view.selected_space.empty()) return page_text("cloud.select-space");
      return page_text(view.projects.empty() ? "cloud.empty-projects" : "cloud.ready");
  }
  return page_text("cloud.failed");
}
}  // namespace pixcake::ui::shell
