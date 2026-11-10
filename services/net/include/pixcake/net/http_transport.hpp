#pragma once

#include "pixcake/support/result_code.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace pixcake::net {

using Header = std::pair<std::string, std::string>;

// Zero means unspecified for existing callers. Implementations must enforce
// nonzero limits while receiving, including decompressed bytes, before growing
// response buffers. header_bytes counts the header names and values.
struct HttpResponseLimits {
  std::size_t body_bytes = 0;
  std::size_t header_bytes = 0;
  std::size_t header_count = 0;
};

// Provider-neutral HTTP request.  It deliberately carries bytes rather than
// a JSON type: the provider codec owns the wire format and can later use JSON,
// multipart, protobuf, or a vendor SDK without leaking that choice upward.
struct HttpRequest {
  std::string method;
  std::string url;
  std::vector<Header> headers;
  std::vector<std::uint8_t> body;
  HttpResponseLimits response_limits{};
  // A redirect must never forward credentials unless the caller and transport
  // explicitly permit it. Disabled requests return the original 3xx response.
  bool follow_redirects = false;
};

struct HttpResponse {
  // Transport failures are reported here.  For an HTTP response this remains
  // Ok even when status_code is 4xx/5xx; the codec maps provider errors.
  ResultCode transport_code = ResultCode::Ok;
  int status_code = 0;
  std::vector<Header> headers;
  std::vector<std::uint8_t> body;
};

class HttpTransport {
 public:
  virtual ~HttpTransport() = default;

  virtual HttpResponse send(const HttpRequest& request,
                            std::chrono::milliseconds timeout) = 0;
};

}  // namespace pixcake::net
