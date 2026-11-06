#include "pixcake/persist/atomic_file.hpp"
#include "pixcake/persist/file_system.hpp"
#include "support/fake_file_system.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
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

const std::vector<std::uint8_t> kOld{'O', 'L', 'D'};
const std::vector<std::uint8_t> kNew{'N', 'E', 'W'};

class TransientRollbackFileSystem final : public pixcake::test::FakeFileSystem {
 public:
  int rename_calls = 0;

  pixcake::IoResult rename_file(const std::string& from, const std::string& to) override {
    ++rename_calls;
    if (rename_calls == 2 || rename_calls == 3) {
      return pixcake::io_error(pixcake::ResultCode::IoFailed);
    }
    return pixcake::test::FakeFileSystem::rename_file(from, to);
  }
};

class PermanentRollbackFileSystem final : public pixcake::test::FakeFileSystem {
 public:
  int rename_calls = 0;

  pixcake::IoResult rename_file(const std::string& from, const std::string& to) override {
    ++rename_calls;
    if (rename_calls >= 2) {
      return pixcake::io_error(pixcake::ResultCode::IoFailed);
    }
    return pixcake::test::FakeFileSystem::rename_file(from, to);
  }
};

bool accepts_known(const std::vector<std::uint8_t>& bytes) {
  return bytes == kOld || bytes == kNew;
}

bool event_before(const std::vector<std::string>& events, const std::string& first,
                  const std::string& second) {
  auto a = std::find(events.begin(), events.end(), first);
  auto b = std::find(events.begin(), events.end(), second);
  return a != events.end() && b != events.end() && a < b;
}

void test_c10_success_only_after_directory_sync() {
  pixcake::test::FakeFileSystem fs;
  fs.files["dir/note"] = kOld;
  EXPECT(pixcake::atomic_replace(fs, "dir/note", kNew) == pixcake::ResultCode::Ok);
  EXPECT(fs.files["dir/note"] == kNew);
  EXPECT(fs.files["dir/note.bak"] == kOld);
  EXPECT(event_before(fs.events, "sync_file:dir/note.tmp", "rename:dir/note->dir/note.bak"));
  EXPECT(event_before(fs.events, "rename:dir/note.tmp->dir/note", "sync_dir:dir"));
  for (const auto& path : fs.writes) {
    EXPECT(path.size() >= 4 && path.compare(path.size() - 4, 4, ".tmp") == 0);
  }
}

void test_b93_disk_full_keeps_formal_file() {
  pixcake::test::FakeFileSystem fs;
  fs.files["dir/note"] = kOld;
  fs.write_error = pixcake::ResultCode::DiskFull;
  EXPECT(pixcake::atomic_replace(fs, "dir/note", kNew) == pixcake::ResultCode::DiskFull);
  EXPECT(fs.files["dir/note"] == kOld);
  EXPECT(fs.files.count("dir/note.tmp") == 0);
}

void test_b93_permission_denied_keeps_formal_file() {
  pixcake::test::FakeFileSystem fs;
  fs.files["dir/note"] = kOld;
  fs.rename_error = pixcake::ResultCode::PermissionDenied;
  EXPECT(pixcake::atomic_replace(fs, "dir/note", kNew) == pixcake::ResultCode::PermissionDenied);
  EXPECT(fs.files["dir/note"] == kOld);
  EXPECT(fs.files.count("dir/note.tmp") == 0);
}

void test_b115_directory_sync_failure_rolls_back() {
  pixcake::test::FakeFileSystem fs;
  fs.files["dir/note"] = kOld;
  fs.sync_dir_error = pixcake::ResultCode::IoFailed;
  EXPECT(pixcake::atomic_replace(fs, "dir/note", kNew) == pixcake::ResultCode::IoFailed);
  EXPECT(fs.files["dir/note"] == kOld);
}

void test_b115_first_save_directory_sync_failure_leaves_no_formal_file() {
  pixcake::test::FakeFileSystem fs;
  fs.sync_dir_error = pixcake::ResultCode::IoFailed;
  EXPECT(pixcake::atomic_replace(fs, "dir/note", kNew) == pixcake::ResultCode::IoFailed);
  EXPECT(fs.files.count("dir/note") == 0);
}

void test_b111_crash_after_backup_rename_reopens_backup() {
  pixcake::test::FakeFileSystem fs;
  fs.files["dir/note"] = kOld;
  fs.crash_after = 3;
  bool crashed = false;
  try {
    pixcake::atomic_replace(fs, "dir/note", kNew);
  } catch (const pixcake::test::SimulatedCrash&) {
    crashed = true;
  }
  EXPECT(crashed);
  fs.crash_after = 0;
  auto loaded = pixcake::open_durable_file(fs, "dir/note", accepts_known);
  EXPECT(loaded.code == pixcake::ResultCode::Ok);
  EXPECT(loaded.used_backup);
  EXPECT(loaded.bytes == kOld);
  EXPECT(fs.files["dir/note"] == kOld);
}

void test_b111_transient_restore_failure_retries_and_preserves_formal() {
  TransientRollbackFileSystem fs;
  fs.files["dir/note"] = kOld;
  EXPECT(pixcake::atomic_replace(fs, "dir/note", kNew) == pixcake::ResultCode::IoFailed);
  EXPECT(fs.rename_calls >= 4);
  EXPECT(fs.files["dir/note"] == kOld);
  EXPECT(fs.files.count("dir/note.bak") == 0);
  EXPECT(fs.files.count("dir/note.tmp") == 0);
}

void test_b111_permanent_restore_failure_recreates_formal_from_backup() {
  PermanentRollbackFileSystem fs;
  fs.files["dir/note"] = kOld;
  EXPECT(pixcake::atomic_replace(fs, "dir/note", kNew) == pixcake::ResultCode::IoFailed);
  EXPECT(fs.files["dir/note"] == kOld);
  EXPECT(fs.files.count("dir/note.tmp") == 0);
  EXPECT(fs.files.count("dir/note.bak") == 1);
}

void test_b100_bad_file_is_isolated_and_backup_restored() {
  pixcake::test::FakeFileSystem fs;
  fs.files["dir/note"] = {'B', 'A', 'D'};
  fs.files["dir/note.bak"] = kOld;
  auto loaded = pixcake::open_durable_file(fs, "dir/note", accepts_known);
  EXPECT(loaded.code == pixcake::ResultCode::Ok);
  EXPECT(loaded.used_backup);
  EXPECT(loaded.bytes == kOld);
  EXPECT(fs.files["dir/note"] == kOld);
  bool isolated = false;
  for (const auto& item : fs.files) {
    if (item.first.find("dir/isolate/") == 0 && item.second.size() == 3 && item.second[0] == 'B') {
      isolated = true;
    }
  }
  EXPECT(isolated);
}

void test_b08_no_backup_marks_recovered_empty_and_does_not_write_formal() {
  pixcake::test::FakeFileSystem fs;
  fs.files["dir/note"] = {'B', 'A', 'D'};
  auto loaded = pixcake::open_durable_file(fs, "dir/note", accepts_known);
  EXPECT(loaded.recovered_empty);
  EXPECT(loaded.code == pixcake::ResultCode::DocumentCorrupt);
  EXPECT(fs.files.count("dir/note") == 0);
  EXPECT(!loaded.absent);
}

void test_absent_file_is_not_success_or_corruption() {
  pixcake::test::FakeFileSystem fs;
  auto loaded = pixcake::open_durable_file(fs, "dir/note", accepts_known);
  EXPECT(loaded.absent);
  EXPECT(!loaded.recovered_empty);
  EXPECT(loaded.code != pixcake::ResultCode::Ok);
  EXPECT(loaded.code != pixcake::ResultCode::DocumentCorrupt);
  EXPECT(loaded.code != pixcake::ResultCode::SourceMissing);
  EXPECT(fs.files.empty());
}

void test_posix_truncated_formal_reopens_backup() {
  namespace fs = std::filesystem;
  const auto root = fs::temp_directory_path() / ("pixcake-persist-" + std::to_string(::getpid()));
  fs::remove_all(root);
  fs::create_directories(root);
  pixcake::PosixFileSystem posix;
  const auto formal = (root / "note").string();
  EXPECT(pixcake::atomic_replace(posix, formal, kOld) == pixcake::ResultCode::Ok);
  EXPECT(pixcake::atomic_replace(posix, formal, kNew) == pixcake::ResultCode::Ok);
  {
    std::ofstream out(formal, std::ios::binary | std::ios::trunc);
    out << "B";
  }
  auto loaded = pixcake::open_durable_file(posix, formal, [](const std::vector<std::uint8_t>& bytes) {
    return bytes.size() == 3;
  });
  EXPECT(loaded.code == pixcake::ResultCode::Ok);
  EXPECT(loaded.used_backup);
  EXPECT(loaded.bytes == kOld);
  fs::remove_all(root);
}

void test_posix_write_lock_is_exclusive_across_file_system_instances() {
  namespace fs = std::filesystem;
  const auto root = fs::temp_directory_path() / ("pixcake-lock-" + std::to_string(::getpid()));
  fs::remove_all(root);
  fs::create_directories(root);
  const auto formal = (root / "note").string();
  pixcake::PosixFileSystem first;
  pixcake::PosixFileSystem second;
  auto held = first.try_write_lock(formal);
  EXPECT(held.code == pixcake::ResultCode::Ok);
  EXPECT(held.lock != nullptr);
  auto denied = second.try_write_lock(formal);
  EXPECT(denied.code == pixcake::ResultCode::StateConflict);
  held.lock.reset();
  auto acquired = second.try_write_lock(formal);
  EXPECT(acquired.code == pixcake::ResultCode::Ok);
  EXPECT(acquired.lock != nullptr);
  fs::remove_all(root);
}

}  // namespace

int main() {
  test_c10_success_only_after_directory_sync();
  test_b93_disk_full_keeps_formal_file();
  test_b93_permission_denied_keeps_formal_file();
  test_b115_directory_sync_failure_rolls_back();
  test_b115_first_save_directory_sync_failure_leaves_no_formal_file();
  test_b111_crash_after_backup_rename_reopens_backup();
  test_b111_transient_restore_failure_retries_and_preserves_formal();
  test_b111_permanent_restore_failure_recreates_formal_from_backup();
  test_b100_bad_file_is_isolated_and_backup_restored();
  test_b08_no_backup_marks_recovered_empty_and_does_not_write_formal();
  test_absent_file_is_not_success_or_corruption();
  test_posix_truncated_formal_reopens_backup();
  test_posix_write_lock_is_exclusive_across_file_system_instances();
  return g_failures == 0 ? 0 : 1;
}
