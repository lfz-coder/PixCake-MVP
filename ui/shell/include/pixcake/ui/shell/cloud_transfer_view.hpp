#pragma once
#include "pixcake/cloud_sync/sync_engine.hpp"
namespace pixcake::ui::shell {
struct CloudTransferView {
  cloud_sync::Phase phase = cloud_sync::Phase::Idle;
  ResultCode code = ResultCode::Ok;
  bool configured = false;
  bool busy = false;
  bool pending = false;
  bool automatic_retry = false;
  std::uint64_t done = 0;
  std::uint64_t total = 0;
};
const std::string& cloud_transfer_text(const CloudTransferView& view);
}
