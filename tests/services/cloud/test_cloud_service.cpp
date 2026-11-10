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
using namespace std::chrono_literals;

int g_failures = 0;
void expect(bool condition, const char* expression, int line) {
  if (!condition) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, expression);
    ++g_failures;
  }
}
#define EXPECT(condition) expect(static_cast<bool>(condition), #condition, __LINE__)

pixcake::account::AccountSnapshot account_snapshot() {
  pixcake::account::AccountSnapshot account;
  account.account_id = "fixture-account-1";
  account.session_handle = "fixture-session-not-a-real-token";
  return account;
}

CloudSpace personal_space() { return {"personal", "个人空间", SpaceKind::Personal, {Permission::Read, Permission::Write}}; }
CloudSpace team_space() { return {"team", "团队空间", SpaceKind::Team, {Permission::Read}}; }
CloudProjectSummary project() { return {"project-1", "personal", "旅拍", 4, 100, 2048}; }

class FakeGateway final : public CloudGateway {
 public:
  CloudSpacesReply spaces_reply{ResultCode::Ok, {personal_space(), team_space()}};
  CloudProjectsReply projects_reply{ResultCode::Ok, {project()}};
  CloudIdentity last_identity;
  std::string last_space;
  std::chrono::milliseconds last_timeout{0};
  int list_spaces_calls = 0;
  int list_projects_calls = 0;
  std::function<void()> on_spaces;
  std::function<void()> on_projects;

  CloudSpacesReply list_spaces(const CloudIdentity& identity, std::chrono::milliseconds timeout) override {
    last_identity = identity;
    last_timeout = timeout;
    ++list_spaces_calls;
    const auto reply = spaces_reply;
    if (on_spaces) { auto callback = std::exchange(on_spaces, {}); callback(); }
    return reply;
  }
  CloudProjectsReply list_projects(const CloudIdentity& identity, const std::string& space_id,
                                  std::chrono::milliseconds timeout) override {
    last_identity = identity;
    last_space = space_id;
    last_timeout = timeout;
    ++list_projects_calls;
    const auto reply = projects_reply;
    if (on_projects) { auto callback = std::exchange(on_projects, {}); callback(); }
    return reply;
  }
};

class MemoryLog final : public pixcake::LogSink {
 public:
  std::vector<pixcake::LogRecord> records;
  void write(const pixcake::LogRecord& record) override { records.push_back(record); }
};

struct Fixture {
  pixcake::account::AccountSession account;
  CloudSession session{account};
  FakeGateway gateway;
  MemoryLog log;
  CloudService service{session, gateway, &log};
  void open() {
    EXPECT(account.sign_in(account_snapshot()) == ResultCode::Ok);
    EXPECT(session.open() == ResultCode::Ok);
    EXPECT(service.refresh_spaces(500ms) == ResultCode::Ok);
  }
};

void test_cld01_s25_live_account_and_logout() {
  Fixture f;
  EXPECT(f.session.open() == ResultCode::StateConflict);
  EXPECT(f.service.refresh_spaces(500ms) == ResultCode::StateConflict);
  EXPECT(f.service.list_projects("personal", 500ms).code == ResultCode::StateConflict);
  EXPECT(f.gateway.list_spaces_calls == 0 && f.gateway.list_projects_calls == 0);
  f.open();
  EXPECT(f.gateway.last_identity.account_id == account_snapshot().account_id);
  EXPECT(f.gateway.last_identity.session_handle == account_snapshot().session_handle);
  EXPECT(f.session.can_read("personal"));
  EXPECT(f.account.sign_out() == ResultCode::Ok);
  EXPECT(!f.session.signed_in());
  EXPECT(f.session.spaces().empty());
  EXPECT(!f.session.can_read("personal"));
  EXPECT(f.service.list_projects("personal", 500ms).code == ResultCode::StateConflict);
  EXPECT(f.gateway.list_projects_calls == 0);
  // Same-account re-login with the same opaque handle must not resurrect permissions.
  EXPECT(f.account.sign_in(account_snapshot()) == ResultCode::Ok);
  EXPECT(!f.session.signed_in());
  EXPECT(f.session.open() == ResultCode::Ok);
  EXPECT(f.session.spaces().empty());
  EXPECT(f.service.list_projects("personal", 500ms).code == ResultCode::PermissionDenied);
  EXPECT(f.service.refresh_spaces(500ms) == ResultCode::Ok);
  EXPECT(f.session.close() == ResultCode::Ok);
  EXPECT(f.session.close() == ResultCode::StateConflict);
  EXPECT(f.session.spaces().empty());
  EXPECT(f.account.signed_in()); // Closing cloud does not log out the desktop account.
}

void test_cld02_permissions_are_explicit_and_revoked() {
  Fixture f;
  f.gateway.spaces_reply.spaces = {personal_space(), team_space(),
      {"hidden", "不可读", SpaceKind::Team, {}},
      {"manager", "仅管理", SpaceKind::Team, {Permission::Manage}},
      {"writer", "仅写入", SpaceKind::Team, {Permission::Write}}};
  f.open();
  EXPECT(f.session.can_read("personal") && f.session.can_write("personal"));
  EXPECT(!f.session.can_manage("personal"));
  EXPECT(f.session.can_read("team") && !f.session.can_write("team"));
  EXPECT(f.session.can_manage("manager") && !f.session.can_read("manager") && !f.session.can_write("manager"));
  EXPECT(f.session.can_write("writer") && !f.session.can_read("writer"));
  for (const auto* id : {"hidden", "manager", "writer", "unknown"}) {
    const auto denied = f.service.list_projects(id, 500ms);
    EXPECT(denied.code == ResultCode::PermissionDenied && denied.projects.empty());
  }
  EXPECT(f.gateway.list_projects_calls == 0);
  f.gateway.projects_reply.code = ResultCode::PermissionDenied;
  EXPECT(f.service.list_projects("personal", 500ms).code == ResultCode::PermissionDenied);
  EXPECT(!f.session.can_read("personal") && f.session.spaces().empty());
  EXPECT(f.service.list_projects("team", 500ms).code == ResultCode::PermissionDenied);
  EXPECT(f.gateway.list_projects_calls == 1);
  f.gateway.spaces_reply.code = ResultCode::Ok;
  EXPECT(f.service.refresh_spaces(500ms) == ResultCode::Ok);
  f.gateway.spaces_reply.code = ResultCode::PermissionDenied;
  EXPECT(f.service.refresh_spaces(500ms) == ResultCode::PermissionDenied);
  EXPECT(f.session.spaces().empty());
}

void test_cld03_c19_b50_remote_errors_and_atomic_directory() {
  Fixture f;
  f.open();
  const auto before = f.session.spaces();
  for (auto code : {ResultCode::NetworkFailed, ResultCode::Cancelled, ResultCode::IoFailed}) {
    f.gateway.spaces_reply = {code, {}};
    EXPECT(f.service.refresh_spaces(713ms) == code);
    EXPECT(f.gateway.last_timeout == 713ms);
    EXPECT(f.session.spaces().size() == before.size());
    EXPECT(f.session.can_read("team"));
    f.gateway.projects_reply.code = code;
    auto reply = f.service.list_projects("personal", 811ms);
    EXPECT(reply.code == code && reply.projects.empty());
    EXPECT(f.gateway.last_timeout == 811ms);
  }
  auto bad = personal_space();
  std::vector<CloudSpace> invalid;
  bad.id.clear(); invalid.push_back(bad);
  bad = personal_space(); bad.name.clear(); invalid.push_back(bad);
  bad = personal_space(); bad.id = std::string("id\0x", 4); invalid.push_back(bad);
  bad = personal_space(); bad.name = "line\nbreak"; invalid.push_back(bad);
  bad = personal_space(); bad.kind = static_cast<SpaceKind>(99); invalid.push_back(bad);
  bad = personal_space(); bad.permissions = {static_cast<Permission>(99)}; invalid.push_back(bad);
  bad = personal_space(); bad.permissions = {Permission::Read, Permission::Read}; invalid.push_back(bad);
  bad = personal_space(); bad.id.assign(kMaxCloudIdBytes + 1, 'a'); invalid.push_back(bad);
  bad = personal_space(); bad.name.assign(kMaxCloudNameBytes + 1, 'a'); invalid.push_back(bad);
  for (const auto& item : invalid) {
    f.gateway.spaces_reply = {ResultCode::Ok, {team_space(), item}};
    EXPECT(f.service.refresh_spaces(500ms) == ResultCode::NetworkFailed);
    EXPECT(f.session.spaces().size() == before.size());
    EXPECT(f.session.can_write("personal"));
  }
  f.gateway.spaces_reply = {ResultCode::Ok, {personal_space(), personal_space()}};
  EXPECT(f.service.refresh_spaces(500ms) == ResultCode::NetworkFailed);
  EXPECT(f.session.spaces().size() == before.size());
  f.gateway.spaces_reply = {ResultCode::Ok, {}};
  EXPECT(f.service.refresh_spaces(500ms) == ResultCode::Ok);
  EXPECT(f.session.spaces().empty());
}

void test_cld04_s06_projects_validate_entire_reply() {
  Fixture f;
  f.open();
  auto good = f.service.list_projects("personal", 500ms);
  EXPECT(good.code == ResultCode::Ok && good.projects.size() == 1);
  EXPECT(!good.projects.empty() && good.projects.front().revision == 4);
  EXPECT(f.gateway.last_space == "personal");
  std::vector<CloudProjectSummary> invalid;
  auto bad = project(); bad.id.clear(); invalid.push_back(bad);
  bad = project(); bad.id = std::string("id\0x", 4); invalid.push_back(bad);
  bad = project(); bad.space_id = "team"; invalid.push_back(bad);
  bad = project(); bad.name.clear(); invalid.push_back(bad);
  bad = project(); bad.revision = -1; invalid.push_back(bad);
  bad = project(); bad.updated_at = -1; invalid.push_back(bad);
  bad = project(); bad.size_bytes = -1; invalid.push_back(bad);
  bad = project(); bad.name = "invalid\rname"; invalid.push_back(bad);
  bad = project(); bad.id.assign(kMaxCloudIdBytes + 1, 'a'); invalid.push_back(bad);
  bad = project(); bad.name.assign(kMaxCloudNameBytes + 1, 'a'); invalid.push_back(bad);
  for (const auto& item : invalid) {
    auto valid = project(); valid.id = "valid-first";
    f.gateway.projects_reply = {ResultCode::Ok, {valid, item}};
    const auto reply = f.service.list_projects("personal", 500ms);
    EXPECT(reply.code == ResultCode::NetworkFailed && reply.projects.empty());
    EXPECT(f.session.can_read("personal"));
  }
  f.gateway.projects_reply = {ResultCode::Ok, {project(), project()}};
  const auto duplicate = f.service.list_projects("personal", 500ms);
  EXPECT(duplicate.code == ResultCode::NetworkFailed && duplicate.projects.empty());
  f.gateway.projects_reply = {ResultCode::Ok, {}};
  const auto empty = f.service.list_projects("personal", 500ms);
  EXPECT(empty.code == ResultCode::Ok && empty.projects.empty());
  auto empty_project = project();
  empty_project.revision = 0; empty_project.updated_at = 0; empty_project.size_bytes = 0;
  f.gateway.projects_reply.projects = {empty_project};
  EXPECT(f.service.list_projects("personal", 500ms).code == ResultCode::Ok);
}

void test_cld05_s25_stale_responses_and_newer_refresh() {
  Fixture f;
  f.open();
  f.gateway.on_spaces = [&] {
    EXPECT(f.account.sign_out() == ResultCode::Ok);
    EXPECT(f.account.sign_in(account_snapshot()) == ResultCode::Ok);
  };
  EXPECT(f.service.refresh_spaces(500ms) == ResultCode::StateConflict);
  EXPECT(f.session.spaces().empty());
  EXPECT(f.session.open() == ResultCode::Ok);
  EXPECT(f.service.refresh_spaces(500ms) == ResultCode::Ok);
  f.gateway.on_projects = [&] {
    auto another = account_snapshot(); another.account_id = "fixture-account-2";
    EXPECT(f.account.sign_in(another) == ResultCode::Ok);
    EXPECT(f.session.open() == ResultCode::Ok);
  };
  auto stale = f.service.list_projects("personal", 500ms);
  EXPECT(stale.code == ResultCode::StateConflict && stale.projects.empty());
  EXPECT(f.session.spaces().empty());
  EXPECT(f.service.refresh_spaces(500ms) == ResultCode::Ok);
  // A nested refresh stands in for serialized completion before an older reply.
  f.gateway.on_spaces = [&] {
    f.gateway.spaces_reply = {ResultCode::Ok, {team_space()}};
    EXPECT(f.service.refresh_spaces(500ms) == ResultCode::Ok);
  };
  EXPECT(f.service.refresh_spaces(500ms) == ResultCode::StateConflict);
  EXPECT(!f.session.can_read("personal") && f.session.can_read("team"));
  f.gateway.spaces_reply = {ResultCode::Ok, {personal_space()}};
  EXPECT(f.service.refresh_spaces(500ms) == ResultCode::Ok);
  f.gateway.on_projects = [&] {
    EXPECT(f.session.close() == ResultCode::Ok);
    EXPECT(f.session.open() == ResultCode::Ok);
  };
  stale = f.service.list_projects("personal", 500ms);
  EXPECT(stale.code == ResultCode::StateConflict && stale.projects.empty());
}

void test_cld06_s06_limits_and_timeout_contract() {
  Fixture f;
  f.open();
  const int before = f.gateway.list_spaces_calls;
  for (auto timeout : {0ms, -1ms, kMaxCloudRequestTimeout + 1ms}) {
    EXPECT(f.service.refresh_spaces(timeout) == ResultCode::StateConflict);
    EXPECT(f.service.list_projects("personal", timeout).code == ResultCode::StateConflict);
  }
  for (const auto& id : {std::string(), std::string("space\0x", 7), std::string(kMaxCloudIdBytes + 1, 'a')}) {
    EXPECT(f.service.list_projects(id, 500ms).code == ResultCode::StateConflict);
  }
  EXPECT(f.gateway.list_spaces_calls == before && f.gateway.list_projects_calls == 0);
  EXPECT(f.service.refresh_spaces(1ms) == ResultCode::Ok);
  EXPECT(f.service.refresh_spaces(kMaxCloudRequestTimeout) == ResultCode::Ok);
  f.gateway.spaces_reply.spaces.clear();
  for (std::size_t i = 0; i < kMaxCloudSpaces; ++i) {
    auto space = team_space(); space.id = std::to_string(i);
    f.gateway.spaces_reply.spaces.push_back(space);
  }
  EXPECT(f.service.refresh_spaces(500ms) == ResultCode::Ok);
  EXPECT(f.session.spaces().size() == kMaxCloudSpaces);
  f.gateway.spaces_reply.spaces.push_back(personal_space());
  EXPECT(f.service.refresh_spaces(500ms) == ResultCode::NetworkFailed);
  EXPECT(f.session.spaces().size() == kMaxCloudSpaces);
  auto max_space = personal_space();
  max_space.id.assign(kMaxCloudIdBytes, 'a'); max_space.name.assign(kMaxCloudNameBytes, 'b');
  f.gateway.spaces_reply.spaces = {personal_space(), max_space};
  EXPECT(f.service.refresh_spaces(500ms) == ResultCode::Ok);
  f.gateway.projects_reply.projects.clear();
  for (std::size_t i = 0; i < kMaxCloudProjects; ++i) {
    auto item = project(); item.id = std::to_string(i);
    f.gateway.projects_reply.projects.push_back(item);
  }
  auto max_item = project();
  max_item.id.assign(kMaxCloudIdBytes, 'c'); max_item.name.assign(kMaxCloudNameBytes, 'd');
  f.gateway.projects_reply.projects.back() = max_item;
  const auto at_limit = f.service.list_projects("personal", 500ms);
  EXPECT(at_limit.code == ResultCode::Ok && at_limit.projects.size() == kMaxCloudProjects);
  f.gateway.projects_reply.projects.push_back(project());
  const auto over = f.service.list_projects("personal", 500ms);
  EXPECT(over.code == ResultCode::NetworkFailed && over.projects.empty());
  auto bad_identity = account_snapshot();
  bad_identity.session_handle.assign(kMaxCloudSessionBytes + 1, 'x');
  EXPECT(f.account.sign_in(bad_identity) == ResultCode::Ok);
  EXPECT(f.session.open() == ResultCode::StateConflict);
  EXPECT(f.session.spaces().empty());
}

void test_cld07_o01_s22_logs_contain_only_outcomes() {
  Fixture f;
  f.open();
  f.gateway.spaces_reply.code = ResultCode::NetworkFailed;
  EXPECT(f.service.refresh_spaces(500ms) == ResultCode::NetworkFailed);
  EXPECT(f.service.list_projects("missing", 500ms).code == ResultCode::PermissionDenied);
  EXPECT(f.log.records.size() == 3);
  EXPECT(!f.log.records.empty() && f.log.records.back().code == ResultCode::PermissionDenied);
  for (const auto& record : f.log.records) {
    EXPECT(record.module == "cloud");
    EXPECT(record.event == "cloud.spaces.refresh" || record.event == "cloud.projects.list");
    EXPECT(record.message.empty() && record.path.empty() && record.document_id.empty() && record.task_id.empty());
    EXPECT(record.capability_id.empty() && record.phase.empty());
  }
}
}  // namespace

int main() {
  test_cld01_s25_live_account_and_logout();
  test_cld02_permissions_are_explicit_and_revoked();
  test_cld03_c19_b50_remote_errors_and_atomic_directory();
  test_cld04_s06_projects_validate_entire_reply();
  test_cld05_s25_stale_responses_and_newer_refresh();
  test_cld06_s06_limits_and_timeout_contract();
  test_cld07_o01_s22_logs_contain_only_outcomes();
  return g_failures == 0 ? 0 : 1;
}
