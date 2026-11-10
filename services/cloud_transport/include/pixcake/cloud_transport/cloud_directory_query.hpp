#pragma once

#include "pixcake/cloud_transport/backend_v1_codec.hpp"

namespace pixcake::cloud_transport {

struct CloudDirectoryReply {
  ResultCode code = ResultCode::Ok;
  cloud::CloudAccount account;
  std::vector<cloud::CloudSpace> spaces;
  std::string selected_space;
  std::vector<cloud::CloudProjectSummary> projects;
};

// Immutable identity in, one complete directory out. Empty account_id is only
// for explicitly restoring a credential; the server supplies the identity.
// Creates its own serialized sessions, never accesses the UI's AccountSession.
CloudDirectoryReply query_directory(net::HttpTransport& transport, const BackendV1Codec& codec,
                                     const cloud::CloudIdentity& identity, const std::string& space_id,
                                     std::chrono::milliseconds timeout);

}  // namespace pixcake::cloud_transport
