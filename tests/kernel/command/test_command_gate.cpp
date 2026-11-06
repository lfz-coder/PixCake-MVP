#include "pixcake/capability/fragment_codec.hpp"
#include "pixcake/command/command_gate.hpp"
#include "pixcake/layerdoc/layer_document.hpp"
#include "pixcake/preset/preset_library.hpp"
#include "pixcake/sync/sync_batch.hpp"
#include "support/fake_file_system.hpp"
#include "support/temp_dir.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#if defined(__APPLE__) || defined(__unix__)
#include <sys/wait.h>
#include <unistd.h>
#define PIXCAKE_HAVE_FORK 1
#endif

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

void test_direct_document_commit_observes_registered_latch() {
  auto document = *pixcake::EditDocument::create("img-1");
  Latch latch;
  latch.on = true;
  pixcake::test::FakeFileSystem files;
  pixcake::CommandGate gate(document, latch, files, "docs/img.pxedit", limits(), nullptr);
  EXPECT(document.set_preview("color.global", "exposure", "1.00") == pixcake::ResultCode::Ok);
  EXPECT(document.commit_preview(0, "color.global", "exposure", limits(), files, "docs/img.pxedit") ==
         pixcake::ResultCode::FastExportBusy);
  EXPECT(document.revision() == 0);
  EXPECT(files.writes.empty());
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

void test_b60_two_gates_share_document_submission_state() {
  auto first_document = *pixcake::EditDocument::create("img-1");
  auto second_document = *pixcake::EditDocument::create("img-1");
  Latch latch;
  pixcake::test::FakeFileSystem files;
  pixcake::CommandGate first(first_document, latch, files, "docs/shared.pxedit", limits(), nullptr);
  pixcake::CommandGate second(second_document, latch, files, "docs/shared.pxedit", limits(), nullptr);
  EXPECT(first.submit(command_with("preview", 0, "-1.50")).code == pixcake::ResultCode::Ok);
  EXPECT(first.submit(command_with("commit", 0, "ignored")).code == pixcake::ResultCode::Ok);
  EXPECT(second.submit(command_with("preview", 0, "2.00")).code == pixcake::ResultCode::StateConflict);
  EXPECT(second_document.revision() == 0);
}

#if defined(PIXCAKE_HAVE_FORK)
pixcake::ResultCode submit_exposure(const std::string& path, pixcake::EditDocument& document, const std::string& value) {
  pixcake::PosixFileSystem files;
  Latch latch;
  const std::vector<pixcake::VersionLimit> limits{{"color.global", 1}};
  pixcake::CommandGate gate(document, latch, files, path, limits, nullptr);
  pixcake::UserCommand command;
  command.name = "preview";
  command.target_id = "img-1";
  command.revision = document.revision();
  command.capability_id = "color.global";
  command.parameter_id = "exposure";
  command.display_value = value;
  if (gate.submit(command).code != pixcake::ResultCode::Ok) return pixcake::ResultCode::IoFailed;
  command.name = "commit";
  return gate.submit(command).code;
}

// 两个进程都在修订 0 装载同一份文档；先提交者成功，后提交者必须得到状态冲突。
void test_b60_c23_cross_process_gate_rejects_stale_commit() {
  pixcake::test::TempDir dir;
  const std::string path = dir.path() + "/img-1.pxedit";
  const std::vector<pixcake::VersionLimit> limits{{"color.global", 1}};
  pixcake::PosixFileSystem files;
  EXPECT(pixcake::EditDocument::create("img-1")->write_snapshot(files, path) == pixcake::ResultCode::Ok);

  int loaded_pipe[2];
  int go_pipe[2];
  EXPECT(pipe(loaded_pipe) == 0 && pipe(go_pipe) == 0);
  const pid_t child = fork();
  if (child == 0) {
    pixcake::EditDocument stale;
    pixcake::PosixFileSystem child_files;
    const bool opened = pixcake::EditDocument::load(stale, child_files, path, limits).code == pixcake::ResultCode::Ok;
    char byte = 1;
    (void)!write(loaded_pipe[1], &byte, 1);
    (void)!read(go_pipe[0], &byte, 1);
    const bool rejected = submit_exposure(path, stale, "-2.00") == pixcake::ResultCode::StateConflict;
    _exit(opened && rejected ? 0 : 1);
  }
  char byte = 0;
  EXPECT(read(loaded_pipe[0], &byte, 1) == 1);
  pixcake::EditDocument current;
  EXPECT(pixcake::EditDocument::load(current, files, path, limits).code == pixcake::ResultCode::Ok);
  EXPECT(submit_exposure(path, current, "2.00") == pixcake::ResultCode::Ok);
  EXPECT(write(go_pipe[1], &byte, 1) == 1);
  int status = 0;
  EXPECT(waitpid(child, &status, 0) == child);
  EXPECT(WIFEXITED(status) && WEXITSTATUS(status) == 0);

  pixcake::EditDocument reloaded;
  EXPECT(pixcake::EditDocument::load(reloaded, files, path, limits).code == pixcake::ResultCode::Ok);
  EXPECT(reloaded.revision() == 1);
  EXPECT(reloaded.committed_parameter("color.global", "exposure") == std::optional<std::string>("2.00"));
}
#endif

}  // namespace

// C-35 / O-04：已注册能力形成环时拒绝开始编辑，写一条可筛选的日志，不碰用户文件。
void test_c35_o04_cyclic_pipeline_refuses_editing() {
  auto document = *pixcake::EditDocument::create("img-1");
  Latch latch;
  pixcake::test::FakeFileSystem files;
  RecordingLog log;
  std::vector<pixcake::PipelineStep> cyclic = {
      {"color.global", "color.global", true, {"color.local"}},
      {"color.local", "color.local", true, {"color.global"}},
  };
  pixcake::CommandGate gate(document, latch, files, "docs/img.pxedit", limits(), &log, cyclic);
  EXPECT(gate.submit(command_with("preview", 0, "1.00")).code == pixcake::ResultCode::StateConflict);
  EXPECT(gate.submit(command_with("commit", 0, "1.00")).code == pixcake::ResultCode::StateConflict);
  EXPECT(document.revision() == 0);
  EXPECT(!document.preview("color.global", "exposure").has_value());
  EXPECT(files.writes.empty());
  std::size_t cycle_records = 0;
  for (const auto& record : log.records) {
    if (record.event == "command.pipeline.invalid") {
      ++cycle_records;
      EXPECT(record.module == "command");
      EXPECT(record.code == pixcake::ResultCode::StateConflict);
      EXPECT(record.message.find("环路") != std::string::npos);
    }
  }
  EXPECT(cycle_records == 2);
}

// 默认管线（phase_one_pipeline）没有环，正常受理。
void test_c35_default_pipeline_allows_editing() {
  auto document = *pixcake::EditDocument::create("img-1");
  Latch latch;
  pixcake::test::FakeFileSystem files;
  RecordingLog log;
  pixcake::CommandGate gate(document, latch, files, "docs/img.pxedit", limits(), &log);
  EXPECT(gate.submit(command_with("preview", 0, "1.00")).code == pixcake::ResultCode::Ok);
  for (const auto& record : log.records) EXPECT(record.event != "command.pipeline.invalid");
}

// 第七轮条件 2（架构 6.2、契约 C-23）：预设、粘贴、批量同步、应用至精修都从命令入口进入，
// 提交成功后和滑杆提交一样通知 CommitListener；失败时不通知、文档不变。
class RecordingListener final : public pixcake::CommitListener {
 public:
  std::vector<std::pair<std::string, std::uint64_t>> commits;
  void committed(const std::string& image_id, std::uint64_t revision) override { commits.emplace_back(image_id, revision); }
};

std::vector<std::uint8_t> marked(std::uint8_t n) { return pixcake::encode_fragment_records({{77, {n}}}); }

pixcake::Preset ai_preset() {
  pixcake::Preset preset;
  preset.id = "ai";
  preset.name = "\xE4\xB8\x80";
  preset.type = pixcake::kPresetAiColor;
  preset.fragments.push_back(pixcake::PresetFragment{"color.style", 1, marked(8)});
  return preset;
}

void test_c23_preset_apply_goes_through_the_gate() {
  pixcake::test::FakeFileSystem files;
  Latch latch;
  auto document = *pixcake::EditDocument::create("img-1");
  pixcake::CommandGate gate(document, latch, files, "docs/c23-preset.pxedit", limits(), nullptr);
  RecordingListener listener;
  gate.set_commit_listener(&listener);
  pixcake::PresetLibrary library;
  const auto result = gate.apply_preset(library, ai_preset(), 0);
  EXPECT(result.code == pixcake::ResultCode::Ok);
  EXPECT(result.revision == 1);
  EXPECT(document.fragment("color.style").has_value());
  EXPECT(listener.commits.size() == 1 && listener.commits[0].first == "img-1" && listener.commits[0].second == 1);
  // 命令入口记下新修订：接着按修订 1 提交滑杆不算过期。
  EXPECT(gate.submit(command_with("preview", 1, "1.00")).code == pixcake::ResultCode::Ok);
  EXPECT(gate.submit(command_with("commit", 1, "1.00")).code == pixcake::ResultCode::Ok);
}

void test_c23_paste_goes_through_the_gate() {
  pixcake::test::FakeFileSystem files;
  Latch latch;
  auto source = *pixcake::EditDocument::create("img-src");
  source.store_fragment("detail", 1, marked(3));
  auto document = *pixcake::EditDocument::create("img-1");
  pixcake::CommandGate gate(document, latch, files, "docs/c23-paste.pxedit", limits(), nullptr);
  RecordingListener listener;
  gate.set_commit_listener(&listener);
  const auto result = gate.paste(pixcake::copy_committed(source), 0);
  EXPECT(result.code == pixcake::ResultCode::Ok);
  EXPECT(document.fragment("detail").has_value() && document.fragment("detail")->inner == marked(3));
  EXPECT(listener.commits.size() == 1 && listener.commits[0].second == document.revision());
}

void test_c23_sync_goes_through_each_target_gate() {
  pixcake::test::FakeFileSystem files;
  Latch latch;
  auto source = *pixcake::EditDocument::create("img-src");
  source.store_fragment("detail", 1, marked(3));
  auto first = *pixcake::EditDocument::create("img-1");
  auto second = *pixcake::EditDocument::create("img-2");
  pixcake::CommandGate first_gate(first, latch, files, "docs/c23-sync-1.pxedit", limits(), nullptr);
  pixcake::CommandGate second_gate(second, latch, files, "docs/c23-sync-2.pxedit", limits(), nullptr);
  RecordingListener listener;
  first_gate.set_commit_listener(&listener);
  second_gate.set_commit_listener(&listener);
  EXPECT(first_gate.sync_from(source, "docs/c23-source.pxedit", {"detail"}, 0).code == pixcake::ResultCode::Ok);
  EXPECT(second_gate.sync_from(source, "docs/c23-source.pxedit", {"detail"}, 0).code == pixcake::ResultCode::Ok);
  EXPECT(first.fragment("detail").has_value() && second.fragment("detail").has_value());
  EXPECT(listener.commits.size() == 2);
  EXPECT(listener.commits.size() == 2 && listener.commits[0].first == "img-1" && listener.commits[1].first == "img-2");
}

void test_c23_apply_to_original_goes_through_the_gate() {
  pixcake::test::FakeFileSystem files;
  Latch latch;
  auto document = *pixcake::EditDocument::create("img-1");
  document.store_fragment("detail", 1, marked(3));
  pixcake::CommandGate gate(document, latch, files, "docs/c23-bake.pxedit", limits(), nullptr);
  RecordingListener listener;
  gate.set_commit_listener(&listener);
  const auto layers = *pixcake::LayerDocument::create("img-1", document.revision());
  const auto result = gate.apply_layers(layers, document.revision());
  EXPECT(result.code == pixcake::ResultCode::Ok);
  EXPECT(document.effects_baked());
  EXPECT(listener.commits.size() == 1 && listener.commits[0].second == document.revision());
  // 已经烘焙过：没有新修订，不再通知。
  EXPECT(gate.apply_layers(layers, document.revision()).code == pixcake::ResultCode::Ok);
  EXPECT(listener.commits.size() == 1);
}

void test_c23_b73_failed_document_commands_do_not_notify() {
  pixcake::test::FakeFileSystem files;
  Latch latch;
  RecordingLog log;
  auto document = *pixcake::EditDocument::create("img-1");
  pixcake::CommandGate gate(document, latch, files, "docs/c23-fail.pxedit", limits(), &log);
  RecordingListener listener;
  gate.set_commit_listener(&listener);
  pixcake::PresetLibrary library;
  EXPECT(gate.apply_preset(library, ai_preset(), 5).code == pixcake::ResultCode::StateConflict);
  latch.on = true;
  EXPECT(gate.apply_preset(library, ai_preset(), 0).code == pixcake::ResultCode::FastExportBusy);
  latch.on = false;
  files.write_error = pixcake::ResultCode::DiskFull;
  EXPECT(gate.apply_preset(library, ai_preset(), 0).code == pixcake::ResultCode::DiskFull);
  files.write_error = pixcake::ResultCode::Ok;
  EXPECT(document.revision() == 0);
  EXPECT(!document.fragment("color.style").has_value());
  EXPECT(listener.commits.empty());
  EXPECT(log.records.size() == 3);
  EXPECT(!log.records.empty() && log.records.back().code == pixcake::ResultCode::DiskFull);
}

// C-33：追色结果只经命令入口写进 color.global、color.local（架构“写回的存放与切换”）。
void test_c33_match_result_written_through_the_gate() {
  pixcake::test::FakeFileSystem files;
  Latch latch;
  auto document = *pixcake::EditDocument::create("img-1");
  pixcake::CommandGate gate(document, latch, files, "docs/c33-match.pxedit", limits(), nullptr);
  RecordingListener listener;
  gate.set_commit_listener(&listener);
  EXPECT(gate.write_match_result({{"color.style", {{"amount", "50"}}}}, 0).code == pixcake::ResultCode::StateConflict);
  EXPECT(gate.write_match_result({{"color.global", {{"exposure", "0.03"}}}}, 0).code ==
         pixcake::ResultCode::StateConflict);
  EXPECT(document.revision() == 0 && listener.commits.empty());
  const auto result = gate.write_match_result({{"color.global", {{"exposure", "1.00"}}}}, 0);
  EXPECT(result.code == pixcake::ResultCode::Ok && result.revision == 1);
  EXPECT(document.match_written_parameter("color.global", "exposure") == std::optional<std::string>("1.00"));
  EXPECT(!document.committed_parameter("color.global", "exposure").has_value());
  EXPECT(listener.commits.size() == 1);
}

int main() {
  test_c33_match_result_written_through_the_gate();
  test_c23_preset_apply_goes_through_the_gate();
  test_c23_paste_goes_through_the_gate();
  test_c23_sync_goes_through_each_target_gate();
  test_c23_apply_to_original_goes_through_the_gate();
  test_c23_b73_failed_document_commands_do_not_notify();
  test_c35_o04_cyclic_pipeline_refuses_editing();
  test_c35_default_pipeline_allows_editing();
#if defined(PIXCAKE_HAVE_FORK)
  test_b60_c23_cross_process_gate_rejects_stale_commit();
#endif
  test_b01_missing_fields_do_not_write();
  test_c24_previews_do_not_write_or_bump_revision();
  test_b61_commit_writes_latest_preview();
  test_b73_stale_revision_does_not_write();
  test_b62_fast_export_stays_until_the_switch_is_turned_off();
  test_direct_document_commit_observes_registered_latch();
  test_unknown_command_does_not_write();
  test_b60_simultaneous_commits_serialize_to_one_revision();
  test_b60_two_gates_share_document_submission_state();
  return g_failures == 0 ? 0 : 1;
}
