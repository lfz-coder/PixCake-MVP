#include "pixcake/cloud_transport/http_cloud_gateway.hpp"
#include "pixcake/cloud/cloud_service.hpp"

#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace {
using pixcake::ResultCode;
using namespace pixcake::cloud;
using namespace pixcake::cloud_transport;
using namespace std::chrono_literals;
namespace net = pixcake::net;
int failures = 0;
void expect(bool condition, const char* expression, int line) {
  if (!condition) { std::fprintf(stderr, "FAIL line %d: %s\n", line, expression); ++failures; }
}
#define EXPECT(value) expect(static_cast<bool>(value), #value, __LINE__)

CloudIdentity identity() { return {"fixture-account", "fixture-token-not-real"}; }
CloudSpace space(std::string id = "personal") { return {std::move(id), "测试空间", SpaceKind::Personal, {Permission::Read}}; }
CloudProjectSummary project(std::string id = "project") { return {std::move(id), "personal", "测试项目", 1, 2, 3}; }
net::HttpResponse ok(std::size_t size = 1) { return {ResultCode::Ok, 200, {}, std::vector<std::uint8_t>(size, 'x')}; }

class FakeClock final : public RequestClock {
 public:
  std::chrono::steady_clock::time_point current{};
  std::chrono::steady_clock::time_point now() const override { return current; }
};

class FakeTransport final : public net::HttpTransport {
 public:
  std::vector<net::HttpResponse> responses{ok()};
  std::vector<net::HttpRequest> requests;
  std::vector<std::chrono::milliseconds> timeouts;
  std::function<void()> on_send;
  net::HttpResponse send(const net::HttpRequest& request, std::chrono::milliseconds timeout) override {
    const auto index = requests.size();
    requests.push_back(request);
    timeouts.push_back(timeout);
    if (on_send) on_send();
    if (index >= responses.size()) return {ResultCode::NetworkFailed, 0, {}, {}};
    return responses[index];
  }
};

// This fixture supplies typed pages. It does not implement or claim a real wire format.
class FakeCodec final : public CloudWireCodec {
 public:
  net::HttpRequest request{"POST", "https://cloud.invalid/directory", {}, {}};
  ResultCode encode_code = ResultCode::Ok;
  ResultCode decode_code = ResultCode::Ok;
  std::vector<SpacesPage> space_pages{{{space()}, {}}};
  std::vector<ProjectsPage> project_pages{{{project()}, {}}};
  mutable std::size_t space_index = 0;
  mutable std::size_t project_index = 0;
  mutable int encodes = 0;
  mutable int decodes = 0;
  mutable CloudIdentity last_identity;
  mutable std::string last_space;
  mutable std::vector<std::string> cursors;
  std::function<void()> on_encode;
  std::function<void()> on_decode;

  ResultCode encode(const CloudIdentity& who, const std::string& cursor, net::HttpRequest& out) const {
    ++encodes; last_identity = who; cursors.push_back(cursor);
    out = request;
    out.headers.push_back({"X-Fixture-Session", who.session_handle});
    if (on_encode) on_encode();
    return encode_code;
  }
  ResultCode encode_spaces(const CloudIdentity& who, const std::string& cursor, net::HttpRequest& out) const override {
    return encode(who, cursor, out);
  }
  ResultCode encode_projects(const CloudIdentity& who, const std::string& id, const std::string& cursor,
                             net::HttpRequest& out) const override {
    last_space = id;
    return encode(who, cursor, out);
  }
  ResultCode decode_spaces(const net::HttpResponse&, SpacesPage& out) const override {
    ++decodes;
    if (on_decode) on_decode();
    if (space_index >= space_pages.size()) return ResultCode::NetworkFailed;
    out = space_pages[space_index++];
    return decode_code;
  }
  ResultCode decode_projects(const net::HttpResponse&, ProjectsPage& out) const override {
    ++decodes;
    if (on_decode) on_decode();
    if (project_index >= project_pages.size()) return ResultCode::NetworkFailed;
    out = project_pages[project_index++];
    return decode_code;
  }
};

struct Fixture {
  FakeClock clock;
  FakeCodec codec;
  FakeTransport transport;
  HttpCloudGateway gateway;
  pixcake::account::AccountSession account;
  CloudSession session{account};
  CloudService service{session, gateway};
  explicit Fixture(CloudHttpLimits limits = {}) : gateway(transport, codec, clock, limits) {}
  void sign_in() {
    pixcake::account::AccountSnapshot snapshot;
    snapshot.account_id = identity().account_id; snapshot.session_handle = identity().session_handle;
    EXPECT(account.sign_in(snapshot) == ResultCode::Ok);
    EXPECT(session.open() == ResultCode::Ok);
  }
};

void test_cld09_cld10_pagination_routes_and_one_budget() {
  Fixture f;
  f.codec.space_pages = {{{space()}, "next"}, {{space("team")}, {}}};
  f.codec.project_pages = {{{project("p1")}, "page2"}, {{project("p2")}, {}}};
  f.transport.responses = {ok(), ok(), ok(), ok()};
  f.codec.request.follow_redirects = true; // Gateway policy overrides codecs.
  f.codec.on_encode = [&] { f.clock.current += 10ms; };
  f.transport.on_send = [&] { f.clock.current += 100ms; };
  f.codec.on_decode = [&] { f.clock.current += 20ms; };
  f.sign_in();
  EXPECT(f.service.refresh_spaces(500ms) == ResultCode::Ok);
  EXPECT(f.session.spaces().size() == 2);
  const auto listed = f.service.list_projects("personal", 500ms);
  EXPECT(listed.code == ResultCode::Ok && listed.projects.size() == 2);
  EXPECT(f.codec.last_identity.session_handle == identity().session_handle);
  EXPECT(f.codec.last_space == "personal");
  EXPECT((f.codec.cursors == std::vector<std::string>{"", "next", "", "page2"}));
  EXPECT((f.transport.timeouts == std::vector<std::chrono::milliseconds>{490ms, 360ms, 490ms, 360ms}));
  for (const auto& request : f.transport.requests) {
    EXPECT(!request.follow_redirects);
    EXPECT(request.response_limits.body_bytes == CloudHttpLimits{}.max_page_body_bytes);
    EXPECT(request.response_limits.header_bytes == CloudHttpLimits{}.max_header_bytes);
    EXPECT(request.response_limits.header_count == CloudHttpLimits{}.max_headers);
  }
}

void test_cld12_status_and_transport_failures_never_decode() {
  for (int status : {0, 199, 301, 302, 401, 403, 404, 429, 500}) {
    Fixture f;
    f.transport.responses.front().status_code = status;
    const auto reply = f.gateway.list_spaces(identity(), 500ms);
    EXPECT(reply.code == ((status == 401 || status == 403) ? ResultCode::PermissionDenied : ResultCode::NetworkFailed));
    EXPECT(reply.spaces.empty());
    EXPECT(f.codec.decodes == 0);
  }
  for (auto error : {ResultCode::NetworkFailed, ResultCode::Cancelled, ResultCode::IoFailed}) {
    Fixture f;
    f.transport.responses.front().transport_code = error;
    const auto reply = f.gateway.list_projects(identity(), "personal", 500ms);
    EXPECT(reply.code == error && reply.projects.empty());
    EXPECT(f.codec.decodes == 0);
  }
}

void test_cld13_encode_rejection_and_cld12_decode_partial_output() {
  Fixture f;
  f.codec.encode_code = ResultCode::StateConflict;
  EXPECT(f.gateway.list_spaces(identity(), 500ms).code == ResultCode::StateConflict);
  EXPECT(f.transport.requests.empty());
  for (auto error : {ResultCode::NetworkFailed, ResultCode::PermissionDenied, ResultCode::Cancelled}) {
    Fixture bad;
    bad.codec.decode_code = error; // Fixture still fills the output with a project.
    const auto reply = bad.gateway.list_projects(identity(), "personal", 500ms);
    EXPECT(reply.code == error && reply.projects.empty());
  }
  for (const auto& url : {std::string(), std::string("http://cloud.invalid"), std::string("https://bad\nurl")}) {
    Fixture bad;
    bad.codec.request.url = url;
    EXPECT(bad.gateway.list_spaces(identity(), 500ms).code == ResultCode::StateConflict);
    EXPECT(bad.transport.requests.empty());
  }
  Fixture method;
  method.codec.request.method = "DELETE";
  EXPECT(method.gateway.list_spaces(identity(), 500ms).code == ResultCode::StateConflict);
  EXPECT(method.transport.requests.empty());
  Fixture header;
  header.codec.request.headers = {{"X-Unsafe", "value\r\nInjected: header"}};
  EXPECT(header.gateway.list_spaces(identity(), 500ms).code == ResultCode::StateConflict);
  EXPECT(header.transport.requests.empty());
}

void test_cld03_second_page_failure_retains_directory() {
  Fixture f;
  f.sign_in();
  EXPECT(f.service.refresh_spaces(500ms) == ResultCode::Ok);
  f.codec.space_pages.push_back({{space("team")}, "next"});
  f.transport.responses.push_back(ok());
  f.transport.responses.push_back({ResultCode::NetworkFailed, 0, {}, {}});
  EXPECT(f.service.refresh_spaces(500ms) == ResultCode::NetworkFailed);
  const auto kept = f.session.spaces();
  EXPECT(kept.size() == 1 && kept.front().id == "personal");
  EXPECT(f.transport.requests.size() == 3);
}

void test_cld09_cursor_cycles_page_count_and_empty_page() {
  for (bool cycle : {true, false}) {
    CloudHttpLimits limits; limits.max_pages = 2;
    Fixture f(limits);
    f.codec.space_pages = {{{space()}, "a"}, {{space("team")}, cycle ? "a" : "b"}};
    f.transport.responses = {ok(), ok()};
    const auto reply = f.gateway.list_spaces(identity(), 500ms);
    EXPECT(reply.code == ResultCode::NetworkFailed && reply.spaces.empty());
    EXPECT(f.transport.requests.size() == 2);
  }
  CloudHttpLimits limits; limits.max_cursor_bytes = 3;
  Fixture long_cursor(limits);
  long_cursor.codec.space_pages.front().next_cursor = "abcd";
  const auto failed = long_cursor.gateway.list_spaces(identity(), 500ms);
  EXPECT(failed.code == ResultCode::NetworkFailed && failed.spaces.empty());
  EXPECT(long_cursor.transport.requests.size() == 1);
  Fixture empty;
  empty.codec.space_pages = {{ {}, "next" }, { {}, {} }};
  empty.transport.responses = {ok(), ok()};
  const auto reply = empty.gateway.list_spaces(identity(), 500ms);
  EXPECT(reply.code == ResultCode::Ok && reply.spaces.empty());
}

void test_cld10_deadlines_include_encoding_transport_and_final_decode() {
  for (int phase = 0; phase < 3; ++phase) {
    Fixture f;
    const auto advance = [&] { f.clock.current += 500ms; };
    if (phase == 0) f.codec.on_encode = advance;
    if (phase == 1) f.transport.on_send = advance;
    if (phase == 2) f.codec.on_decode = advance;
    const auto reply = f.gateway.list_spaces(identity(), 500ms);
    EXPECT(reply.code == ResultCode::NetworkFailed && reply.spaces.empty());
    EXPECT(f.transport.requests.size() == (phase == 0 ? 0U : 1U));
  }
  Fixture backwards;
  backwards.transport.on_send = [&] { backwards.clock.current -= 1ms; };
  EXPECT(backwards.gateway.list_spaces(identity(), 500ms).code == ResultCode::NetworkFailed);
  Fixture invalid;
  for (auto timeout : {0ms, -1ms, kMaxCloudRequestTimeout + 1ms}) {
    EXPECT(invalid.gateway.list_spaces(identity(), timeout).code == ResultCode::StateConflict);
  }
  EXPECT(invalid.codec.encodes == 0 && invalid.transport.requests.empty());
  auto bad_identity = identity(); bad_identity.session_handle.clear();
  EXPECT(invalid.gateway.list_spaces(bad_identity, 500ms).code == ResultCode::StateConflict);
  EXPECT(invalid.gateway.list_projects(identity(), "", 500ms).code == ResultCode::StateConflict);
  EXPECT(invalid.transport.requests.empty());
}

void test_cld11_raw_and_cumulative_budgets() {
  CloudHttpLimits limits;
  limits.max_page_body_bytes = 4; limits.max_total_body_bytes = 6;
  for (std::size_t second : {2U, 3U}) {
    Fixture f(limits);
    f.codec.space_pages = {{{space()}, "next"}, {{space("team")}, {}}};
    f.transport.responses = {ok(4), ok(second)};
    const auto reply = f.gateway.list_spaces(identity(), 500ms);
    EXPECT(reply.code == (second == 2 ? ResultCode::Ok : ResultCode::NetworkFailed));
    EXPECT(reply.spaces.size() == (second == 2 ? 2U : 0U));
    EXPECT(f.codec.decodes == (second == 2 ? 2 : 1));
    EXPECT(f.transport.requests.size() == 2 && f.transport.requests.back().response_limits.body_bytes == 2);
  }
  Fixture page(limits);
  page.transport.responses = {ok(5)};
  EXPECT(page.gateway.list_spaces(identity(), 500ms).code == ResultCode::NetworkFailed);
  EXPECT(page.codec.decodes == 0);
  limits.max_header_bytes = 4; limits.max_headers = 1;
  for (bool count : {true, false}) {
    Fixture f(limits);
    f.transport.responses.front().headers = count ? std::vector<net::Header>{{"", ""}, {"", ""}}
                                                  : std::vector<net::Header>{{"key", "xx"}};
    EXPECT(f.gateway.list_spaces(identity(), 500ms).code == ResultCode::NetworkFailed);
    EXPECT(f.codec.decodes == 0);
  }
  limits.max_request_body_bytes = 4;
  Fixture request(limits);
  request.codec.request.body.resize(5);
  EXPECT(request.gateway.list_spaces(identity(), 500ms).code == ResultCode::StateConflict);
  EXPECT(request.transport.requests.empty());
  limits.max_pages = 0;
  Fixture invalid(limits);
  EXPECT(invalid.gateway.list_spaces(identity(), 500ms).code == ResultCode::StateConflict);
  EXPECT(invalid.transport.requests.empty());
}

void test_cld11_decoded_total_is_bounded() {
  Fixture f;
  f.codec.space_pages = {{std::vector<CloudSpace>(kMaxCloudSpaces, space()), "next"}, {{space()}, {}}};
  f.transport.responses = {ok(), ok()};
  const auto spaces = f.gateway.list_spaces(identity(), 500ms);
  EXPECT(spaces.code == ResultCode::NetworkFailed && spaces.spaces.empty());
  Fixture p;
  p.codec.project_pages = {{std::vector<CloudProjectSummary>(kMaxCloudProjects + 1, project()), {}}};
  const auto projects = p.gateway.list_projects(identity(), "personal", 500ms);
  EXPECT(projects.code == ResultCode::NetworkFailed && projects.projects.empty());
}

void test_cld02_cld05_integration_auth_stale_and_bad_semantics() {
  Fixture f;
  f.sign_in();
  EXPECT(f.service.refresh_spaces(500ms) == ResultCode::Ok);
  f.codec.space_pages.push_back({{space()}, "next"});
  f.codec.space_pages.push_back({{space()}, {}});
  f.transport.responses.push_back(ok()); f.transport.responses.push_back(ok());
  EXPECT(f.service.refresh_spaces(500ms) == ResultCode::NetworkFailed); // Cross-page duplicate.
  EXPECT(f.session.can_read("personal"));
  auto wrong = project(); wrong.space_id = "other";
  f.codec.project_pages = {{{wrong}, {}}}; f.transport.responses.push_back(ok());
  auto result = f.service.list_projects("personal", 500ms);
  EXPECT(result.code == ResultCode::NetworkFailed && result.projects.empty());
  f.transport.responses.push_back({ResultCode::Ok, 401, {}, {}});
  EXPECT(f.service.list_projects("personal", 500ms).code == ResultCode::PermissionDenied);
  EXPECT(f.session.spaces().empty());

  Fixture stale;
  stale.sign_in();
  stale.codec.on_decode = [&] { EXPECT(stale.account.sign_out() == ResultCode::Ok); };
  EXPECT(stale.service.refresh_spaces(500ms) == ResultCode::StateConflict);
  EXPECT(stale.session.spaces().empty());
}

void test_cld10_system_clock_constructor() {
  FakeCodec codec; FakeTransport transport;
  HttpCloudGateway gateway(transport, codec);
  EXPECT(gateway.list_spaces(identity(), 5000ms).code == ResultCode::Ok);
}
}  // namespace

int main() {
  test_cld09_cld10_pagination_routes_and_one_budget();
  test_cld12_status_and_transport_failures_never_decode();
  test_cld13_encode_rejection_and_cld12_decode_partial_output();
  test_cld03_second_page_failure_retains_directory();
  test_cld09_cursor_cycles_page_count_and_empty_page();
  test_cld10_deadlines_include_encoding_transport_and_final_decode();
  test_cld11_raw_and_cumulative_budgets();
  test_cld11_decoded_total_is_bounded();
  test_cld02_cld05_integration_auth_stale_and_bad_semantics();
  test_cld10_system_clock_constructor();
  return failures == 0 ? 0 : 1;
}
