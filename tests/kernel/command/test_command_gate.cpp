#include "pixcake/command/command_gate.hpp"
#include "support/fake_file_system.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
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

class Latch : public pixcake::FastExportLatch {
 public:
  bool on = false;
  int queue_depth = 4;
  bool enabled() const override { return on; }
};

class RecordingLog : public pixcake::LogSink {
 public:
  std::vector<pixcake::LogRecord> records;
  void write(const pixcake::LogRecord& record) override { records.push_back(record); }
};

class SlowFileSystem : public pixcake::test::FakeFileSystem {
 public:
  std::atomic<int> writing{0};
  std::atomic<bool> go{false};

  pixcake::IoResult write_new(const std::string& path, const std::vector<std::uint8_t>& data) override {
    writing.fetch_add(1);
    while (!go.load()) {
      std::this_thread::yield();
    }
    return pixcake::test::FakeFileSystem::write_new(path, data);
  }
};

pixcake::UserCommand command_with(std::string name, std::uint64_t revision, std::string value) {
  pixcake::UserCommand command;
  command.name = std::move(name);
  command.target_id = "img-1";
  command.revision = revision;
  command.capability_id = "color.global";
  command.parameter_id = "exposure";
  command.display_value = std::move(value);
  return command;
}

std::vector<pixcake::VersionLimit> limits() { return {pixcake::VersionLimit{"color.global", 1}}; }

void test_b01_missing_fields_do_not_write() {
  auto document = *pixcake::EditDocument::create("img-1");
  Latch latch;
  pixcake::test::FakeFileSystem files;
  RecordingLog log;
  pixcake::CommandGate gate(document, latch, files, "docs/img.pxedit", limits(), &log);
  pixcake::UserCommand missing;
  missing.name = "commit";
  missing.target_id = "img-1";
  const auto result = gate.submit(missing);
  EXPECT(result.code == pixcake::ResultCode::StateConflict);
  EXPECT(result.revision == 0);
  EXPECT(document.revision() == 0);
  EXPECT(files.writes.empty());

  pixcake::UserCommand blank = command_with("commit", 0, "0");
  blank.name = "";
  EXPECT(gate.submit(blank).code == pixcake::ResultCode::StateConflict);
  EXPECT(files.writes.empty());
}

void test_c24_previews_do_not_write_or_bump_revision() {
  auto document = *pixcake::EditDocument::create("img-1");
  Latch latch;
  pixcake::test::FakeFileSystem files;
  pixcake::CommandGate gate(document, latch, files, "docs/img.pxedit", limits(), nullptr);
  EXPECT(gate.submit(command_with("preview", 0, "drag-1")).code == pixcake::ResultCode::Ok);
  EXPECT(gate.submit(command_with("preview", 0, "drag-2")).code == pixcake::ResultCode::Ok);
  EXPECT(gate.submit(command_with("preview", 0, "-1.50")).code == pixcake::ResultCode::Ok);
  EXPECT(document.revision() == 0);
  EXPECT(files.writes.empty());
  EXPECT(document.preview("color.global", "exposure") == "-1.50");
}

void test_b61_commit_writes_latest_preview() {
  auto document = *pixcake::EditDocument::create("img-1");
  Latch latch;
  pixcake::test::FakeFileSystem files;
  pixcake::CommandGate gate(document, latch, files, "docs/img.pxedit", limits(), nullptr);
  EXPECT(gate.submit(command_with("preview", 0, "drag-1")).code == pixcake::ResultCode::Ok);
  EXPECT(gate.submit(command_with("preview", 0, "-1.50")).code == pixcake::ResultCode::Ok);
  const auto committed = gate.submit(command_with("commit", 0, "stale-command-value"));
  EXPECT(committed.code == pixcake::ResultCode::Ok);
  EXPECT(committed.revision == 1);
  EXPECT(document.committed_parameter("color.global", "exposure") == "-1.50");
  EXPECT(files.writes.size() == 1);
}

void test_b73_stale_revision_does_not_write() {
  auto document = *pixcake::EditDocument::create("img-1");
  Latch latch;
  pixcake::test::FakeFileSystem files;
  pixcake::CommandGate gate(document, latch, files, "docs/img.pxedit", limits(), nullptr);
  gate.submit(command_with("preview", 0, "0.00"));
  EXPECT(gate.submit(command_with("commit", 0, "0.00")).code == pixcake::ResultCode::Ok);
  const auto writes = files.writes.size();
  const auto stale = gate.submit(command_with("commit", 0, "1.00"));
  EXPECT(stale.code == pixcake::ResultCode::StateConflict);
  EXPECT(stale.revision == 1);
  EXPECT(files.writes.size() == writes);
  EXPECT(document.committed_parameter("color.global", "exposure") == "0.00");
}

void test_b62_fast_export_stays_until_the_switch_is_turned_off() {
  auto document = *pixcake::EditDocument::create("img-1");
  Latch latch;
  latch.on = true;
  latch.queue_depth = 0;
  pixcake::test::FakeFileSystem files;
  RecordingLog log;
  pixcake::CommandGate gate(document, latch, files, "docs/img.pxedit", limits(), &log);
  const auto preview = gate.submit(command_with("preview", 0, "SECRET-DISPLAY"));
  const auto commit = gate.submit(command_with("commit", 0, "SECRET-DISPLAY"));
  EXPECT(preview.code == pixcake::ResultCode::FastExportBusy);
  EXPECT(commit.code == pixcake::ResultCode::FastExportBusy);
  EXPECT(files.writes.empty());
  EXPECT(document.revision() == 0);
  EXPECT(!log.records.empty());
  EXPECT(log.records.back().message == "极速导出占用");
  EXPECT(log.records.back().message.find("SECRET-DISPLAY") == std::string::npos);
  EXPECT(log.records.back().path.find("SECRET-DISPLAY") == std::string::npos);
  EXPECT(log.records.back().capability_id.find("SECRET-DISPLAY") == std::string::npos);
  latch.on = false;
  EXPECT(gate.submit(command_with("preview", 0, "-1.50")).code == pixcake::ResultCode::Ok);
  EXPECT(gate.submit(command_with("commit", 0, "ignored")).code == pixcake::ResultCode::Ok);
  EXPECT(document.committed_parameter("color.global", "exposure") == "-1.50");
}

void test_unknown_command_does_not_write() {
  auto document = *pixcake::EditDocument::create("img-1");
  Latch latch;
  pixcake::test::FakeFileSystem files;
  pixcake::CommandGate gate(document, latch, files, "docs/img.pxedit", limits(), nullptr);
  EXPECT(gate.submit(command_with("export", 0, "0")).code == pixcake::ResultCode::StateConflict);
  EXPECT(files.writes.empty());
}

void test_b60_simultaneous_commits_serialize_to_one_revision() {
  auto document = *pixcake::EditDocument::create("img-1");
  Latch latch;
  SlowFileSystem files;
  pixcake::CommandGate gate(document, latch, files, "docs/img.pxedit", limits(), nullptr);
  EXPECT(gate.submit(command_with("preview", 0, "-1.50")).code == pixcake::ResultCode::Ok);
  pixcake::CommandResult first;
  pixcake::CommandResult second;
  std::thread t1([&] { first = gate.submit(command_with("commit", 0, "ignored")); });
  std::thread t2([&] { second = gate.submit(command_with("commit", 0, "ignored")); });
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (files.writing.load() < 1 && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::yield();
  }
  EXPECT(files.writing.load() == 1);
  files.go = true;
  t1.join();
  t2.join();
  const int successes = (first.code == pixcake::ResultCode::Ok ? 1 : 0) +
                        (second.code == pixcake::ResultCode::Ok ? 1 : 0);
  EXPECT(successes == 1);
  EXPECT(document.revision() == 1);
  EXPECT(files.writing.load() == 1);
  EXPECT(document.committed_parameter("color.global", "exposure") == "-1.50");
}

}  // namespace

int main() {
  test_b01_missing_fields_do_not_write();
  test_c24_previews_do_not_write_or_bump_revision();
  test_b61_commit_writes_latest_preview();
  test_b73_stale_revision_does_not_write();
  test_b62_fast_export_stays_until_the_switch_is_turned_off();
  test_unknown_command_does_not_write();
  test_b60_simultaneous_commits_serialize_to_one_revision();
  return g_failures == 0 ? 0 : 1;
}
