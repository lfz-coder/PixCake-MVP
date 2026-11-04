# 导出队列 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 入队时复制文档修订和导出设置，由队列把调用方给来的成品字节写完、校验、再换名；重开时重放事件，并按崩溃点把队列收回来。

**Architecture:** `kernel/exportq` 只调度和提交文件，不编码图片，也不扣张。成品事实在提交成功时产生，测试再交给已经存在的账本结算。`storage/sqlite` 把导出事件写进调用方给的另一个库文件。项目库和账本库的 `user_version` 都不改。

**Tech Stack:** C++17、CMake、CTest、系统自带的 `sqlite3`。测试是独立可执行文件，不引入第三方测试框架。文件系统用现有的 `FileSystem`，失败注入用 `tests/support/fake_file_system.hpp`。

## Global Constraints

- 导出任务入队时复制文档修订和导出设置。之后调用方再改自己的结构，已经入队的快照不变。失败重试用这份快照，不读活文档（契约 C-49）。
- 状态只有：排队、准备、渲染、写临时文件、提交到目标路径、成功、失败、取消（契约 C-49、架构第 6.12 节）。
- 临时文件在调用方给的缓存目录里，一名一件。提交顺序是写完、刷文件、校验、再换名到目标路径。换名未完成时目标路径不出现半张成品（架构第 6.12 节）。
- 尚未开始写临时文件：回到排队。正在写临时文件：删除临时文件，任务失败，可以重试。临时文件已完整但还没换名：重新换名并校验。目标文件已通过校验：标成成功，不再写一次。目标路径上的文件和开始事件里的校验和不一致：删除该文件，任务失败。没有开始事件的文件不认作成品，也不删除（架构第 11 节）。
- 开始事件写在碰目标路径之前。完成事件写在校验成功之后（架构第 11 节）。
- 张数仍由账本在结算时扣。队列不包含 `pixcake/ledger/`，也不调用结算。失败或取消且没有完成事件时，不产生成品事实（契约 C-48、架构第 14 节第 7 条）。
- 极速导出是队列上的用户开关，`ExportQueue` 实现已有的 `FastExportLatch`。开启期间已有的命令入口拒绝 `preview` 和 `commit`。队列里没有任务，或任务已经成功，都不把开关关掉。只有 `set_fast_export(false)` 关掉（契约 C-03、C-21、边界 B-62、质量 Q-19）。
- 排版参数放在入队快照里，队列不解释里面的文字。没有打印机时，目标文件仍按同一套提交流程写完；`request_print` 把出参设为不可用。本计划不调用打印机，也不新增 `ResultCode`。现有结果码里没有「不可用」（架构第 6.12 节、边界 B-11）。
- 一个任务的磁盘满、权限或输入输出失败不回滚其他任务。目标路径在失败返回时不存在半成品（架构第 12 节）。
- 用户路径沿用项目库已经在用的规则：非空、以 `/` 开头、不含 NUL。导出编号非空，不是 `.` 或 `..`，不含 `/`、`\` 和 NUL。
- `kernel/exportq` 不包含 Qt 头，不包含 `sqlite3.h`，不包含 `pixcake/ledger/`，不包含 `capabilities/`，不调用 `system`。
- 导出库文件和项目库、账本库不是同一个文件。`user_version` 为 0 时建本计划的三张表并写成 1。已经是 1 则直接开。其他版本返回状态冲突，不建表，也不改 `user_version`。项目库仍是 3。
- 执行时只在本地提交，不推远端，不建 Pull Request。

本计划明确不做：像素编码、水印绘制、排版绘制、系统打印对话框、工具价、支付、Qt 壳、`kernel-cli`、改 `command_gate.cpp`、改账本折叠、升项目库或账本库的 `user_version`。退出时的询问对话框是壳层的事；队列只提供不留半成品的取消。

---

### Task 1: 快照、提交和崩溃点

**Files:**
- Create: `kernel/exportq/include/pixcake/exportq/export_queue.hpp`
- Create: `kernel/exportq/src/export_queue.cpp`
- Create: `kernel/exportq/CMakeLists.txt`
- Create: `tests/kernel/exportq/test_export_queue.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Modify: `tests/arch/check_dependencies.cmake`

**Interfaces:**
- Consumes: `ResultCode`、`FileSystem`、`FastExportLatch`、`checksum_fnv1a64`、`CommandGate`、`CreditLedger`
- Produces: `ExportJob`、`ExportEvent`、`ExportProductFact`、`ExportCache`、`ExportEventSink`、`ExportQueue`

- [x] **Step 1: Write the failing test**

把 `tests/kernel/exportq/test_export_queue.cpp` 写成下面的文件。根 `CMakeLists.txt` 在 `kernel/ledger` 后加入 `add_subdirectory(kernel/exportq)`。`tests/CMakeLists.txt` 在 `test_ledger` 后加入 `test_export_queue`，包含 `${CMAKE_SOURCE_DIR}/tests`，链接 `pixcake_exportq`、`pixcake_command`、`pixcake_ledger`、`pixcake_warnings`。`kernel/exportq/CMakeLists.txt` 先只编译一个空的 `src/export_queue.cpp`，让失败停在缺少声明。

`tests/arch/check_dependencies.cmake` 的规则 7 改成：

```cmake
scan("${SOURCE_DIR}/kernel/exportq" "rule 7" "capabilities/" "ledger/src" "pixcake/ledger/" "sqlite3.h")
```

```cpp
#include "pixcake/command/command_gate.hpp"
#include "pixcake/exportq/export_queue.hpp"
#include "pixcake/ledger/credit_ledger.hpp"
#include "support/fake_file_system.hpp"

#include <cstdio>
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

pixcake::ExportJob job_of(std::string export_id, std::string original_id, std::string destination) {
  pixcake::ExportJob job;
  job.export_id = std::move(export_id);
  job.original_id = std::move(original_id);
  job.document_revision = 7;
  job.destination_path = std::move(destination);
  job.settings_snapshot = "quality=8";
  job.watermark_free_second = false;
  job.print_requested = false;
  return job;
}

const std::vector<std::uint8_t> kBytes = {1, 2, 3, 4};

void reach_render(pixcake::ExportQueue& queue, const pixcake::ExportJob& job) {
  EXPECT(queue.enqueue(job) == pixcake::ResultCode::Ok);
  EXPECT(queue.prepare(job.export_id) == pixcake::ResultCode::Ok);
  EXPECT(queue.start_rendering(job.export_id) == pixcake::ResultCode::Ok);
}

void test_snapshot_and_fast_export_stay() {
  pixcake::test::FakeFileSystem files;
  auto queue = pixcake::ExportQueue::open("/cache/exports");
  auto job = job_of("exp-1", "orig-1", "/out/a.jpg");
  EXPECT(queue.enqueue(job) == pixcake::ResultCode::Ok);
  job.document_revision = 99;
  job.settings_snapshot = "quality=1";
  const auto before = queue.events().size();
  EXPECT(queue.enqueue(job) == pixcake::ResultCode::Ok);
  EXPECT(queue.events().size() == before);
  pixcake::ExportJob stored;
  EXPECT(queue.find_job("exp-1", stored));
  EXPECT(stored.document_revision == 7);
  EXPECT(stored.settings_snapshot == "quality=8");

  EXPECT(queue.set_fast_export(true) == pixcake::ResultCode::Ok);
  EXPECT(queue.enabled());
  EXPECT(queue.set_fast_export(true) == pixcake::ResultCode::Ok);
  reach_render(queue, job_of("exp-1", "orig-1", "/out/a.jpg"));
  EXPECT(queue.commit_product("exp-1", kBytes, files) == pixcake::ResultCode::Ok);
  EXPECT(queue.status("exp-1") == "成功");
  EXPECT(queue.enabled());

  auto document = *pixcake::EditDocument::create("img-1");
  pixcake::CommandGate gate(document, queue, files, "/docs/img.pxedit",
                            {pixcake::VersionLimit{"color.global", 1}}, nullptr);
  pixcake::UserCommand preview;
  preview.name = "preview";
  preview.target_id = "img-1";
  preview.revision = 0;
  preview.capability_id = "color.global";
  preview.parameter_id = "exposure";
  preview.display_value = "0";
  EXPECT(gate.submit(preview).code == pixcake::ResultCode::FastExportBusy);
  preview.name = "commit";
  EXPECT(gate.submit(preview).code == pixcake::ResultCode::FastExportBusy);
  EXPECT(queue.set_fast_export(false) == pixcake::ResultCode::Ok);
  EXPECT(!queue.enabled());
  preview.name = "preview";
  EXPECT(gate.submit(preview).code == pixcake::ResultCode::Ok);
}

void test_commit_once_and_reject_a_different_destination() {
  pixcake::test::FakeFileSystem files;
  auto queue = pixcake::ExportQueue::open("/cache/exports");
  auto job = job_of("exp-1", "orig-1", "/out/a.jpg");
  const auto rejected = queue.events().size();
  auto relative = job;
  relative.export_id = "exp-rel";
  relative.destination_path = "out/a.jpg";
  EXPECT(queue.enqueue(relative) == pixcake::ResultCode::StateConflict);
  auto slash = job;
  slash.export_id = "a/b";
  EXPECT(queue.enqueue(slash) == pixcake::ResultCode::StateConflict);
  EXPECT(queue.events().size() == rejected);

  reach_render(queue, job);
  files.files["/out/a.jpg"] = {9, 9};
  const auto occupied = queue.events().size();
  EXPECT(queue.commit_product("exp-1", kBytes, files) == pixcake::ResultCode::StateConflict);
  EXPECT(queue.events().size() == occupied);
  EXPECT(files.files["/out/a.jpg"] == std::vector<std::uint8_t>({9, 9}));
  EXPECT(queue.status("exp-1") == "渲染");

  files.files["/out/a.jpg"] = kBytes;
  EXPECT(queue.commit_product("exp-1", kBytes, files) == pixcake::ResultCode::Ok);
  EXPECT(queue.status("exp-1") == "成功");
  EXPECT(files.writes.empty());
  EXPECT(queue.products().size() == 1);
  EXPECT(!queue.products()[0].watermark_free_second);
  EXPECT(queue.products()[0].product_exists);
  const auto done = queue.events().size();
  EXPECT(queue.commit_product("exp-1", kBytes, files) == pixcake::ResultCode::Ok);
  EXPECT(queue.events().size() == done);
  EXPECT(queue.products().size() == 1);
}

void test_disk_full_then_retry_uses_the_snapshot() {
  pixcake::test::FakeFileSystem files;
  auto queue = pixcake::ExportQueue::open("/cache/exports");
  auto job = job_of("exp-1", "orig-1", "/out/a.jpg");
  reach_render(queue, job);
  files.write_error = pixcake::ResultCode::DiskFull;
  const auto before = queue.events().size();
  EXPECT(queue.commit_product("exp-1", kBytes, files) == pixcake::ResultCode::DiskFull);
  EXPECT(queue.events().size() == before + 2);
  EXPECT(queue.status("exp-1") == "失败");
  EXPECT(files.files.find("/out/a.jpg") == files.files.end());

  auto other = job_of("exp-2", "orig-2", "/out/b.jpg");
  files.write_error = pixcake::ResultCode::Ok;
  reach_render(queue, other);
  EXPECT(queue.commit_product("exp-2", kBytes, files) == pixcake::ResultCode::Ok);
  std::vector<std::uint8_t> written;
  EXPECT(files.read_file("/out/b.jpg", written).ok);
  EXPECT(written == kBytes);

  job.settings_snapshot = "quality=0";
  EXPECT(queue.retry("exp-1") == pixcake::ResultCode::Ok);
  pixcake::ExportJob stored;
  EXPECT(queue.find_job("exp-1", stored));
  EXPECT(stored.settings_snapshot == "quality=8");
  EXPECT(stored.document_revision == 7);
  EXPECT(queue.prepare("exp-1") == pixcake::ResultCode::Ok);
  EXPECT(queue.start_rendering("exp-1") == pixcake::ResultCode::Ok);
  EXPECT(queue.commit_product("exp-1", kBytes, files) == pixcake::ResultCode::Ok);
  EXPECT(queue.status("exp-1") == "成功");
}

void test_rename_failure_and_crash_points() {
  pixcake::test::FakeFileSystem files;
  auto queue = pixcake::ExportQueue::open("/cache/exports");
  reach_render(queue, job_of("exp-1", "orig-1", "/out/a.jpg"));
  files.rename_error = pixcake::ResultCode::IoFailed;
  EXPECT(queue.commit_product("exp-1", kBytes, files) == pixcake::ResultCode::IoFailed);
  EXPECT(queue.status("exp-1") == "提交到目标路径");
  EXPECT(files.files.count("/cache/exports/exp-1") == 1);
  EXPECT(files.files.count("/out/a.jpg") == 0);

  files.rename_error = pixcake::ResultCode::Ok;
  files.files["/out/a.jpg"] = {8};
  EXPECT(queue.recover(files) == pixcake::ResultCode::Ok);
  EXPECT(queue.status("exp-1") == "失败");
  EXPECT(files.files.count("/out/a.jpg") == 0);
  EXPECT(files.files.count("/cache/exports/exp-1") == 0);
  EXPECT(queue.products().empty());

  EXPECT(queue.retry("exp-1") == pixcake::ResultCode::Ok);
  EXPECT(queue.prepare("exp-1") == pixcake::ResultCode::Ok);
  EXPECT(queue.start_rendering("exp-1") == pixcake::ResultCode::Ok);
  files.rename_error = pixcake::ResultCode::IoFailed;
  EXPECT(queue.commit_product("exp-1", kBytes, files) == pixcake::ResultCode::IoFailed);
  files.rename_error = pixcake::ResultCode::Ok;
  EXPECT(queue.recover(files) == pixcake::ResultCode::Ok);
  EXPECT(queue.status("exp-1") == "成功");
  std::vector<std::uint8_t> written;
  EXPECT(files.read_file("/out/a.jpg", written).ok);
  EXPECT(written == kBytes);
  const auto finished = files.writes.size();
  const auto facts = queue.products().size();
  EXPECT(queue.recover(files) == pixcake::ResultCode::Ok);
  EXPECT(files.writes.size() == finished);
  EXPECT(queue.products().size() == facts);

  auto rendering = pixcake::ExportQueue::open("/cache/exports");
  reach_render(rendering, job_of("exp-r", "orig-r", "/out/r.jpg"));
  files.files["/out/r.jpg"] = kBytes;
  EXPECT(rendering.recover(files) == pixcake::ResultCode::Ok);
  EXPECT(rendering.status("exp-r") == "排队");
  EXPECT(files.files["/out/r.jpg"] == kBytes);
  EXPECT(rendering.products().empty());
}

void test_cancel_removes_temp_and_print_is_unavailable() {
  pixcake::test::FakeFileSystem files;
  auto queue = pixcake::ExportQueue::open("/cache/exports");
  auto job = job_of("exp-1", "orig-1", "/out/layout.jpg");
  job.print_requested = true;
  job.settings_snapshot = "copies=2";
  reach_render(queue, job);
  pixcake::ExportJob stored;
  EXPECT(queue.find_job("exp-1", stored));
  EXPECT(stored.settings_snapshot == "copies=2");
  EXPECT(queue.cancel("exp-1") == pixcake::ResultCode::Ok);
  EXPECT(queue.status("exp-1") == "取消");
  EXPECT(files.files.count("/cache/exports/exp-1") == 0);
  EXPECT(files.files.count("/out/layout.jpg") == 0);
  EXPECT(queue.products().empty());
  const auto cancelled = queue.events().size();
  EXPECT(queue.cancel("exp-1") == pixcake::ResultCode::Ok);
  EXPECT(queue.events().size() == cancelled);

  auto printed = pixcake::ExportQueue::open("/cache/exports");
  auto layout = job_of("exp-p", "orig-p", "/out/layout.jpg");
  layout.print_requested = true;
  reach_render(printed, layout);
  EXPECT(printed.commit_product("exp-p", kBytes, files) == pixcake::ResultCode::Ok);
  bool unavailable = false;
  EXPECT(printed.request_print("exp-p", false, unavailable) == pixcake::ResultCode::Ok);
  EXPECT(unavailable);
  std::vector<std::uint8_t> written;
  EXPECT(files.read_file("/out/layout.jpg", written).ok);
  EXPECT(written == kBytes);
  unavailable = true;
  EXPECT(printed.request_print("exp-p", true, unavailable) == pixcake::ResultCode::Ok);
  EXPECT(!unavailable);
  EXPECT(printed.request_print("missing", false, unavailable) == pixcake::ResultCode::StateConflict);
}

void test_product_facts_match_one_settlement() {
  pixcake::test::FakeFileSystem files;
  auto queue = pixcake::ExportQueue::open("/cache/exports");
  auto normal = job_of("exp-a", "orig-1", "/out/a.jpg");
  auto second = job_of("exp-b", "orig-1", "/out/b.jpg");
  second.watermark_free_second = true;
  reach_render(queue, normal);
  EXPECT(queue.commit_product("exp-a", kBytes, files) == pixcake::ResultCode::Ok);
  reach_render(queue, second);
  EXPECT(queue.commit_product("exp-b", kBytes, files) == pixcake::ResultCode::Ok);
  EXPECT(queue.fail("exp-a") == pixcake::ResultCode::StateConflict);

  pixcake::FreeQueuePolicy policy;
  auto ledger = pixcake::CreditLedger::open(policy, {});
  EXPECT(ledger.credit_sheets(2) == pixcake::ResultCode::Ok);
  std::vector<pixcake::ExportProduct> batch;
  for (const auto& fact : queue.products()) {
    batch.push_back({fact.export_id, fact.original_id, fact.watermark_free_second, fact.product_exists});
  }
  EXPECT(ledger.settle_exports(batch) == pixcake::ResultCode::Ok);
  EXPECT(ledger.export_sheets() == 1);
  EXPECT(ledger.settle_exports(batch) == pixcake::ResultCode::Ok);
  EXPECT(ledger.export_sheets() == 1);
}

}  // namespace

int main() {
  test_snapshot_and_fast_export_stay();
  test_commit_once_and_reject_a_different_destination();
  test_disk_full_then_retry_uses_the_snapshot();
  test_rename_failure_and_crash_points();
  test_cancel_removes_temp_and_print_is_unavailable();
  test_product_facts_match_one_settlement();
  if (g_failures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  return 0;
}
```

- [x] **Step 2: Run test to verify it fails**

Run: `cmake -S . -B build && cmake --build build --target test_export_queue`

Expected: 编译失败，因为还没有 `pixcake/exportq/export_queue.hpp`。

- [x] **Step 3: Write minimal implementation**

`kernel/exportq/include/pixcake/exportq/export_queue.hpp`：

```cpp
#pragma once

#include "pixcake/persist/file_system.hpp"
#include "pixcake/support/fast_export_latch.hpp"
#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace pixcake {

struct ExportJob {
  std::string export_id;
  std::string original_id;
  std::uint64_t document_revision = 0;
  std::string destination_path;
  std::string settings_snapshot;
  bool watermark_free_second = false;
  bool print_requested = false;
};

struct ExportEvent {
  std::string kind;
  std::string export_id;
  std::string original_id;
  std::uint64_t document_revision = 0;
  std::string destination_path;
  std::string settings_snapshot;
  bool watermark_free_second = false;
  bool print_requested = false;
  std::uint64_t checksum = 0;
};

struct ExportProductFact {
  std::string export_id;
  std::string original_id;
  bool watermark_free_second = false;
  bool product_exists = false;
};

struct ExportCache {
  bool fast_export = false;
  std::vector<std::pair<std::string, std::string>> statuses;
};

class ExportQueue;

class ExportEventSink {
 public:
  virtual ~ExportEventSink() = default;
  virtual ResultCode persist(const ExportEvent& event, const ExportCache& cache) = 0;
};

// 极速导出不因为队列空闲而自己关闭
class ExportQueue : public FastExportLatch {
 public:
  static ExportQueue open(std::string cache_directory, std::vector<ExportEvent> events = {});

  ExportQueue(const ExportQueue&) = delete;
  ExportQueue& operator=(const ExportQueue&) = delete;
  ExportQueue(ExportQueue&&) noexcept;
  ExportQueue& operator=(ExportQueue&&) noexcept;

  void set_sink(ExportEventSink* sink);

  bool enabled() const override;
  ResultCode set_fast_export(bool on);
  ResultCode enqueue(const ExportJob& job);
  ResultCode prepare(const std::string& export_id);
  ResultCode start_rendering(const std::string& export_id);
  ResultCode commit_product(const std::string& export_id, const std::vector<std::uint8_t>& bytes,
                            FileSystem& files);
  ResultCode fail(const std::string& export_id);
  ResultCode cancel(const std::string& export_id);
  ResultCode retry(const std::string& export_id);
  ResultCode request_print(const std::string& export_id, bool printer_available, bool& unavailable);
  ResultCode recover(FileSystem& files);

  std::string status(const std::string& export_id) const;
  bool find_job(const std::string& export_id, ExportJob& out) const;
  std::vector<ExportProductFact> products() const;
  ExportCache cache() const;
  const std::vector<ExportEvent>& events() const;

 private:
  ExportQueue(std::string cache_directory, std::vector<ExportEvent> events);

  ResultCode note(ExportEvent event);
  ResultCode recover_one(const std::string& export_id, FileSystem& files);
  std::string temp_path(const std::string& export_id) const;

  std::string cache_directory_;
  std::vector<ExportEvent> events_;
  ExportEventSink* sink_ = nullptr;
};

}  // namespace pixcake
```

`kernel/exportq/src/export_queue.cpp` 整文件如下。`kernel/exportq/CMakeLists.txt` 编译这个文件，公开 `include`，并公开链接 `pixcake_persist`、`pixcake_support`、`pixcake_warnings`。

```cpp
#include "pixcake/exportq/export_queue.hpp"

#include "pixcake/support/checksum.hpp"

#include <map>
#include <utility>

namespace pixcake {
namespace {

struct JobView {
  ExportJob job;
  std::string status;
  bool temp_begun = false;
  bool temp_done = false;
  bool commit_begun = false;
  std::uint64_t checksum = 0;
};

struct Fold {
  bool fast_export = false;
  std::map<std::string, JobView> jobs;
  std::vector<ExportProductFact> products;
};

ExportJob job_from(const ExportEvent& event) {
  ExportJob job;
  job.export_id = event.export_id;
  job.original_id = event.original_id;
  job.document_revision = event.document_revision;
  job.destination_path = event.destination_path;
  job.settings_snapshot = event.settings_snapshot;
  job.watermark_free_second = event.watermark_free_second;
  job.print_requested = event.print_requested;
  return job;
}

ExportEvent event_from(const ExportJob& job, std::string kind) {
  ExportEvent event;
  event.kind = std::move(kind);
  event.export_id = job.export_id;
  event.original_id = job.original_id;
  event.document_revision = job.document_revision;
  event.destination_path = job.destination_path;
  event.settings_snapshot = job.settings_snapshot;
  event.watermark_free_second = job.watermark_free_second;
  event.print_requested = job.print_requested;
  return event;
}

Fold fold(const std::vector<ExportEvent>& events) {
  Fold shot;
  for (const auto& event : events) {
    if (event.kind == "fast-on") {
      shot.fast_export = true;
      continue;
    }
    if (event.kind == "fast-off") {
      shot.fast_export = false;
      continue;
    }
    auto& job = shot.jobs[event.export_id];
    if (event.kind == "enqueue") {
      job.job = job_from(event);
      job.status = "排队";
    } else if (event.kind == "prepare") {
      job.status = "准备";
    } else if (event.kind == "render") {
      job.status = "渲染";
    } else if (event.kind == "temp-begin") {
      job.status = "写临时文件";
      job.temp_begun = true;
    } else if (event.kind == "temp-end") {
      job.status = "写临时文件";
      job.temp_done = true;
      job.checksum = event.checksum;
    } else if (event.kind == "commit-begin") {
      job.status = "提交到目标路径";
      job.commit_begun = true;
      job.checksum = event.checksum;
    } else if (event.kind == "commit-end") {
      job.status = "成功";
      ExportProductFact fact;
      fact.export_id = job.job.export_id;
      fact.original_id = job.job.original_id;
      fact.watermark_free_second = job.job.watermark_free_second;
      fact.product_exists = true;
      shot.products.push_back(std::move(fact));
    } else if (event.kind == "fail") {
      job.status = "失败";
    } else if (event.kind == "cancel") {
      job.status = "取消";
    } else if (event.kind == "requeue") {
      job.status = "排队";
      job.temp_begun = false;
      job.temp_done = false;
      job.commit_begun = false;
      job.checksum = 0;
    }
  }
  return shot;
}

bool safe_export_id(const std::string& export_id) {
  return !export_id.empty() && export_id != "." && export_id != ".." &&
         export_id.find('/') == std::string::npos && export_id.find('\\') == std::string::npos &&
         export_id.find('\0') == std::string::npos;
}

bool absolute_destination(const std::string& path) {
  return path.size() > 1 && path.front() == '/' && path.back() != '/' && path.find('\0') == std::string::npos;
}

std::uint64_t checksum_of(const std::vector<std::uint8_t>& bytes) {
  return checksum_fnv1a64(bytes.data(), bytes.size());
}

ResultCode read_checksum(FileSystem& files, const std::string& path, bool& present, std::uint64_t& checksum) {
  present = false;
  if (!files.exists(path)) {
    return ResultCode::Ok;
  }
  std::vector<std::uint8_t> bytes;
  const IoResult read = files.read_file(path, bytes);
  if (!read.ok) {
    return read.not_found ? ResultCode::Ok : read.code;
  }
  present = true;
  checksum = checksum_of(bytes);
  return ResultCode::Ok;
}

ResultCode remove_if_present(FileSystem& files, const std::string& path) {
  if (!files.exists(path)) {
    return ResultCode::Ok;
  }
  const IoResult removed = files.remove_file(path);
  if (!removed.ok && !removed.not_found) {
    return removed.code;
  }
  return ResultCode::Ok;
}

std::string parent_directory(const std::string& path) {
  const auto slash = path.rfind('/');
  if (slash == std::string::npos || slash == 0) {
    return "/";
  }
  return path.substr(0, slash);
}

}  // namespace

ExportQueue::ExportQueue(std::string cache_directory, std::vector<ExportEvent> events)
    : cache_directory_(std::move(cache_directory)), events_(std::move(events)) {}

ExportQueue ExportQueue::open(std::string cache_directory, std::vector<ExportEvent> events) {
  return ExportQueue(std::move(cache_directory), std::move(events));
}

ExportQueue::ExportQueue(ExportQueue&&) noexcept = default;
ExportQueue& ExportQueue::operator=(ExportQueue&&) noexcept = default;

void ExportQueue::set_sink(ExportEventSink* sink) { sink_ = sink; }

const std::vector<ExportEvent>& ExportQueue::events() const { return events_; }

bool ExportQueue::enabled() const { return fold(events_).fast_export; }

std::string ExportQueue::status(const std::string& export_id) const {
  const auto shot = fold(events_);
  const auto found = shot.jobs.find(export_id);
  if (found == shot.jobs.end()) {
    return "";
  }
  return found->second.status;
}

bool ExportQueue::find_job(const std::string& export_id, ExportJob& out) const {
  const auto shot = fold(events_);
  const auto found = shot.jobs.find(export_id);
  if (found == shot.jobs.end() || found->second.job.export_id.empty()) {
    return false;
  }
  out = found->second.job;
  return true;
}

std::vector<ExportProductFact> ExportQueue::products() const { return fold(events_).products; }

ExportCache ExportQueue::cache() const {
  ExportCache cache;
  const auto shot = fold(events_);
  cache.fast_export = shot.fast_export;
  for (const auto& item : shot.jobs) {
    if (!item.second.job.export_id.empty()) {
      cache.statuses.emplace_back(item.first, item.second.status);
    }
  }
  return cache;
}

std::string ExportQueue::temp_path(const std::string& export_id) const {
  return cache_directory_ + "/" + export_id;
}

ResultCode ExportQueue::note(ExportEvent event) {
  events_.push_back(std::move(event));
  if (sink_ == nullptr) {
    return ResultCode::Ok;
  }
  const ResultCode persisted = sink_->persist(events_.back(), cache());
  if (persisted != ResultCode::Ok) {
    events_.pop_back();
    return persisted;
  }
  return ResultCode::Ok;
}

ResultCode ExportQueue::set_fast_export(bool on) {
  if (enabled() == on) {
    return ResultCode::Ok;
  }
  ExportEvent event;
  event.kind = on ? "fast-on" : "fast-off";
  return note(std::move(event));
}

ResultCode ExportQueue::enqueue(const ExportJob& job) {
  if (!safe_export_id(job.export_id) || job.original_id.empty() || job.original_id.find('\0') != std::string::npos ||
      !absolute_destination(job.destination_path)) {
    return ResultCode::StateConflict;
  }
  if (fold(events_).jobs.find(job.export_id) != fold(events_).jobs.end()) {
    return ResultCode::Ok;
  }
  return note(event_from(job, "enqueue"));
}

ResultCode ExportQueue::prepare(const std::string& export_id) {
  const auto shot = fold(events_);
  const auto found = shot.jobs.find(export_id);
  if (found == shot.jobs.end()) {
    return ResultCode::StateConflict;
  }
  if (found->second.status == "准备") {
    return ResultCode::Ok;
  }
  if (found->second.status != "排队") {
    return ResultCode::StateConflict;
  }
  ExportEvent event;
  event.kind = "prepare";
  event.export_id = export_id;
  return note(std::move(event));
}

ResultCode ExportQueue::start_rendering(const std::string& export_id) {
  const auto shot = fold(events_);
  const auto found = shot.jobs.find(export_id);
  if (found == shot.jobs.end()) {
    return ResultCode::StateConflict;
  }
  if (found->second.status == "渲染") {
    return ResultCode::Ok;
  }
  if (found->second.status != "准备") {
    return ResultCode::StateConflict;
  }
  ExportEvent event;
  event.kind = "render";
  event.export_id = export_id;
  return note(std::move(event));
}

ResultCode ExportQueue::fail(const std::string& export_id) {
  const auto shot = fold(events_);
  const auto found = shot.jobs.find(export_id);
  if (found == shot.jobs.end()) {
    return ResultCode::StateConflict;
  }
  if (found->second.status == "失败") {
    return ResultCode::Ok;
  }
  if (found->second.status != "排队" && found->second.status != "准备" && found->second.status != "渲染") {
    return ResultCode::StateConflict;
  }
  ExportEvent event;
  event.kind = "fail";
  event.export_id = export_id;
  return note(std::move(event));
}

ResultCode ExportQueue::retry(const std::string& export_id) {
  const auto shot = fold(events_);
  const auto found = shot.jobs.find(export_id);
  if (found == shot.jobs.end()) {
    return ResultCode::StateConflict;
  }
  if (found->second.status == "排队") {
    return ResultCode::Ok;
  }
  if (found->second.status != "失败") {
    return ResultCode::StateConflict;
  }
  ExportEvent event;
  event.kind = "requeue";
  event.export_id = export_id;
  return note(std::move(event));
}

ResultCode ExportQueue::request_print(const std::string& export_id, bool printer_available, bool& unavailable) {
  ExportJob job;
  if (!find_job(export_id, job) || !job.print_requested) {
    return ResultCode::StateConflict;
  }
  unavailable = !printer_available;
  return ResultCode::Ok;
}

ResultCode ExportQueue::cancel(const std::string& export_id) {
  const auto shot = fold(events_);
  const auto found = shot.jobs.find(export_id);
  if (found == shot.jobs.end()) {
    return ResultCode::StateConflict;
  }
  if (found->second.status == "取消") {
    return ResultCode::Ok;
  }
  if (found->second.commit_begun ||
      (found->second.status != "排队" && found->second.status != "准备" && found->second.status != "渲染")) {
    return ResultCode::StateConflict;
  }
  ExportEvent event;
  event.kind = "cancel";
  event.export_id = export_id;
  return note(std::move(event));
}

ResultCode ExportQueue::commit_product(const std::string& export_id, const std::vector<std::uint8_t>& bytes,
                                       FileSystem& files) {
  const auto shot = fold(events_);
  const auto found = shot.jobs.find(export_id);
  if (found == shot.jobs.end() || bytes.empty()) {
    return ResultCode::StateConflict;
  }
  if (found->second.status == "成功") {
    return ResultCode::Ok;
  }
  if (found->second.status != "渲染") {
    return ResultCode::StateConflict;
  }
  const ExportJob& job = found->second.job;
  const std::uint64_t checksum = checksum_of(bytes);
  bool destination_present = false;
  std::uint64_t destination_checksum = 0;
  const ResultCode looked = read_checksum(files, job.destination_path, destination_present, destination_checksum);
  if (looked != ResultCode::Ok) {
    return looked;
  }
  if (destination_present && destination_checksum != checksum) {
    return ResultCode::StateConflict;
  }
  if (destination_present) {
    ExportEvent begin = event_from(job, "commit-begin");
    begin.checksum = checksum;
    const ResultCode began = note(std::move(begin));
    if (began != ResultCode::Ok) {
      return began;
    }
    ExportEvent end = event_from(job, "commit-end");
    end.checksum = checksum;
    return note(std::move(end));
  }

  const IoResult directory = files.create_directory(cache_directory_);
  if (!directory.ok) {
    return directory.code;
  }
  ExportEvent temp_begin = event_from(job, "temp-begin");
  const ResultCode began_temp = note(std::move(temp_begin));
  if (began_temp != ResultCode::Ok) {
    return began_temp;
  }
  const std::string temp = temp_path(export_id);
  const ResultCode cleared = remove_if_present(files, temp);
  if (cleared != ResultCode::Ok) {
    return cleared;
  }
  const IoResult written = files.write_new(temp, bytes);
  if (!written.ok) {
    remove_if_present(files, temp);
    ExportEvent failed = event_from(job, "fail");
    const ResultCode noted = note(std::move(failed));
    return noted == ResultCode::Ok ? written.code : noted;
  }
  const IoResult synced = files.sync_file(temp);
  if (!synced.ok) {
    remove_if_present(files, temp);
    ExportEvent failed = event_from(job, "fail");
    const ResultCode noted = note(std::move(failed));
    return noted == ResultCode::Ok ? synced.code : noted;
  }
  bool temp_present = false;
  std::uint64_t temp_checksum = 0;
  const ResultCode reread = read_checksum(files, temp, temp_present, temp_checksum);
  if (reread != ResultCode::Ok || !temp_present || temp_checksum != checksum) {
    remove_if_present(files, temp);
    ExportEvent failed = event_from(job, "fail");
    const ResultCode noted = note(std::move(failed));
    return noted == ResultCode::Ok ? ResultCode::IoFailed : noted;
  }
  ExportEvent temp_end = event_from(job, "temp-end");
  temp_end.checksum = checksum;
  const ResultCode ended = note(std::move(temp_end));
  if (ended != ResultCode::Ok) {
    return ended;
  }
  ExportEvent commit_begin = event_from(job, "commit-begin");
  commit_begin.checksum = checksum;
  const ResultCode commit_noted = note(std::move(commit_begin));
  if (commit_noted != ResultCode::Ok) {
    return commit_noted;
  }
  const IoResult renamed = files.rename_file(temp, job.destination_path);
  if (!renamed.ok) {
    return renamed.code;
  }
  const IoResult directory_synced = files.sync_directory(parent_directory(job.destination_path));
  if (!directory_synced.ok) {
    return directory_synced.code;
  }
  bool committed_present = false;
  std::uint64_t committed_checksum = 0;
  const ResultCode verified = read_checksum(files, job.destination_path, committed_present, committed_checksum);
  if (verified != ResultCode::Ok || !committed_present || committed_checksum != checksum) {
    remove_if_present(files, job.destination_path);
    ExportEvent failed = event_from(job, "fail");
    const ResultCode noted = note(std::move(failed));
    return noted == ResultCode::Ok ? ResultCode::IoFailed : noted;
  }
  ExportEvent commit_end = event_from(job, "commit-end");
  commit_end.checksum = checksum;
  return note(std::move(commit_end));
}

ResultCode ExportQueue::recover(FileSystem& files) {
  const IoResult directory = files.create_directory(cache_directory_);
  if (!directory.ok) {
    return directory.code;
  }
  std::vector<std::string> ids;
  for (const auto& item : fold(events_).jobs) {
    ids.push_back(item.first);
  }
  ResultCode first = ResultCode::Ok;
  for (const auto& export_id : ids) {
    const ResultCode code = recover_one(export_id, files);
    if (code != ResultCode::Ok && first == ResultCode::Ok) {
      first = code;
    }
  }
  return first;
}

ResultCode ExportQueue::recover_one(const std::string& export_id, FileSystem& files) {
  const auto shot = fold(events_);
  const auto found = shot.jobs.find(export_id);
  if (found == shot.jobs.end()) {
    return ResultCode::Ok;
  }
  const JobView& job = found->second;
  if (job.status == "成功" || job.status == "失败" || job.status == "取消" || job.status == "排队") {
    return ResultCode::Ok;
  }
  if (job.status == "准备" || job.status == "渲染") {
    ExportEvent event;
    event.kind = "requeue";
    event.export_id = export_id;
    return note(std::move(event));
  }
  const std::string temp = temp_path(export_id);
  if (!job.commit_begun) {
    if (!job.temp_done) {
      const ResultCode removed = remove_if_present(files, temp);
      if (removed != ResultCode::Ok) {
        return removed;
      }
      ExportEvent failed;
      failed.kind = "fail";
      failed.export_id = export_id;
      return note(std::move(failed));
    }
    bool destination_present = false;
    std::uint64_t existing_checksum = 0;
    const ResultCode existing = read_checksum(files, job.job.destination_path, destination_present, existing_checksum);
    if (existing != ResultCode::Ok) {
      return existing;
    }
    if (destination_present) {
      remove_if_present(files, temp);
      ExportEvent failed;
      failed.kind = "fail";
      failed.export_id = export_id;
      return note(std::move(failed));
    }
    ExportEvent begin = event_from(job.job, "commit-begin");
    begin.checksum = job.checksum;
    const ResultCode began = note(std::move(begin));
    if (began != ResultCode::Ok) {
      return began;
    }
    return recover_one(export_id, files);
  }
  bool present = false;
  std::uint64_t destination_checksum = 0;
  const ResultCode looked = read_checksum(files, job.job.destination_path, present, destination_checksum);
  if (looked != ResultCode::Ok) {
    return looked;
  }
  if (present && destination_checksum != job.checksum) {
    const ResultCode removed = remove_if_present(files, job.job.destination_path);
    if (removed != ResultCode::Ok) {
      return removed;
    }
    remove_if_present(files, temp);
    ExportEvent failed;
    failed.kind = "fail";
    failed.export_id = export_id;
    return note(std::move(failed));
  }
  if (!present) {
    const IoResult renamed = files.rename_file(temp, job.job.destination_path);
    if (!renamed.ok) {
      return renamed.code;
    }
    const ResultCode verified = read_checksum(files, job.job.destination_path, present, destination_checksum);
    if (verified != ResultCode::Ok) {
      return verified;
    }
    if (!present || destination_checksum != job.checksum) {
      remove_if_present(files, job.job.destination_path);
      ExportEvent failed;
      failed.kind = "fail";
      failed.export_id = export_id;
      return note(std::move(failed));
    }
  } else {
    remove_if_present(files, temp);
  }
  const IoResult directory_synced = files.sync_directory(parent_directory(job.job.destination_path));
  if (!directory_synced.ok) {
    return directory_synced.code;
  }
  ExportEvent end = event_from(job.job, "commit-end");
  end.checksum = job.checksum;
  return note(std::move(end));
}

}  // namespace pixcake
```

`commit_product` 在目标已有相同字节时不调用 `write_new`。磁盘满时先记 `temp-begin`，写入失败后再记 `fail`，所以事件增加 2 条，目标路径不出现。换名失败时已经记了 `temp-begin`、`temp-end`、`commit-begin`，状态停在「提交到目标路径」，临时文件还在，函数返回换名的错误码，不记 `fail`。`recover` 在还没有 `temp-end` 时删除临时文件并记 `fail`，不碰目标路径上已有的文件。已经有 `temp-end` 但还没有 `commit-begin` 时，先补开始事件，再换名；目标路径上如果已经有文件，不把它认作成品，也不删除，任务记 `fail` 并删掉临时文件。已经有 `commit-begin` 时，目标字节和开始事件的校验和不同就删掉目标文件和临时文件并记 `fail`。校验和相同，或换名成功后校验通过，才记 `commit-end`。准备或渲染中恢复只记 `requeue`。

- [x] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_export_queue && ctest --test-dir build -R '^test_export_queue$' --output-on-failure`

Expected: `test_export_queue` 通过。

- [x] **Step 5: Commit**

```bash
git add kernel/exportq tests/kernel/exportq CMakeLists.txt tests/CMakeLists.txt tests/arch/check_dependencies.cmake docs/superpowers/plans/2026-11-03-kernel-export-queue.md
git commit -m "$(cat <<'EOF'
feat: commit export files from a queued snapshot

EOF
)"
```

执行时只在本地提交，不推远端，不建 Pull Request。

---

### Task 2: 重放导出事件并接着换名

**Files:**
- Create: `kernel/exportq/include/pixcake/exportq/export_book.hpp`
- Create: `kernel/exportq/src/export_book.cpp`
- Create: `storage/sqlite/include/pixcake/storage/sqlite_export_log.hpp`
- Create: `storage/sqlite/src/sqlite_export_log.cpp`
- Modify: `kernel/exportq/CMakeLists.txt`
- Modify: `storage/sqlite/CMakeLists.txt`
- Modify: `tests/kernel/exportq/test_export_queue.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `ExportQueue`、`ExportEvent`、`ExportCache`、`ExportEventSink`
- Produces: `ExportEventLog`、`ExportBook`、`SqliteExportLog`

- [x] **Step 1: Write the failing test**

在 `test_export_queue.cpp` 增加下面的测试，并从 `main` 调用。测试增加 `#include "pixcake/exportq/export_book.hpp"`、`#include "pixcake/storage/sqlite_export_log.hpp"`、`#include <sqlite3.h>`、`#include <memory>`、`#include "support/temp_dir.hpp"`。链接改为 `pixcake_exportq pixcake_storage pixcake_command pixcake_ledger pixcake_warnings`。

```cpp
void test_reopen_repairs_cache_and_finishes_rename() {
  pixcake::test::TempDir dir;
  const std::string path = dir.path() + "/export.db";
  pixcake::test::FakeFileSystem files;
  {
    std::unique_ptr<pixcake::SqliteExportLog> log;
    EXPECT(pixcake::SqliteExportLog::open(path, log) == pixcake::ResultCode::Ok);
    std::unique_ptr<pixcake::ExportBook> book;
    EXPECT(pixcake::ExportBook::open("/cache/exports", *log, files, book) == pixcake::ResultCode::Ok);
    EXPECT(book->queue().set_fast_export(true) == pixcake::ResultCode::Ok);
    auto job = job_of("exp-1", "orig-1", "/out/a.jpg");
    reach_render(book->queue(), job);
    files.rename_error = pixcake::ResultCode::IoFailed;
    EXPECT(book->queue().commit_product("exp-1", kBytes, files) == pixcake::ResultCode::IoFailed);
    EXPECT(book->queue().status("exp-1") == "提交到目标路径");
  }

  sqlite3* raw = nullptr;
  EXPECT(sqlite3_open(path.c_str(), &raw) == SQLITE_OK);
  EXPECT(sqlite3_exec(raw, "UPDATE export_cache SET fast_export = 0 WHERE id = 1;", nullptr, nullptr, nullptr) ==
         SQLITE_OK);
  EXPECT(sqlite3_exec(raw, "UPDATE export_task_cache SET status = '排队' WHERE export_id = 'exp-1';", nullptr,
                      nullptr, nullptr) == SQLITE_OK);
  sqlite3_close(raw);

  files.rename_error = pixcake::ResultCode::Ok;
  std::unique_ptr<pixcake::SqliteExportLog> reopened_log;
  EXPECT(pixcake::SqliteExportLog::open(path, reopened_log) == pixcake::ResultCode::Ok);
  std::unique_ptr<pixcake::ExportBook> reopened;
  EXPECT(pixcake::ExportBook::open("/cache/exports", *reopened_log, files, reopened) == pixcake::ResultCode::Ok);
  EXPECT(reopened->queue().enabled());
  EXPECT(reopened->queue().status("exp-1") == "成功");
  std::vector<std::uint8_t> written;
  EXPECT(files.read_file("/out/a.jpg", written).ok);
  EXPECT(written == kBytes);

  sqlite3_stmt* cache = nullptr;
  EXPECT(sqlite3_open(path.c_str(), &raw) == SQLITE_OK);
  EXPECT(sqlite3_prepare_v2(raw, "SELECT fast_export FROM export_cache WHERE id = 1;", -1, &cache, nullptr) ==
         SQLITE_OK);
  EXPECT(sqlite3_step(cache) == SQLITE_ROW);
  EXPECT(sqlite3_column_int(cache, 0) == 1);
  sqlite3_finalize(cache);
  sqlite3_stmt* task = nullptr;
  EXPECT(sqlite3_prepare_v2(raw, "SELECT status FROM export_task_cache WHERE export_id = ?;", -1, &task, nullptr) ==
         SQLITE_OK);
  EXPECT(sqlite3_bind_text(task, 1, "exp-1", -1, SQLITE_TRANSIENT) == SQLITE_OK);
  EXPECT(sqlite3_step(task) == SQLITE_ROW);
  EXPECT(std::string(reinterpret_cast<const char*>(sqlite3_column_text(task, 0))) == "成功");
  sqlite3_finalize(task);
  sqlite3_close(raw);

  pixcake::test::TempDir future_dir;
  const std::string future_path = future_dir.path() + "/export.db";
  EXPECT(sqlite3_open(future_path.c_str(), &raw) == SQLITE_OK);
  EXPECT(sqlite3_exec(raw, "PRAGMA user_version=2;", nullptr, nullptr, nullptr) == SQLITE_OK);
  sqlite3_close(raw);
  std::unique_ptr<pixcake::SqliteExportLog> future;
  EXPECT(pixcake::SqliteExportLog::open(future_path, future) == pixcake::ResultCode::StateConflict);
  EXPECT(future == nullptr);
  EXPECT(sqlite3_open(future_path.c_str(), &raw) == SQLITE_OK);
  sqlite3_stmt* version = nullptr;
  EXPECT(sqlite3_prepare_v2(raw, "PRAGMA user_version;", -1, &version, nullptr) == SQLITE_OK);
  EXPECT(sqlite3_step(version) == SQLITE_ROW);
  EXPECT(sqlite3_column_int(version, 0) == 2);
  sqlite3_finalize(version);
  sqlite3_stmt* tables = nullptr;
  EXPECT(sqlite3_prepare_v2(raw, "SELECT count(*) FROM sqlite_master WHERE type = 'table' AND name = 'export_event';",
                            -1, &tables, nullptr) == SQLITE_OK);
  EXPECT(sqlite3_step(tables) == SQLITE_ROW);
  EXPECT(sqlite3_column_int(tables, 0) == 0);
  sqlite3_finalize(tables);
  sqlite3_close(raw);
}
```

- [x] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_export_queue`

Expected: 编译失败，因为还没有 `ExportBook`。

- [x] **Step 3: Write minimal implementation**

`ExportEventLog` 提供 `load`、`append`、`read_cache`、`write_cache`、`in_transaction`。没有 `export_cache` 行时 `read_cache` 成功并给出空 optional。

`ExportBook::open` 先读出全部事件交给 `ExportQueue::open`。缓存缺失，或极速开关、任务状态和 `cache()` 不同：用折叠结果覆盖缓存。这一步失败则不恢复文件，也不交出账本。然后 `set_sink`，再 `recover`。`recover` 每记一条事件都立刻通过 sink 落盘。恢复失败则 `open` 失败，不交出这本书；已经落盘的事件保留，下次打开继续恢复。

`ExportBook` 的 sink 把这一条事件和当时的 `cache()` 放进同一个事务。事务失败时内存里的这条事件被 `note` 弹出，文件步骤还没开始。

新库执行：

```sql
CREATE TABLE export_event (
  seq INTEGER PRIMARY KEY,
  kind TEXT NOT NULL,
  export_id TEXT NOT NULL,
  original_id TEXT NOT NULL,
  document_revision INTEGER NOT NULL,
  destination_path TEXT NOT NULL,
  settings_snapshot TEXT NOT NULL,
  watermark_free_second INTEGER NOT NULL,
  print_requested INTEGER NOT NULL,
  checksum TEXT NOT NULL
);
CREATE TABLE export_cache (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  fast_export INTEGER NOT NULL
);
CREATE TABLE export_task_cache (
  export_id TEXT PRIMARY KEY,
  status TEXT NOT NULL
);
```

`checksum` 用 16 位小写十六进制保存 64 位校验和，读写都按这个宽度转换，不用带符号整数截断。文本只用 `sqlite3_bind_text`。版本检查在 `PRAGMA journal_mode` 和 `CREATE TABLE` 之前。`user_version` 不是 0 也不是 1 时直接关掉库并返回状态冲突。

`kernel/exportq` 仍不包含 `sqlite3.h`。SQLite 只出现在 `storage/sqlite/src/sqlite_export_log.cpp`。`pixcake_exportq` 增加 `src/export_book.cpp`。`pixcake_storage` 增加这个源文件，并额外公开链接 `pixcake_exportq`。不改 `sqlite_project_store.cpp`，也不改账本日志。

`export_book.hpp`：

```cpp
#pragma once

#include "pixcake/exportq/export_queue.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace pixcake {

class ExportEventLog {
 public:
  virtual ~ExportEventLog() = default;
  virtual ResultCode load(std::vector<ExportEvent>& out) = 0;
  virtual ResultCode append(const std::vector<ExportEvent>& events) = 0;
  virtual ResultCode read_cache(std::optional<ExportCache>& out) = 0;
  virtual ResultCode write_cache(const ExportCache& cache) = 0;
  virtual ResultCode in_transaction(const std::function<ResultCode()>& body) = 0;
};

class ExportBook {
 public:
  static ResultCode open(std::string cache_directory, ExportEventLog& log, FileSystem& files,
                         std::unique_ptr<ExportBook>& out);
  ExportQueue& queue();

 private:
  struct Sink : ExportEventSink {
    explicit Sink(ExportEventLog& log) : log(log) {}
    ResultCode persist(const ExportEvent& event, const ExportCache& cache) override;
    ExportEventLog& log;
  };

  ExportBook(ExportEventLog& log, ExportQueue queue);
  ExportEventLog& log_;
  Sink sink_;
  ExportQueue queue_;
};

}  // namespace pixcake
```

`export_book.cpp`：

```cpp
#include "pixcake/exportq/export_book.hpp"

#include <utility>

namespace pixcake {

ResultCode ExportBook::Sink::persist(const ExportEvent& event, const ExportCache& cache) {
  return log.in_transaction([&] {
    const ResultCode appended = log.append({event});
    if (appended != ResultCode::Ok) {
      return appended;
    }
    return log.write_cache(cache);
  });
}

ExportBook::ExportBook(ExportEventLog& log, ExportQueue queue)
    : log_(log), sink_(log_), queue_(std::move(queue)) {
  queue_.set_sink(&sink_);
}

ExportQueue& ExportBook::queue() { return queue_; }

ResultCode ExportBook::open(std::string cache_directory, ExportEventLog& log, FileSystem& files,
                            std::unique_ptr<ExportBook>& out) {
  out.reset();
  std::vector<ExportEvent> events;
  const ResultCode loaded = log.load(events);
  if (loaded != ResultCode::Ok) {
    return loaded;
  }
  ExportQueue queue = ExportQueue::open(std::move(cache_directory), std::move(events));
  std::optional<ExportCache> cache;
  const ResultCode read = log.read_cache(cache);
  if (read != ResultCode::Ok) {
    return read;
  }
  const ExportCache folded = queue.cache();
  if (!cache || cache->fast_export != folded.fast_export || cache->statuses != folded.statuses) {
    const ResultCode repaired = log.in_transaction([&] { return log.write_cache(folded); });
    if (repaired != ResultCode::Ok) {
      return repaired;
    }
  }
  auto book = std::unique_ptr<ExportBook>(new ExportBook(log, std::move(queue)));
  const ResultCode recovered = book->queue_.recover(files);
  if (recovered != ResultCode::Ok) {
    return recovered;
  }
  out = std::move(book);
  return ResultCode::Ok;
}

}  // namespace pixcake
```

`storage/sqlite/include/pixcake/storage/sqlite_export_log.hpp`：

```cpp
#pragma once

#include "pixcake/exportq/export_book.hpp"

#include <memory>
#include <string>

namespace pixcake {

class SqliteExportLog : public ExportEventLog {
 public:
  static ResultCode open(const std::string& path, std::unique_ptr<SqliteExportLog>& out);
  ~SqliteExportLog() override;

  ResultCode load(std::vector<ExportEvent>& out) override;
  ResultCode append(const std::vector<ExportEvent>& events) override;
  ResultCode read_cache(std::optional<ExportCache>& out) override;
  ResultCode write_cache(const ExportCache& cache) override;
  ResultCode in_transaction(const std::function<ResultCode()>& body) override;

 private:
  struct Impl;
  explicit SqliteExportLog(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace pixcake
```

`storage/sqlite/src/sqlite_export_log.cpp` 整文件如下。版本检查在 `PRAGMA journal_mode` 和 `CREATE TABLE` 之前。`checksum` 用 16 位小写十六进制。

```cpp
#include "pixcake/storage/sqlite_export_log.hpp"

#include <sqlite3.h>

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>

namespace pixcake {
namespace {

constexpr const char* kSchema =
    "CREATE TABLE export_event ("
    "seq INTEGER PRIMARY KEY,"
    "kind TEXT NOT NULL,"
    "export_id TEXT NOT NULL,"
    "original_id TEXT NOT NULL,"
    "document_revision INTEGER NOT NULL,"
    "destination_path TEXT NOT NULL,"
    "settings_snapshot TEXT NOT NULL,"
    "watermark_free_second INTEGER NOT NULL,"
    "print_requested INTEGER NOT NULL,"
    "checksum TEXT NOT NULL);"
    "CREATE TABLE export_cache ("
    "id INTEGER PRIMARY KEY CHECK (id = 1),"
    "fast_export INTEGER NOT NULL);"
    "CREATE TABLE export_task_cache ("
    "export_id TEXT PRIMARY KEY,"
    "status TEXT NOT NULL);";

ResultCode map_sqlite(int rc) {
  if (rc == SQLITE_OK || rc == SQLITE_DONE || rc == SQLITE_ROW) {
    return ResultCode::Ok;
  }
  if (rc == SQLITE_FULL) {
    return ResultCode::DiskFull;
  }
  if (rc == SQLITE_PERM || rc == SQLITE_READONLY || rc == SQLITE_AUTH) {
    return ResultCode::PermissionDenied;
  }
  if (rc == SQLITE_CONSTRAINT) {
    return ResultCode::StateConflict;
  }
  return ResultCode::IoFailed;
}

ResultCode exec_sql(sqlite3* db, const char* sql) {
  char* error = nullptr;
  const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &error);
  if (error != nullptr) {
    sqlite3_free(error);
  }
  return map_sqlite(rc);
}

class Statement {
 public:
  Statement() = default;
  Statement(const Statement&) = delete;
  Statement& operator=(const Statement&) = delete;
  ~Statement() { reset(); }

  ResultCode prepare(sqlite3* db, const char* sql) {
    reset();
    return map_sqlite(sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr));
  }

  sqlite3_stmt* get() const { return stmt_; }

  void reset() {
    if (stmt_ != nullptr) {
      sqlite3_finalize(stmt_);
      stmt_ = nullptr;
    }
  }

 private:
  sqlite3_stmt* stmt_ = nullptr;
};

ResultCode bind_text(sqlite3_stmt* stmt, int index, const std::string& value) {
  if (value.size() > static_cast<std::size_t>(INT_MAX)) {
    return ResultCode::IoFailed;
  }
  return map_sqlite(sqlite3_bind_text(stmt, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT));
}

std::string column_text(sqlite3_stmt* stmt, int index) {
  const auto* text = sqlite3_column_text(stmt, index);
  if (text == nullptr) {
    return {};
  }
  const int size = sqlite3_column_bytes(stmt, index);
  return std::string(reinterpret_cast<const char*>(text), static_cast<std::size_t>(size));
}

std::string hex16(std::uint64_t value) {
  char buffer[17];
  std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(value));
  return buffer;
}

std::uint64_t parse_hex16(const std::string& text) {
  if (text.size() != 16) {
    return 0;
  }
  return std::strtoull(text.c_str(), nullptr, 16);
}

ResultCode read_user_version(sqlite3* db, int& version) {
  Statement stmt;
  const ResultCode prepared = stmt.prepare(db, "PRAGMA user_version;");
  if (prepared != ResultCode::Ok) {
    return prepared;
  }
  if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
    return ResultCode::IoFailed;
  }
  version = sqlite3_column_int(stmt.get(), 0);
  return ResultCode::Ok;
}

ResultCode create_schema(sqlite3* db) {
  const ResultCode began = exec_sql(db, "BEGIN IMMEDIATE;");
  if (began != ResultCode::Ok) {
    return began;
  }
  const ResultCode schema = exec_sql(db, kSchema);
  if (schema != ResultCode::Ok) {
    exec_sql(db, "ROLLBACK;");
    return schema;
  }
  const ResultCode stamp = exec_sql(db, "PRAGMA user_version=1;");
  if (stamp != ResultCode::Ok) {
    exec_sql(db, "ROLLBACK;");
    return stamp;
  }
  const ResultCode committed = exec_sql(db, "COMMIT;");
  if (committed != ResultCode::Ok) {
    exec_sql(db, "ROLLBACK;");
    return committed;
  }
  return ResultCode::Ok;
}

}  // namespace

struct SqliteExportLog::Impl {
  sqlite3* db = nullptr;
  int tx_depth = 0;
};

SqliteExportLog::SqliteExportLog(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

SqliteExportLog::~SqliteExportLog() {
  if (impl_ == nullptr || impl_->db == nullptr) {
    return;
  }
  if (impl_->tx_depth > 0) {
    sqlite3_exec(impl_->db, "ROLLBACK;", nullptr, nullptr, nullptr);
  }
  sqlite3_close(impl_->db);
  impl_->db = nullptr;
}

ResultCode SqliteExportLog::open(const std::string& path, std::unique_ptr<SqliteExportLog>& out) {
  out.reset();
  sqlite3* db = nullptr;
  const int opened = sqlite3_open(path.c_str(), &db);
  if (opened != SQLITE_OK) {
    const ResultCode code = map_sqlite(opened);
    if (db != nullptr) {
      sqlite3_close(db);
    }
    return code == ResultCode::Ok ? ResultCode::IoFailed : code;
  }
  sqlite3_busy_timeout(db, 1000);
  int version = 0;
  const ResultCode version_code = read_user_version(db, version);
  if (version_code != ResultCode::Ok) {
    sqlite3_close(db);
    return version_code;
  }
  if (version != 0 && version != 1) {
    sqlite3_close(db);
    return ResultCode::StateConflict;
  }
  const ResultCode wal = exec_sql(db, "PRAGMA journal_mode=WAL;");
  if (wal != ResultCode::Ok) {
    sqlite3_close(db);
    return wal;
  }
  if (version == 0) {
    const ResultCode schema = create_schema(db);
    if (schema != ResultCode::Ok) {
      sqlite3_close(db);
      return schema;
    }
  }
  auto impl = std::make_unique<Impl>();
  impl->db = db;
  out.reset(new SqliteExportLog(std::move(impl)));
  return ResultCode::Ok;
}

ResultCode SqliteExportLog::load(std::vector<ExportEvent>& out) {
  out.clear();
  Statement stmt;
  const ResultCode prepared = stmt.prepare(
      impl_->db,
      "SELECT kind, export_id, original_id, document_revision, destination_path, settings_snapshot, "
      "watermark_free_second, print_requested, checksum FROM export_event ORDER BY seq;");
  if (prepared != ResultCode::Ok) {
    return prepared;
  }
  int stepped = SQLITE_DONE;
  while ((stepped = sqlite3_step(stmt.get())) == SQLITE_ROW) {
    ExportEvent event;
    event.kind = column_text(stmt.get(), 0);
    event.export_id = column_text(stmt.get(), 1);
    event.original_id = column_text(stmt.get(), 2);
    event.document_revision = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.get(), 3));
    event.destination_path = column_text(stmt.get(), 4);
    event.settings_snapshot = column_text(stmt.get(), 5);
    event.watermark_free_second = sqlite3_column_int(stmt.get(), 6) != 0;
    event.print_requested = sqlite3_column_int(stmt.get(), 7) != 0;
    event.checksum = parse_hex16(column_text(stmt.get(), 8));
    out.push_back(std::move(event));
  }
  return stepped == SQLITE_DONE ? ResultCode::Ok : ResultCode::IoFailed;
}

ResultCode SqliteExportLog::append(const std::vector<ExportEvent>& events) {
  Statement stmt;
  const ResultCode prepared = stmt.prepare(
      impl_->db,
      "INSERT INTO export_event(kind, export_id, original_id, document_revision, destination_path, "
      "settings_snapshot, watermark_free_second, print_requested, checksum) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);");
  if (prepared != ResultCode::Ok) {
    return prepared;
  }
  for (const auto& event : events) {
    if (event.document_revision > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
      return ResultCode::IoFailed;
    }
    sqlite3_reset(stmt.get());
    sqlite3_clear_bindings(stmt.get());
    if (const ResultCode code = bind_text(stmt.get(), 1, event.kind); code != ResultCode::Ok) {
      return code;
    }
    if (const ResultCode code = bind_text(stmt.get(), 2, event.export_id); code != ResultCode::Ok) {
      return code;
    }
    if (const ResultCode code = bind_text(stmt.get(), 3, event.original_id); code != ResultCode::Ok) {
      return code;
    }
    if (const ResultCode code = map_sqlite(sqlite3_bind_int64(
            stmt.get(), 4, static_cast<sqlite3_int64>(event.document_revision)));
        code != ResultCode::Ok) {
      return code;
    }
    if (const ResultCode code = bind_text(stmt.get(), 5, event.destination_path); code != ResultCode::Ok) {
      return code;
    }
    if (const ResultCode code = bind_text(stmt.get(), 6, event.settings_snapshot); code != ResultCode::Ok) {
      return code;
    }
    if (const ResultCode code = map_sqlite(sqlite3_bind_int(stmt.get(), 7, event.watermark_free_second ? 1 : 0));
        code != ResultCode::Ok) {
      return code;
    }
    if (const ResultCode code = map_sqlite(sqlite3_bind_int(stmt.get(), 8, event.print_requested ? 1 : 0));
        code != ResultCode::Ok) {
      return code;
    }
    if (const ResultCode code = bind_text(stmt.get(), 9, hex16(event.checksum)); code != ResultCode::Ok) {
      return code;
    }
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
      return ResultCode::IoFailed;
    }
  }
  return ResultCode::Ok;
}

ResultCode SqliteExportLog::read_cache(std::optional<ExportCache>& out) {
  out.reset();
  Statement cache;
  const ResultCode prepared = cache.prepare(impl_->db, "SELECT fast_export FROM export_cache WHERE id = 1;");
  if (prepared != ResultCode::Ok) {
    return prepared;
  }
  const int cache_row = sqlite3_step(cache.get());
  if (cache_row == SQLITE_DONE) {
    return ResultCode::Ok;
  }
  if (cache_row != SQLITE_ROW) {
    return ResultCode::IoFailed;
  }
  ExportCache value;
  value.fast_export = sqlite3_column_int(cache.get(), 0) != 0;
  Statement tasks;
  const ResultCode tasks_prepared =
      tasks.prepare(impl_->db, "SELECT export_id, status FROM export_task_cache ORDER BY export_id;");
  if (tasks_prepared != ResultCode::Ok) {
    return tasks_prepared;
  }
  while (sqlite3_step(tasks.get()) == SQLITE_ROW) {
    value.statuses.emplace_back(column_text(tasks.get(), 0), column_text(tasks.get(), 1));
  }
  out = std::move(value);
  return ResultCode::Ok;
}

ResultCode SqliteExportLog::write_cache(const ExportCache& cache) {
  const ResultCode cleared = exec_sql(impl_->db, "DELETE FROM export_task_cache;");
  if (cleared != ResultCode::Ok) {
    return cleared;
  }
  Statement row;
  const ResultCode prepared = row.prepare(
      impl_->db, "INSERT INTO export_cache(id, fast_export) VALUES (1, ?) ON CONFLICT(id) DO UPDATE SET "
                 "fast_export = excluded.fast_export;");
  if (prepared != ResultCode::Ok) {
    return prepared;
  }
  if (const ResultCode code = map_sqlite(sqlite3_bind_int(row.get(), 1, cache.fast_export ? 1 : 0));
      code != ResultCode::Ok) {
    return code;
  }
  if (sqlite3_step(row.get()) != SQLITE_DONE) {
    return ResultCode::IoFailed;
  }
  Statement task;
  const ResultCode task_prepared =
      task.prepare(impl_->db, "INSERT INTO export_task_cache(export_id, status) VALUES (?, ?);");
  if (task_prepared != ResultCode::Ok) {
    return task_prepared;
  }
  for (const auto& item : cache.statuses) {
    sqlite3_reset(task.get());
    sqlite3_clear_bindings(task.get());
    if (const ResultCode code = bind_text(task.get(), 1, item.first); code != ResultCode::Ok) {
      return code;
    }
    if (const ResultCode code = bind_text(task.get(), 2, item.second); code != ResultCode::Ok) {
      return code;
    }
    if (sqlite3_step(task.get()) != SQLITE_DONE) {
      return ResultCode::IoFailed;
    }
  }
  return ResultCode::Ok;
}

ResultCode SqliteExportLog::in_transaction(const std::function<ResultCode()>& body) {
  const bool outer = impl_->tx_depth == 0;
  if (outer) {
    const ResultCode began = exec_sql(impl_->db, "BEGIN IMMEDIATE;");
    if (began != ResultCode::Ok) {
      return began;
    }
  }
  ++impl_->tx_depth;
  const ResultCode code = body();
  --impl_->tx_depth;
  if (!outer) {
    return code;
  }
  if (code != ResultCode::Ok) {
    exec_sql(impl_->db, "ROLLBACK;");
    return code;
  }
  const ResultCode committed = exec_sql(impl_->db, "COMMIT;");
  if (committed != ResultCode::Ok) {
    exec_sql(impl_->db, "ROLLBACK;");
    return committed;
  }
  return ResultCode::Ok;
}

}  // namespace pixcake
```

- [x] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_export_queue test_project_paths test_ledger && ctest --test-dir build --output-on-failure`

Expected: 全量测试通过。`test_export_queue` 覆盖换名中断后的重开。`test_project_paths` 仍要求项目库当前版本是 3、未来版本是 4。`test_ledger` 仍通过。`arch_dependencies` 通过。

- [x] **Step 5: Commit**

```bash
git add kernel/exportq storage/sqlite tests/kernel/exportq tests/CMakeLists.txt docs/superpowers/plans/2026-11-03-kernel-export-queue.md
git commit -m "$(cat <<'EOF'
feat: replay the export queue and finish an interrupted rename

EOF
)"
```

执行时只在本地提交，不推远端，不建 Pull Request。
