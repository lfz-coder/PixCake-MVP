#include "shell_controller.hpp"
#include <algorithm>

namespace pixcake::shell {
ui::shell::CloudTransferView ShellController::cloud_transfer_view() const {
  return cloud_transfer_ ? cloud_transfer_->view() : ui::shell::CloudTransferView{};
}
void ShellController::request_cloud_upload(std::int64_t local, const std::string& space_id) {
  if (!cloud_transfer_ || !projects_ || cloud_transfer_view().busy || cloud_transfer_view().pending) return;
  const auto cloud = cloud_view();
  const auto space = std::find_if(cloud.spaces.begin(), cloud.spaces.end(), [&](const auto& row) { return row.id == space_id; });
  if (space == cloud.spaces.end() || std::find(space->permissions.begin(), space->permissions.end(), cloud::Permission::Write)
      == space->permissions.end()) return;
  cloud_transfer_->upload(local, space_id);
}
void ShellController::request_cloud_download(const std::string& project_id) {
  if (!cloud_transfer_ || !projects_) return;
  const auto cloud = cloud_view();
  const auto project = std::find_if(cloud.projects.begin(), cloud.projects.end(), [&](const auto& row) { return row.id == project_id; });
  if (project == cloud.projects.end() || project->revision <= 0) return;
  cloud_transfer_->download(project->id, project->space_id, project->revision);
}
void ShellController::request_cloud_transfer_retry(bool keep_both) { if (cloud_transfer_) cloud_transfer_->retry(keep_both); }
void ShellController::request_cloud_transfer_pause() { if (cloud_transfer_) cloud_transfer_->pause(); }
void ShellController::request_cloud_transfer_discard() { if (cloud_transfer_) cloud_transfer_->discard(); }
}
