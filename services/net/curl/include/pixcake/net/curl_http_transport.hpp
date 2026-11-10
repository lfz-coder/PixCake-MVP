#pragma once

#include "pixcake/net/http_transport.hpp"

#include <string>

namespace pixcake::net {

// Synchronous adapter, serialized use. Only requests to this HTTPS origin are
// accepted. An optional CA file changes trust roots, never peer/host checking.
// Ordinary failures return empty responses with an explicit transport code.
class CurlHttpTransport final : public HttpTransport {
 public:
  explicit CurlHttpTransport(std::string https_origin, std::string ca_file = {});
  HttpResponse send(const HttpRequest& request, std::chrono::milliseconds timeout) override;

 private:
  std::string origin_;
  std::string ca_file_;
};

}  // namespace pixcake::net
