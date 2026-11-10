#pragma once

#include "pixcake/cloud/cloud_types.hpp"
#include "pixcake/net/http_transport.hpp"

#include <string>
#include <vector>

namespace pixcake::cloud_transport {

struct SpacesPage {
  std::vector<cloud::CloudSpace> items;
  std::string next_cursor;
};

struct ProjectsPage {
  std::vector<cloud::CloudProjectSummary> items;
  std::string next_cursor;
};

// A provider codec owns the trusted endpoint, authentication and wire format.
// Cursors are opaque data, never URLs to follow. Empty means first/last page.
// Decoders must bound allocations and reject malformed/unknown responses;
// CloudService performs the final directory semantics validation. Ordinary
// remote/protocol failures return ResultCode rather than throwing exceptions.
// BackendV1Codec is the optional adapter for this project's documented API v1.
class CloudWireCodec {
 public:
  virtual ~CloudWireCodec() = default;

  virtual ResultCode encode_spaces(const cloud::CloudIdentity& identity,
                                   const std::string& cursor,
                                   net::HttpRequest& request) const = 0;
  virtual ResultCode encode_projects(const cloud::CloudIdentity& identity,
                                     const std::string& space_id,
                                     const std::string& cursor,
                                     net::HttpRequest& request) const = 0;
  virtual ResultCode decode_spaces(const net::HttpResponse& response,
                                   SpacesPage& page) const = 0;
  virtual ResultCode decode_projects(const net::HttpResponse& response,
                                     ProjectsPage& page) const = 0;
};

}  // namespace pixcake::cloud_transport
