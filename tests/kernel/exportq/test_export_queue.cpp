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
