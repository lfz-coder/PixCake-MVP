#pragma once

#include "pixcake/cloud/cloud_gateway.hpp"
#include "pixcake/cloud/cloud_session.hpp"
#include "pixcake/support/log_sink.hpp"

namespace pixcake::cloud {

// Directory reads only: no local project mutations, transfers or UI work.
// Share the serialized owner of CloudSession; do not call concurrently.
class CloudService final {
 public:
  CloudService(CloudSession& session, CloudGateway& gateway, LogSink* log = nullptr);

  // Bad remote data -> NetworkFailed; stale login/directory -> StateConflict.
  // Network failures retain the last directory; PermissionDenied clears it.
  // Timeout must be positive and <= kMaxCloudRequestTimeout. The gateway owns
  // enforcement; this seam does not spawn threads to interrupt a blocking call.
  ResultCode refresh_spaces(std::chrono::milliseconds timeout);
  CloudProjectsReply list_projects(const std::string& space_id, std::chrono::milliseconds timeout);

 private:
  ResultCode record(const char* event, ResultCode code) const;
  CloudSession& session_;
  CloudGateway& gateway_;
  LogSink* log_;
};

}  // namespace pixcake::cloud
