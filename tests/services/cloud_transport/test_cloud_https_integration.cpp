#include "pixcake/cloud/cloud_service.hpp"
#include "pixcake/cloud_transport/backend_v1_codec.hpp"
#include "pixcake/cloud_transport/http_cloud_gateway.hpp"
#include "pixcake/net/curl_http_transport.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace {
using pixcake::ResultCode;
using namespace std::chrono_literals;
namespace net = pixcake::net;
namespace cloud = pixcake::cloud;
namespace wire = pixcake::cloud_transport;
int failures = 0;
void expect(bool condition, const char* expression, int line) {
  if (!condition) { std::fprintf(stderr, "FAIL line %d: %s\n", line, expression); ++failures; }
}
#define EXPECT(value) expect(static_cast<bool>(value), #value, __LINE__)
void empty_failure(const net::HttpResponse& reply) {
  EXPECT(reply.transport_code == ResultCode::NetworkFailed && reply.status_code == 0);
  EXPECT(reply.body.empty() && reply.headers.empty());
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 4) { std::fprintf(stderr, "Use the isolated smoke_cpp_https.py runner.\n"); return 2; }
  const std::string origin = argv[1], ca = argv[2];
  std::ifstream input(argv[3]);
  std::vector<std::string> manifest;
  for (std::string line; std::getline(input, line);) manifest.push_back(line);
  if (manifest.size() != 10) return 2;
  const cloud::CloudIdentity alice{manifest[0], manifest[1]}, bob{manifest[2], manifest[3]};
  const auto& team = manifest[4];
  const std::set<std::string> expected_projects(manifest.begin() + 7, manifest.end());
  net::CurlHttpTransport transport(origin, ca);
  wire::BackendV1Codec codec(origin, 1);
  wire::HttpCloudGateway gateway(transport, codec);
  pixcake::account::AccountSession account;
  pixcake::account::AccountSnapshot snapshot;
  snapshot.account_id = alice.account_id; snapshot.session_handle = alice.session_handle;
  EXPECT(account.sign_in(snapshot) == ResultCode::Ok);
  cloud::CloudSession session(account);
  cloud::CloudService service(session, gateway);
  EXPECT(session.open() == ResultCode::Ok);
  EXPECT(service.refresh_spaces(5000ms) == ResultCode::Ok);
  EXPECT(session.spaces().size() == 2);
  const auto projects = service.list_projects(team, 5000ms);
  EXPECT(projects.code == ResultCode::Ok && projects.projects.size() == 3);
  std::set<std::string> actual;
  for (const auto& item : projects.projects) {
    actual.insert(item.id);
    EXPECT(item.space_id == team && item.name.find("项目 ") == 0 && item.revision == 0 && item.size_bytes == 0);
  }
  EXPECT(actual == expected_projects);
  const auto bob_spaces = gateway.list_spaces(bob, 5000ms);
  EXPECT(bob_spaces.code == ResultCode::Ok && bob_spaces.spaces.size() == 1 && bob_spaces.spaces[0].id == manifest[6]);
  EXPECT(gateway.list_projects(bob, team, 5000ms).code == ResultCode::PermissionDenied);
  auto invalid = alice; invalid.session_handle.assign(43, 'x');
  EXPECT(gateway.list_spaces(invalid, 5000ms).code == ResultCode::PermissionDenied);

  net::HttpRequest health{"GET", origin + "/healthz", {}, {}};
  EXPECT(transport.send(health, 3000ms).status_code == 200);
  net::CurlHttpTransport untrusted(origin);
  empty_failure(untrusted.send(health, 3000ms));
  const auto wrong_host = std::string("https://localhost:") + origin.substr(origin.rfind(':') + 1);
  net::CurlHttpTransport mismatch(wrong_host, ca);
  empty_failure(mismatch.send({"GET", wrong_host + "/healthz", {}, {}}, 3000ms));

  auto request = health;
  request.url = origin + "/__fixture__/redirect";
  const auto redirect = transport.send(request, 3000ms);
  EXPECT(redirect.transport_code == ResultCode::Ok && redirect.status_code == 302);
  request.url = origin + "/__fixture__/hits";
  const auto hits = transport.send(request, 3000ms);
  EXPECT(hits.status_code == 200 && std::string(hits.body.begin(), hits.body.end()) == "0");

  for (const auto& path : {"body", "gzip"}) {
    request = health; request.url = origin + "/__fixture__/" + path;
    EXPECT(transport.send(request, 3000ms).body.size() == 256 * 1024);
    request.response_limits.body_bytes = 1024;
    empty_failure(transport.send(request, 3000ms));
  }
  request = health; request.url = origin + "/__fixture__/headers";
  request.response_limits.header_count = 256;
  EXPECT(transport.send(request, 3000ms).status_code == 200);
  request.response_limits.header_count = 128;
  empty_failure(transport.send(request, 3000ms));
  request.response_limits = {0, 32, 256};
  empty_failure(transport.send(request, 3000ms));

  request = health; request.url = origin + "/__fixture__/slow";
  const auto began = std::chrono::steady_clock::now();
  empty_failure(transport.send(request, 75ms));
  EXPECT(std::chrono::steady_clock::now() - began < 1500ms);
  request = health; request.url = origin + "/__fixture__/bad-json";
  wire::SpacesPage bad_page{{}, "old"};
  EXPECT(codec.decode_spaces(transport.send(request, 3000ms), bad_page) == ResultCode::NetworkFailed);
  EXPECT(bad_page.items.empty() && bad_page.next_cursor.empty());

  const auto mode = [&](const std::string& value) {
    EXPECT(transport.send({"POST", origin + "/__fixture__/mode/" + value, {}, {}}, 3000ms).status_code == 204);
  };
  mode("offline");
  EXPECT(service.refresh_spaces(3000ms) == ResultCode::NetworkFailed && session.spaces().size() == 2);
  mode("slow-pages");
  const auto pagination_began = std::chrono::steady_clock::now();
  EXPECT(service.refresh_spaces(180ms) == ResultCode::NetworkFailed && session.spaces().size() == 2);
  EXPECT(std::chrono::steady_clock::now() - pagination_began < 1500ms);
  mode("normal");

  // Exercise real DELETE and server-side revocation; no test mutation endpoint.
  request = {"DELETE", origin + "/v1/session", {{"Authorization", "Bearer " + alice.session_handle}}, {}};
  EXPECT(transport.send(request, 3000ms).status_code == 204);
  EXPECT(service.refresh_spaces(3000ms) == ResultCode::PermissionDenied && session.spaces().empty());
  if (!failures) std::puts("C++ HTTPS passed: service pagination, account isolation, revocation, verified TLS, redirect, deadlines and receive limits.");
  return failures ? 1 : 0;
}
