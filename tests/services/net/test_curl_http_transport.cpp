#include "pixcake/net/curl_http_transport.hpp"

#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {
using pixcake::ResultCode;
using namespace pixcake::net;
using namespace std::chrono_literals;
int failures = 0;
void expect(bool condition, int line) { if (!condition) { std::fprintf(stderr, "FAIL line %d\n", line); ++failures; } }
#define EXPECT(value) expect(static_cast<bool>(value), __LINE__)
void rejected(CurlHttpTransport& transport, const HttpRequest& request, std::chrono::milliseconds timeout = 500ms) {
  const auto reply = transport.send(request, timeout);
  EXPECT(reply.transport_code == ResultCode::StateConflict);
  EXPECT(reply.status_code == 0 && reply.headers.empty() && reply.body.empty());
}
// Every case fails preflight. This test must never need a socket or DNS query.
void test_cld16_origin_validation() {
  CurlHttpTransport transport("https://cloud.invalid");
  for (const auto& url : {"", "http://cloud.invalid/a", "file:///tmp/a", "https://other.invalid/a", "https://cloud.invalid:8443/a",
                          "https://user@cloud.invalid/a", "https://cloud.invalid/a#", "https://cloud.invalid/a#secret", "https://cloud.invalid/a\nb"})
    rejected(transport, {"GET", url, {}, {}});
  for (const auto& origin : {"http://cloud.invalid", "https://cloud.invalid/api", "https://cloud.invalid?", "https://user@cloud.invalid", "https://cloud.invalid:0"}) {
    CurlHttpTransport bad(origin);
    rejected(bad, {"GET", "https://cloud.invalid/a", {}, {}});
  }
}
void test_cld16_cld17_request_boundaries() {
  CurlHttpTransport transport("https://cloud.invalid");
  const HttpRequest valid{"GET", "https://cloud.invalid/a", {}, {}};
  const std::vector<std::function<void(HttpRequest&)>> invalid = {
    [](auto& r) { r.follow_redirects = true; }, [](auto& r) { r.method = "PUT"; },
    [](auto& r) { r.body = {'x'}; }, [](auto& r) { r.method = "POST"; r.body.resize(8 * 1024 * 1024 + 1); },
    [](auto& r) { r.url += std::string(16 * 1024, 'x'); },
    [](auto& r) { r.headers = {{"Bad Header", "x"}}; }, [](auto& r) { r.headers = {{"X-Test", "a\r\nInjected: x"}}; },
    [](auto& r) { r.headers = {{"Authorization", "a"}, {"authorization", "b"}}; },
    [](auto& r) { r.headers = {{"X-Test", std::string(32 * 1024, 'x')}}; },
    [](auto& r) { r.headers.resize(129, {"X-Test", "a"}); },
    [](auto& r) { r.response_limits.body_bytes = 64 * 1024 * 1024 + 1; },
    [](auto& r) { r.response_limits.header_bytes = 64 * 1024 + 1; }, [](auto& r) { r.response_limits.header_count = 257; }
  };
  for (const auto& change : invalid) { auto request = valid; change(request); rejected(transport, request); }
  for (const auto& name : {"hOsT", "Content-Length", "Transfer-Encoding", "Connection", "Proxy-Authorization", "Accept-Encoding", "Expect", "Trailer", "TE", "Upgrade"}) {
    auto request = valid; request.headers = {{name, "x"}}; rejected(transport, request);
  }
  rejected(transport, valid, 0ms); rejected(transport, valid, -1ms); rejected(transport, valid, 60001ms);
}
}  // namespace
int main() { test_cld16_origin_validation(); test_cld16_cld17_request_boundaries(); return failures ? 1 : 0; }
