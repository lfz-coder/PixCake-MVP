#include "pixcake/persist/atomic_file.hpp"
#include "pixcake/persist/file_system.hpp"
#include "pixcake/support/counting_log_sink.hpp"
#include "support/fake_file_system.hpp"

#include <algorithm>
#include <chrono>
#include <thread>
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


// 写临时文件很慢的文件系统，用来模拟“提交后迟迟写不完”。
class SlowWriteFileSystem : public pixcake::test::FakeFileSystem {
 public:
  std::chrono::milliseconds delay{0};
  pixcake::IoResult write_new(const std::string& path, const std::vector<std::uint8_t>& data) override {
    std::this_thread::sleep_for(delay);
    return pixcake::test::FakeFileSystem::write_new(path, data);
  }
};

// B-52 / P-16 / O-07：超过截止时间还没写完，不返回成功；正式文件保持上一份，临时文件删掉，留一条可筛选记录。
void test_b52_late_write_keeps_formal_file_and_is_not_success() {
  pixcake::CountingLogSink log;
  SlowWriteFileSystem fs;
  fs.delay = std::chrono::milliseconds(40);
  fs.files["dir/note"] = kOld;
  const auto code = pixcake::atomic_replace(fs, "dir/note", kNew, &log, std::chrono::milliseconds(10));
  EXPECT(code == pixcake::ResultCode::IoFailed);
  EXPECT(fs.files["dir/note"] == kOld);
  EXPECT(fs.files.find("dir/note.tmp") == fs.files.end());
  EXPECT(log.count("persist.write.deadline", pixcake::ResultCode::IoFailed) == 1);
  EXPECT(log.count("persist.write.failed", pixcake::ResultCode::IoFailed) == 1);
  EXPECT(log.count("persist.write.finish") == 0);
  // 截止时间之内写完照常成功。
  fs.delay = std::chrono::milliseconds(0);
  EXPECT(pixcake::atomic_replace(fs, "dir/note", kNew, &log, std::chrono::milliseconds(1000)) == pixcake::ResultCode::Ok);
  EXPECT(fs.files["dir/note"] == kNew);
}

// 默认截止时间就是需求的 2 秒。
void test_p16_default_commit_deadline_is_two_seconds() {
  EXPECT(pixcake::kCommitWriteDeadline == std::chrono::milliseconds(2000));
}

int main() {
  test_b52_late_write_keeps_formal_file_and_is_not_success();
  test_p16_default_commit_deadline_is_two_seconds();
  test_o05_write_start_precedes_files_and_finish_follows();
  test_o11_disk_full_is_counted_each_time();
  test_c10_success_only_after_directory_sync();
  test_b93_disk_full_keeps_formal_file();
  test_b93_permission_denied_keeps_formal_file();
  test_b115_directory_sync_failure_rolls_back();
  test_b115_first_save_directory_sync_failure_leaves_no_formal_file();
  test_absent_file_is_not_success_or_corruption();
  test_posix_write_lock_is_exclusive_across_file_system_instances();
  test_c23_check_durable_revision();
  return g_failures == 0 ? 0 : 1;
}
