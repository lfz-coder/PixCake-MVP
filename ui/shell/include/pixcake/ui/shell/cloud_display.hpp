#pragma once

#include "pixcake/cloud/cloud_types.hpp"

namespace pixcake::ui::shell {
enum class CloudState { NotConfigured, SignedOut, Idle, Loading, Ready, Failed, Denied };
struct CloudView {
  CloudState state = CloudState::NotConfigured;
  ResultCode code = ResultCode::Ok;
  bool busy = false;
  bool has_snapshot = false;
  std::vector<cloud::CloudSpace> spaces;
  std::string selected_space;
  std::vector<cloud::CloudProjectSummary> projects;
};
const std::string& cloud_status_text(const CloudView& view);
}  // namespace pixcake::ui::shell
