#pragma once

#include "pixcake/generated/http_remote_tool.hpp"

#include <chrono>
#include <cstdint>
#include <utility>
#include <vector>

namespace pixcake::generated {

// A deliberately non-official, deterministic wire fixture.  It exists only
// to exercise the complete adapter path before the official provider schema is
// available.  It must never be treated as PixCake's production API.
class FixtureWireCodec final : public GenerationWireCodec {
 public:
  ResultCode encode_request(const generate::GenerationTask& task,
                            net::HttpRequest& out) const override;
  ResultCode decode_response(const net::HttpResponse& response,
                             RemoteReply& out) const override;

  static std::vector<std::uint8_t> encode_response(const RemoteReply& reply,
                                                    int status_code = 200);
  // Reads only the provider-neutral output count from a fixture request.  It
  // is used by the local transport so a multi-output task exercises the same
  // result-list boundary as a real adapter.
  static bool request_output_count(const net::HttpRequest& request, std::uint32_t& out);
};

// Development-only transport paired with FixtureWireCodec.  It deliberately
// has no network side effects; the official provider transport will replace it
// once the real protocol is available.
class FixtureHttpTransport final : public net::HttpTransport {
 public:
  explicit FixtureHttpTransport(RemoteReply response);

  net::HttpResponse send(const net::HttpRequest& request,
                         std::chrono::milliseconds timeout) override;

 private:
  RemoteReply response_;
};

}  // namespace pixcake::generated
