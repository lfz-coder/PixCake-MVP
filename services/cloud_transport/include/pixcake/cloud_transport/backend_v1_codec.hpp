#pragma once

#include "pixcake/cloud_transport/cloud_wire_codec.hpp"
#include "pixcake/account/auth_types.hpp"

#include <cstddef>
#include <string>

namespace pixcake::cloud_transport {

// This project's API v1. Optional PIXCAKE_ENABLE_CLOUD_HTTP build feature;
// no third-party types cross the public boundary. The matching transport must
// enforce HTTPS origin/TLS validation and receive limits before allocation.
class BackendV1Codec final : public CloudWireCodec {
 public:
  explicit BackendV1Codec(std::string https_origin, std::size_t page_size = 100);
  ResultCode encode_account(const std::string& token, net::HttpRequest& request) const;
  ResultCode decode_account(const net::HttpResponse& response, cloud::CloudAccount& account) const;
  ResultCode encode_auth(const account::AuthRequest& input, net::HttpRequest& request) const;
  account::AuthReply decode_auth(account::AuthOperation operation, const net::HttpResponse& response) const;
  ResultCode encode_spaces(const cloud::CloudIdentity& identity, const std::string& cursor,
                           net::HttpRequest& request) const override;
  ResultCode encode_projects(const cloud::CloudIdentity& identity, const std::string& space_id,
                             const std::string& cursor, net::HttpRequest& request) const override;
  ResultCode decode_spaces(const net::HttpResponse& response, SpacesPage& page) const override;
  ResultCode decode_projects(const net::HttpResponse& response, ProjectsPage& page) const override;

 private:
  ResultCode encode(const cloud::CloudIdentity& identity, const std::string& path,
                     const std::string& cursor, net::HttpRequest& request) const;
  std::string origin_;
  std::size_t page_size_;
};

}  // namespace pixcake::cloud_transport
