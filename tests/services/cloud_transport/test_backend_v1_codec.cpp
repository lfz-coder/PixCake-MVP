#include "pixcake/cloud_transport/backend_v1_codec.hpp"
#include "pixcake/cloud_transport/cloud_directory_query.hpp"

#include <cstdio>
#include <string>
#include <vector>
#include <thread>

namespace {
using pixcake::ResultCode;
using namespace pixcake::cloud_transport;
namespace net = pixcake::net;
int failures = 0;
void expect(bool condition, const char* expression, int line) {
  if (!condition) { std::fprintf(stderr, "FAIL line %d: %s\n", line, expression); ++failures; }
}
#define EXPECT(value) expect(static_cast<bool>(value), #value, __LINE__)
const std::string sid = "sp_" + std::string(32, 'a');
const std::string pid = "prj_" + std::string(32, 'b');
pixcake::cloud::CloudIdentity identity() { return {"acc_" + std::string(32, 'c'), std::string(43, 'T')}; }
std::string space() {
  return "{\"id\":\"" + sid + "\",\"name\":\"中文空间\",\"kind\":\"team\",\"permissions\":[\"manage\",\"read\"]}";
}
std::string project() {
  return "{\"id\":\"" + pid + "\",\"space_id\":\"" + sid + "\",\"name\":\"修图项目\",\"revision\":0,\"updated_at\":123,\"size_bytes\":9223372036854775807}";
}
std::string envelope(const std::string& items, const std::string& cursor = "") {
  return "{\"api_version\":1,\"items\":[" + items + "],\"next_cursor\":\"" + cursor + "\"}";
}
std::string replace(std::string str, const std::string& from, const std::string& to) {
  const auto at = str.find(from);
  EXPECT(at != std::string::npos);
  if (at != std::string::npos) str.replace(at, from.size(), to);
  return str;
}
net::HttpResponse response(const std::string& body) {
  return {ResultCode::Ok, 200, {{"Content-Type", "application/json"}}, {body.begin(), body.end()}};
}
void bad_space(const std::string& body) {
  BackendV1Codec codec("https://cloud.invalid");
  SpacesPage page{{{sid, "old", pixcake::cloud::SpaceKind::Team, {}}}, "old"};
  EXPECT(codec.decode_spaces(response(body), page) == ResultCode::NetworkFailed);
  EXPECT(page.items.empty() && page.next_cursor.empty());
}
void bad_project(const std::string& body) {
  BackendV1Codec codec("https://cloud.invalid");
  ProjectsPage page{{{pid, sid, "old", 0, 0, 0}}, "old"};
  EXPECT(codec.decode_projects(response(body), page) == ResultCode::NetworkFailed);
  EXPECT(page.items.empty() && page.next_cursor.empty());
}
void test_cld14_requests() {
  BackendV1Codec codec("https://cloud.invalid:8443/", 1);
  net::HttpRequest request;
  EXPECT(codec.encode_spaces(identity(), "a.b_c-d", request) == ResultCode::Ok);
  EXPECT(request.method == "GET" && request.url == "https://cloud.invalid:8443/v1/spaces?limit=1&cursor=a.b_c-d");
  EXPECT(request.headers.size() == 2 && request.headers[0].second == "Bearer " + identity().session_handle);
  EXPECT(request.body.empty() && !request.follow_redirects);
  EXPECT(request.url.find(identity().account_id) == std::string::npos && request.url.find(identity().session_handle) == std::string::npos);
  EXPECT(codec.encode_projects(identity(), sid, "", request) == ResultCode::Ok);
  EXPECT(request.url == "https://cloud.invalid:8443/v1/spaces/" + sid + "/projects?limit=1");
  for (const auto& origin : {"", "http://cloud.invalid", "https://user@cloud.invalid", "https://cloud.invalid/api", "https://cloud.invalid?q", "https://cloud.invalid/#f"}) {
    BackendV1Codec bad(origin);
    EXPECT(bad.encode_spaces(identity(), "", request) == ResultCode::StateConflict);
    EXPECT(request.url.empty() && request.headers.empty());
  }
  for (std::size_t limit : {0, 101}) {
    BackendV1Codec bad("https://cloud.invalid", limit);
    EXPECT(bad.encode_spaces(identity(), "", request) == ResultCode::StateConflict);
  }
  for (const auto& cursor : {std::string("x&limit=100"), std::string("https://other.invalid"), std::string(4097, 'a'), std::string("\xff")}) {
    EXPECT(codec.encode_spaces(identity(), cursor, request) == ResultCode::StateConflict);
    EXPECT(request.url.empty() && request.headers.empty());
  }
  auto who = identity(); who.session_handle[10] = '\n';
  EXPECT(codec.encode_spaces(who, "", request) == ResultCode::StateConflict);
  who = identity(); who.account_id = "fake";
  EXPECT(codec.encode_spaces(who, "", request) == ResultCode::StateConflict);
  EXPECT(codec.encode_projects(identity(), "../account", "", request) == ResultCode::StateConflict);
  EXPECT(request.url.empty() && request.headers.empty());
}
void test_cld15_valid_and_empty_pages() {
  BackendV1Codec codec("https://cloud.invalid");
  SpacesPage spaces;
  auto wire = response(envelope(space(), "next.a_b-0"));
  wire.headers = {{"cOnTeNt-TyPe", " Application/JSON ; charset=utf-8"}};
  EXPECT(codec.decode_spaces(wire, spaces) == ResultCode::Ok);
  EXPECT(spaces.items.size() == 1 && spaces.next_cursor == "next.a_b-0");
  if (!spaces.items.empty()) {
    EXPECT(spaces.items[0].name == "中文空间" && spaces.items[0].kind == pixcake::cloud::SpaceKind::Team);
    EXPECT(spaces.items[0].permissions.size() == 2 && spaces.items[0].permissions[0] == pixcake::cloud::Permission::Manage);
  }
  ProjectsPage projects;
  EXPECT(codec.decode_projects(response(envelope(project())), projects) == ResultCode::Ok);
  EXPECT(projects.items.size() == 1 && projects.items[0].size_bytes == INT64_MAX);
  EXPECT(codec.decode_spaces(response(envelope("")), spaces) == ResultCode::Ok && spaces.items.empty());
  EXPECT(codec.decode_projects(response(envelope("")), projects) == ResultCode::Ok && projects.items.empty());
  std::string items;
  for (int i = 0; i < 100; ++i) { if (i) items += ','; items += project(); }
  // Duplicate IDs across items are intentionally CloudService's responsibility.
  EXPECT(codec.decode_projects(response(envelope(items)), projects) == ResultCode::Ok && projects.items.size() == 100);
  bad_project(envelope(items + "," + project()));
}
void test_cld15_http_and_envelope() {
  BackendV1Codec codec("https://cloud.invalid");
  for (int status : {0, 199, 201, 204, 302, 401, 403, 409, 410, 429, 500}) {
    auto wire = response(envelope(space())); wire.status_code = status;
    SpacesPage page{{}, "old"};
    EXPECT(codec.decode_spaces(wire, page) == ((status == 401 || status == 403) ? ResultCode::PermissionDenied : ResultCode::NetworkFailed));
    EXPECT(page.items.empty() && page.next_cursor.empty());
  }
  for (auto headers : {std::vector<net::Header>{}, std::vector<net::Header>{{"Content-Type", "text/json"}},
                      std::vector<net::Header>{{"Content-Type", "application/json"}, {"content-type", "application/json"}},
                      std::vector<net::Header>{{"Content-Type", "application/json"}, {"X-Large", std::string(32 * 1024, 'x')}},
                      std::vector<net::Header>(129, {"X-Too-Many", "x"})}) {
    auto wire = response(envelope(space())); wire.headers = headers;
    SpacesPage page;
    EXPECT(codec.decode_spaces(wire, page) == ResultCode::NetworkFailed);
  }
  for (const auto& bad : {"", "null", "[]", "{}", "{", "{\"api_version\":1,\"items\":[]}", "{\"api_version\":1,\"items\":{},\"next_cursor\":\"\"}"}) bad_space(bad);
  for (const auto& version : {"2", "true", "1.0", "1e0", "\"1\"", "null"})
    bad_space(replace(envelope(space()), "\"api_version\":1", std::string("\"api_version\":") + version));
  bad_space(envelope(space()) + " trailing");
  bad_space(replace(envelope(space()), "\"items\":", "\"api_version\":1,\"items\":"));
  bad_space(replace(envelope(space()), "\"items\":", "\"unknown\":0,\"items\":"));
  bad_space(envelope(space(), "https://other.invalid"));
  bad_space(std::string(2 * 1024 * 1024 + 1, ' '));
}
void test_cld15_fields_and_partial_output() {
  const auto valid = space();
  for (const auto& broken : {replace(valid, "\"team\"", "\"unknown\""), replace(valid, "\"read\"", "\"admin\""),
       replace(valid, "\"read\"", "\"manage\""), replace(valid, "\"manage\",\"read\"", "true"),
       replace(valid, sid, "sp_ABC"), replace(valid, "中文空间", ""), replace(valid, "中文空间", "bad\\u0000name"),
       replace(valid, "中文空间", std::string(1025, 'a')), replace(valid, "中文空间", std::string("\xc0\xaf")),
       replace(valid, "\"kind\":", "\"name\":\"duplicate\",\"kind\":"), replace(valid, "\"permissions\":", "\"extra\":0,\"permissions\":")})
    bad_space(envelope(valid + ',' + broken));
  for (const auto& integer : {"-1", "true", "1.0", "1e2", "\"1\"", "null", "9223372036854775808", "18446744073709551616"})
    for (const auto& field : {"\"revision\":0", "\"updated_at\":123", "\"size_bytes\":9223372036854775807"}) {
      const std::string key(field);
      bad_project(envelope(project() + ',' + replace(project(), key, key.substr(0, key.find(':') + 1) + integer)));
    }
  bad_project(envelope(replace(project(), sid, "../escape")));
}
void test_cld15_parser_budgets() {
  bad_space("{\"x\":" + std::string(9, '[') + "0" + std::string(9, ']') + "}");
  bad_space(envelope(space(), std::string(4097, 'a')));
  std::string keys = "{";
  for (int i = 0; i < 33; ++i) keys += (i ? "," : "") + std::string("\"k") + std::to_string(i) + "\":0";
  bad_space(keys + "}");
  bad_space("{\"" + std::string(65, 'k') + "\":0}");
  std::string row = "[";
  for (int i = 0; i < 100; ++i) row += i ? ",0" : "0";
  row += "]";
  std::string many = "[";
  for (int i = 0; i < 50; ++i) many += (i ? "," : "") + row;
  bad_space(many + "]");
}
void test_cbr01_verified_account_and_budget() {
  BackendV1Codec codec("https://cloud.invalid");
  const auto account_id = identity().account_id;
  const auto expires = std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::system_clock::now().time_since_epoch()).count() + 3600;
  const auto account_body = "{\"api_version\":1,\"account_id\":\"" + account_id +
                            "\",\"session_expires_at\":" + std::to_string(expires) + "}";
  pixcake::cloud::CloudAccount account;
  EXPECT(codec.decode_account(response(account_body), account) == ResultCode::Ok);
  EXPECT(account.account_id == account_id && account.session_expires_at == expires);
  for (const auto& body : {replace(account_body, account_id, "invalid"),
       replace(account_body, std::to_string(expires), "true"), replace(account_body, std::to_string(expires), "-1"),
       replace(account_body, "\"api_version\":1", "\"api_version\":1,\"api_version\":1"),
       replace(account_body, "\"api_version\":1", "\"api_version\":1,\"balance\":100")}) {
    EXPECT(codec.decode_account(response(body), account) == ResultCode::NetworkFailed && account.account_id.empty());
  }
  net::HttpRequest request;
  EXPECT(codec.encode_account(identity().session_handle, request) == ResultCode::Ok);
  EXPECT(request.url == "https://cloud.invalid/v1/account");
  EXPECT(codec.encode_account("bad", request) == ResultCode::StateConflict && request.url.empty());
  class Transport final : public net::HttpTransport {
   public:
    std::vector<net::HttpResponse> replies;
    std::vector<std::chrono::milliseconds> budgets;
    bool delay = false;
    net::HttpResponse send(const net::HttpRequest&, std::chrono::milliseconds budget) override {
      budgets.push_back(budget);
      if (delay) std::this_thread::sleep_for(std::chrono::milliseconds{20});
      if (budgets.size() > replies.size()) return {ResultCode::NetworkFailed, 0, {}, {}};
      return replies[budgets.size() - 1];
    }
  } transport;
  transport.replies = {response(account_body), response(envelope(space())), response(envelope(project()))};
  const auto good = query_directory(transport, codec, identity(), sid, std::chrono::milliseconds{500});
  EXPECT(good.code == ResultCode::Ok && good.account.account_id == account_id && good.projects.size() == 1);
  EXPECT(transport.budgets.size() == 3 && transport.budgets.back() <= transport.budgets.front());
  transport.budgets.clear();
  auto wrong = identity(); wrong.account_id = "acc_" + std::string(32, 'd');
  EXPECT(query_directory(transport, codec, wrong, "", std::chrono::milliseconds{500}).code == ResultCode::PermissionDenied);
  EXPECT(transport.budgets.size() == 1);
  transport.budgets.clear(); transport.replies[0] = response(replace(account_body, std::to_string(expires), "1"));
  EXPECT(query_directory(transport, codec, identity(), "", std::chrono::milliseconds{500}).code == ResultCode::PermissionDenied);
  transport.budgets.clear(); transport.replies[0] = response(account_body); transport.delay = true;
  const auto late = query_directory(transport, codec, identity(), "", std::chrono::milliseconds{5});
  EXPECT(late.code == ResultCode::NetworkFailed && late.spaces.empty() && transport.budgets.size() == 1);
}
}  // namespace
int main() {
  test_cld14_requests(); test_cld15_valid_and_empty_pages(); test_cld15_http_and_envelope();
  test_cld15_fields_and_partial_output(); test_cld15_parser_budgets();
  test_cbr01_verified_account_and_budget();
  return failures ? 1 : 0;
}
