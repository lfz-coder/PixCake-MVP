#pragma once

#include "pixcake/cloud/cloud_types.hpp"

#include <string>

namespace pixcake::cloud {

// The gateway is the only protocol boundary known by CloudService.  A real
// implementation may use services/net, while tests can provide a fake.  No
// provider-specific wire type belongs in this interface.
// Synchronous seam; a future UI must dispatch it outside its event thread.
// Implementations must enforce the supplied timeout across all I/O/pagination,
// return NetworkFailed on timeout/unusable responses and PermissionDenied for
// rejected authorization. Calls must not throw for ordinary remote failures.
class CloudGateway {
 public:
  virtual ~CloudGateway() = default;

  virtual CloudSpacesReply list_spaces(const CloudIdentity& identity,
                                       std::chrono::milliseconds timeout) = 0;
  virtual CloudProjectsReply list_projects(const CloudIdentity& identity,
                                           const std::string& space_id,
                                           std::chrono::milliseconds timeout) = 0;
};

}  // namespace pixcake::cloud
