#include "pixcake/persist/atomic_file.hpp"
#include "pixcake/persist/file_system.hpp"
#include "pixcake/support/counting_log_sink.hpp"
#include "support/fake_file_system.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
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

std::vector<std::uint8_t> revision_bytes(std::uint64_t revision) {
  std::vector<std::uint8_t> bytes;
  for (int shift = 0; shift < 64; shift += 8) bytes.push_back(static_cast<std::uint8_t>(revision >> shift));
  return bytes;
}

std::optional<std::uint64_t> revision_of(const std::vector<std::uint8_t>& bytes) {
  if (bytes.size() != 8) return std::nullopt;
  std::uint64_t revision = 0;
  for (int index = 0; index < 8; ++index) revision |= static_cast<std::uint64_t>(bytes[index]) << (index * 8);
  return revision;
}

void test_c23_check_durable_revision() {
  pixcake::test::FakeFileSystem fs;
  EXPECT(pixcake::check_durable_revision(fs, "dir/doc", 0, revision_of) == pixcake::ResultCode::Ok);
  fs.files["dir/doc"] = revision_bytes(3);
  EXPECT(pixcake::check_durable_revision(fs, "dir/doc", 3, revision_of) == pixcake::ResultCode::Ok);
  EXPECT(pixcake::check_durable_revision(fs, "dir/doc", 2, revision_of) == pixcake::ResultCode::StateConflict);
  EXPECT(fs.files["dir/doc"] == revision_bytes(3));

  pixcake::test::FakeFileSystem damaged;
  damaged.files["dir/doc"] = {'B', 'A', 'D'};
  damaged.files["dir/doc.bak"] = revision_bytes(5);
  EXPECT(pixcake::check_durable_revision(damaged, "dir/doc", 3, revision_of) == pixcake::ResultCode::StateConflict);
  EXPECT(pixcake::check_durable_revision(damaged, "dir/doc", 5, revision_of) == pixcake::ResultCode::Ok);
}

// 写入发生时，日志里已经有几条“开始”记录。
struct WatchingFileSystem final : pixcake::test::FakeFileSystem {
  const pixcake::CountingLogSink* log = nullptr;
  std::uint64_t starts_seen_at_first_write = 99;
  pixcake::IoResult write_new(const std::string& path, const std::vector<std::uint8_t>& data) override {
    if (starts_seen_at_first_write == 99) starts_seen_at_first_write = log->count("persist.write.start");
    return FakeFileSystem::write_new(path, data);
  }
};

struct KeepingSink final : pixcake::LogSink {
  std::vector<pixcake::LogRecord> records;
  void write(const pixcake::LogRecord& record) override { records.push_back(record); }
};

void test_o05_write_start_precedes_files_and_finish_follows() {
  KeepingSink kept;
  pixcake::CountingLogSink log(&kept);
  WatchingFileSystem fs;
  fs.log = &log;
  fs.files["dir/note"] = kOld;
  EXPECT(pixcake::atomic_replace(fs, "dir/note", kNew, &log) == pixcake::ResultCode::Ok);
  EXPECT(fs.starts_seen_at_first_write == 1);
  EXPECT(log.count("persist.write.start") == 1);
  EXPECT(log.count("persist.write.finish", pixcake::ResultCode::Ok) == 1);
  EXPECT(log.count("persist.write.failed") == 0);
  EXPECT(!kept.records.empty() && kept.records.back().event == "persist.write.finish");
  EXPECT(!kept.records.empty() && kept.records.back().module == "persist");
  EXPECT(!kept.records.empty() && kept.records.back().path == "dir/note");
}

void test_o11_disk_full_is_counted_each_time() {
  pixcake::CountingLogSink log;
  pixcake::test::FakeFileSystem fs;
  fs.files["dir/note"] = kOld;
  fs.write_error = pixcake::ResultCode::DiskFull;
  EXPECT(pixcake::atomic_replace(fs, "dir/note", kNew, &log) == pixcake::ResultCode::DiskFull);
  EXPECT(pixcake::atomic_replace(fs, "dir/note", kNew, &log) == pixcake::ResultCode::DiskFull);
  EXPECT(log.count("persist.write.start") == 2);
  EXPECT(log.count("persist.write.failed", pixcake::ResultCode::DiskFull) == 2);
  EXPECT(log.count("persist.write.finish") == 0);
}

void test_o10_backup_and_empty_recovery_are_logged() {
  pixcake::CountingLogSink log;
  pixcake::test::FakeFileSystem fs;
  fs.files["dir/note"] = {'B', 'A', 'D'};
  fs.files["dir/note.bak"] = kOld;
  EXPECT(pixcake::open_durable_file(fs, "dir/note", accepts_known, &log).used_backup);
  EXPECT(log.count("persist.recover.backup") == 1);
  EXPECT(log.count("persist.quarantine") == 1);
  EXPECT(log.count("persist.recover.empty") == 0);

  pixcake::CountingLogSink empty_log;
  pixcake::test::FakeFileSystem broken;
  broken.files["dir/note"] = {'B', 'A', 'D'};
  broken.files["dir/note.bak"] = {'B', 'A', 'D', '2'};
  EXPECT(pixcake::open_durable_file(broken, "dir/note", accepts_known, &empty_log).recovered_empty);
  EXPECT(empty_log.count("persist.recover.empty", pixcake::ResultCode::DocumentCorrupt) == 1);
  EXPECT(empty_log.count("persist.quarantine") == 2);
  EXPECT(empty_log.count("persist.recover.backup") == 0);
}

int main() {
  test_o05_write_start_precedes_files_and_finish_follows();
  test_o11_disk_full_is_counted_each_time();
  test_o10_backup_and_empty_recovery_are_logged();
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
  test_c23_check_durable_revision();
  return g_failures == 0 ? 0 : 1;
}
