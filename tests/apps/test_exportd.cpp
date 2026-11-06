#include "pixcake/exportd/export_service.hpp"

#include <cassert>

namespace {

pixcake::exportd::ExportWork work() {
  pixcake::exportd::ExportWork value;
  value.job.export_id = "export-1";
  value.job.original_id = "image-1";
  value.job.document_revision = 11;
  value.job.document_snapshot = {0x50, 0x58, 0x45, 0x44};
  value.description.image_id = "image-1";
  value.description.document_revision = 11;
  value.source_width = 2;
  value.source_height = 2;
  value.source_stride = 8;
  return value;
}

}  // namespace

int main() {
  pixcake::exportd::ExportService service;
  std::string diagnostic;
  auto valid = work();
  assert(service.submit(std::move(valid), diagnostic) == pixcake::ResultCode::Ok);
  assert(service.pending_count() == 1);
  const auto result = service.process_next();
  assert(result.code == pixcake::ResultCode::UnsupportedFormat);
  assert(result.export_id == "export-1");
  assert(service.pending_count() == 1);

  auto queued_work = work();
  queued_work.job.destination_path = "/cache/export-1.jpg";
  queued_work.job.destination_root = "/cache";
  queued_work.job.settings_snapshot = "quality=8";
  pixcake::ExportQueue queue = pixcake::ExportQueue::open("/cache/exports");
  assert(queue.enqueue(queued_work.job) == pixcake::ResultCode::Ok);
  const auto queued_result = service.process_next(queue);
  assert(queued_result.code == pixcake::ResultCode::UnsupportedFormat);
  assert(queue.status("export-1") == "失败");
  assert(service.pending_count() == 1);
  assert(service.retry_pending(queue, diagnostic) == pixcake::ResultCode::Ok);
  assert(queue.status("export-1") == "排队");
  assert(service.process_next(queue).code == pixcake::ResultCode::UnsupportedFormat);
  assert(queue.status("export-1") == "失败");

  auto mismatch = work();
  mismatch.description.document_revision = 12;
  assert(service.submit(std::move(mismatch), diagnostic) == pixcake::ResultCode::StateConflict);

  auto operation = work();
  operation.job.export_id = "export-2";
  operation.description.operations.push_back({"color.global", "global", "pixcake.color.global", 1, {}, {},
                                               "normal", true, {}});
  assert(service.submit(std::move(operation), diagnostic) == pixcake::ResultCode::Ok);
  const auto effect_result = service.process_next();
  assert(effect_result.code == pixcake::ResultCode::UnsupportedFormat);
  assert(effect_result.export_id == "export-1");
  return 0;
}
