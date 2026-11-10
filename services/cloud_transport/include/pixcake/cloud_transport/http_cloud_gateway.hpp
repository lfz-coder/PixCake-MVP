#pragma once

#include "pixcake/cloud/cloud_gateway.hpp"
#include "pixcake/cloud_transport/cloud_wire_codec.hpp"

#include <chrono>
#include <cstddef>

namespace pixcake::cloud_transport {

// Defaults are hard upper bounds; callers may only lower positive limits.
// They are client resource safeguards, not storage quotas or API page sizes.
struct CloudHttpLimits {
  std::size_t max_pages = 128;
  std::size_t max_cursor_bytes = 4096;
  std::size_t max_page_body_bytes = 2 * 1024 * 1024;
  std::size_t max_total_body_bytes = 64 * 1024 * 1024;
  std::size_t max_header_bytes = 32 * 1024;
  std::size_t max_headers = 128;
  std::size_t max_request_body_bytes = 64 * 1024;
};

class RequestClock {
 public:
  virtual ~RequestClock() = default;
  virtual std::chrono::steady_clock::time_point now() const = 0;
};

// Synchronous, serialized use only, like CloudService. Injected dependencies
// must outlive this gateway. One budget covers encoding, I/O and all pages;
// the transport remains responsible for interrupting blocking I/O on timeout.
class HttpCloudGateway final : public cloud::CloudGateway {
 public:
  HttpCloudGateway(net::HttpTransport& transport, const CloudWireCodec& codec);
  HttpCloudGateway(net::HttpTransport& transport, const CloudWireCodec& codec,
                   const RequestClock& clock, CloudHttpLimits limits = {});

  cloud::CloudSpacesReply list_spaces(const cloud::CloudIdentity& identity,
                                      std::chrono::milliseconds timeout) override;
  cloud::CloudProjectsReply list_projects(const cloud::CloudIdentity& identity,
                                          const std::string& space_id,
                                          std::chrono::milliseconds timeout) override;

 private:
  net::HttpTransport& transport_;
  const CloudWireCodec& codec_;
  const RequestClock& clock_;
  CloudHttpLimits limits_;
};

}  // namespace pixcake::cloud_transport
