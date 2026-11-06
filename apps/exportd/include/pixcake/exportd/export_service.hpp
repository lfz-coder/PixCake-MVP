#pragma once

#include "pixcake/exportq/export_queue.hpp"
#include "pixcake/gpu/render_executor.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

namespace pixcake::exportd {

struct ExportWork {
  ExportJob job;
  render::RenderDescription description;
  std::uint32_t source_width = 0;
  std::uint32_t source_height = 0;
  std::uint32_t source_stride = 0;
};

struct ExportResult {
  ResultCode code = ResultCode::Ok;
  std::string diagnostic;
  std::string export_id;
};

// Owns only process-local admission. Durable state and temporary-file
// commits remain owned by kernel/exportq; this service never writes a target
// file and never consumes a preview frame.
class ExportService {
 public:
  ResultCode submit(ExportWork work, std::string& diagnostic);
  ExportResult process_next();
  ExportResult process_next(ExportQueue& queue);
  ResultCode retry_pending(ExportQueue& queue, std::string& diagnostic);
  std::size_t pending_count() const;

 private:
  static ResultCode validate_work(const ExportWork&, std::string& diagnostic);
  ExportResult process_next(ExportQueue* queue);
  std::deque<ExportWork> pending_;
};

}  // namespace pixcake::exportd
