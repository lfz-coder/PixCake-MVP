#pragma once

#include "pixcake/generated/remote_tool.hpp"
#include "pixcake/net/http_transport.hpp"

#include <chrono>
#include <string>

namespace pixcake::generated {

// The wire codec is the only component that knows a provider's request and
// response fields.  It is intentionally abstract until the official API
// contract is available; production code must not guess that contract.
class GenerationWireCodec {
 public:
  virtual ~GenerationWireCodec() = default;

  virtual ResultCode encode_request(const generate::GenerationTask& task,
                                    net::HttpRequest& out) const = 0;
  virtual ResultCode decode_response(const net::HttpResponse& response,
                                     RemoteReply& out) const = 0;
};

// Adapts a generic HTTP transport and provider codec to the generation
// process's stable RemoteTool interface.  It does not charge points, write
// result files, or place layers.
class HttpRemoteTool final : public RemoteTool {
 public:
  HttpRemoteTool(net::HttpTransport& transport, const GenerationWireCodec& codec,
                 std::chrono::milliseconds timeout);

  RemoteReply run(const generate::GenerationTask& task) override;

 private:
  net::HttpTransport& transport_;
  const GenerationWireCodec& codec_;
  std::chrono::milliseconds timeout_;
};

}  // namespace pixcake::generated
