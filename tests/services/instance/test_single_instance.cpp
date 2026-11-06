#include "pixcake/instance/single_instance.hpp"
#include "support/temp_dir.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;

void expect(bool cond, const char* text, int line) {
  if (!cond) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, text);
    ++g_failures;
  }
}

#define EXPECT(cond) expect(static_cast<bool>(cond), #cond, __LINE__)

int child_status(pid_t child) {
  int status = 0;
  if (waitpid(child, &status, 0) != child || !WIFEXITED(status)) return -1;
  return WEXITSTATUS(status);
}

std::optional<std::string> wait_message(pixcake::instance::PrimaryInstance& primary) {
  for (int i = 0; i < 200; ++i) {
    if (auto message = primary.take_message()) return message;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return std::nullopt;
}

void test_b67_second_claim_sees_running_primary() {
  pixcake::test::TempDir dir;
  auto first = pixcake::instance::claim_instance(dir.path());
  EXPECT(first.code == pixcake::ResultCode::Ok && first.primary != nullptr && !first.another_running);
  const pid_t child = fork();
  if (child == 0) {
    auto second = pixcake::instance::claim_instance(dir.path());
    _exit(second.code == pixcake::ResultCode::Ok && second.another_running && second.primary == nullptr ? 0 : 1);
  }
  EXPECT(child_status(child) == 0);
}

void test_t21_second_launch_activates_primary() {
  pixcake::test::TempDir dir;
  auto first = pixcake::instance::claim_instance(dir.path());
  EXPECT(first.primary != nullptr);
  if (!first.primary) return;
  EXPECT(!first.primary->take_message().has_value());
  const pid_t child = fork();
  if (child == 0) {
    _exit(pixcake::instance::notify_primary(dir.path(), "activate") == pixcake::ResultCode::Ok ? 0 : 1);
  }
  EXPECT(wait_message(*first.primary) == std::optional<std::string>("activate"));
  EXPECT(child_status(child) == 0);
}

void test_s29_killed_primary_releases_lock() {
  pixcake::test::TempDir dir;
  const pid_t primary = fork();
  if (primary == 0) {
    auto claim = pixcake::instance::claim_instance(dir.path());
    _exit(claim.primary != nullptr ? 0 : 1);  // 不析构：锁随进程释放，套接字文件留下
  }
  EXPECT(child_status(primary) == 0);
  EXPECT(access((dir.path() + "/instance.sock").c_str(), F_OK) == 0);
  auto next = pixcake::instance::claim_instance(dir.path());
  EXPECT(next.code == pixcake::ResultCode::Ok && next.primary != nullptr);
  if (!next.primary) return;
  const pid_t child = fork();
  if (child == 0) {
    _exit(pixcake::instance::notify_primary(dir.path(), "activate") == pixcake::ResultCode::Ok ? 0 : 1);
  }
  EXPECT(wait_message(*next.primary) == std::optional<std::string>("activate"));
  EXPECT(child_status(child) == 0);
}

void test_s29_concurrent_claims_have_one_winner() {
  pixcake::test::TempDir dir;
  std::vector<pid_t> children;
  for (int i = 0; i < 8; ++i) {
    const pid_t child = fork();
    if (child == 0) {
      auto claim = pixcake::instance::claim_instance(dir.path());
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
      if (claim.code != pixcake::ResultCode::Ok) _exit(1);
      _exit(claim.primary != nullptr ? 10 : (claim.another_running ? 11 : 1));
    }
    children.push_back(child);
  }
  int winners = 0;
  int others = 0;
  for (const pid_t child : children) {
    const int status = child_status(child);
    winners += status == 10 ? 1 : 0;
    others += status == 11 ? 1 : 0;
  }
  EXPECT(winners == 1);
  EXPECT(others == 7);
}

void test_b67_long_socket_path_is_rejected() {
  pixcake::test::TempDir dir;
  const std::string deep = dir.path() + "/" + std::string(sizeof(sockaddr_un{}.sun_path), 'x');
  auto claim = pixcake::instance::claim_instance(deep);
  EXPECT(claim.code == pixcake::ResultCode::IoFailed);
  EXPECT(claim.primary == nullptr);
  EXPECT(access(deep.c_str(), F_OK) != 0);
}

}  // namespace

int main() {
  test_b67_second_claim_sees_running_primary();
  test_t21_second_launch_activates_primary();
  test_s29_killed_primary_releases_lock();
  test_s29_concurrent_claims_have_one_winner();
  test_b67_long_socket_path_is_rejected();
  return g_failures == 0 ? 0 : 1;
}
