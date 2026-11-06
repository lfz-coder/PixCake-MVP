#include "pixcake/platform/mac_source_access.hpp"
#include "support/temp_dir.hpp"

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
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

std::string real(const std::string& path) {
  char buffer[PATH_MAX];
  return ::realpath(path.c_str(), buffer) != nullptr ? std::string(buffer) : path;
}

void write_file(const std::string& path) {
  std::ofstream out(path, std::ios::binary);
  out << "pixels";
}

// S-10：书签由系统生成并解析；文件被挪走时书签指向新路径（项目库据此标原文件缺失），删除后解析失败。
void test_s10_mac_bookmark_resolves_moves_and_missing_files() {
  pixcake::test::TempDir dir;
  const std::string original = real(dir.path()) + "/a.png";
  write_file(original);
  std::vector<std::uint8_t> bookmark;
  EXPECT(pixcake::platform::MacSourceAccess::create_bookmark(original, bookmark) == pixcake::ResultCode::Ok);
  EXPECT(!bookmark.empty());
  pixcake::platform::MacSourceAccess access;
  auto resolved = access.resolve(bookmark);
  EXPECT(resolved.code == pixcake::ResultCode::Ok);
  EXPECT(real(resolved.absolute_path) == original);

  const std::string moved = real(dir.path()) + "/renamed.png";
  EXPECT(std::rename(original.c_str(), moved.c_str()) == 0);
  resolved = access.resolve(bookmark);
  EXPECT(resolved.code == pixcake::ResultCode::Ok || resolved.code == pixcake::ResultCode::SourceMissing);
  if (resolved.code == pixcake::ResultCode::Ok) EXPECT(real(resolved.absolute_path) != original);

  EXPECT(std::remove(moved.c_str()) == 0);
  resolved = access.resolve(bookmark);
  EXPECT(resolved.code == pixcake::ResultCode::SourceMissing);

  EXPECT(pixcake::platform::MacSourceAccess::create_bookmark("relative.png", bookmark) ==
         pixcake::ResultCode::StateConflict);
  EXPECT(pixcake::platform::MacSourceAccess::create_bookmark(real(dir.path()) + "/absent.png", bookmark) ==
         pixcake::ResultCode::SourceMissing);
  EXPECT(access.resolve({1, 2, 3}).code == pixcake::ResultCode::PermissionDenied);
}

}  // namespace

int main() {
  test_s10_mac_bookmark_resolves_moves_and_missing_files();
  if (g_failures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  return 0;
}
