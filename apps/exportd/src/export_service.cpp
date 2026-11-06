#include "pixcake/exportd/export_service.hpp"

#include <limits>

namespace pixcake::exportd {
namespace {

bool valid_source_geometry(const ExportWork& work, std::string& diagnostic) {
  if (work.source_width == 0 || work.source_height == 0) {
    diagnostic = "export source dimensions are empty";
    return false;
  }
  const std::uint64_t minimum_stride = static_cast<std::uint64_t>(work.source_width) * 4ull;
  if (minimum_stride > std::numeric_limits<std::uint32_t>::max() ||
      work.source_stride < minimum_stride) {
    diagnostic = "export source stride is smaller than RGBA width";
    return false;
  }
  return true;
}

}  // namespace

ResultCode ExportService::validate_work(const ExportWork& work, std::string& diagnostic) {
  if (work.job.export_id.empty() || work.job.original_id.empty() || work.job.document_snapshot.empty()) {
    diagnostic = "export job is missing its identity or document snapshot";
    return ResultCode::StateConflict;
  }
  if (work.description.image_id != work.job.original_id ||
      work.description.document_revision != work.job.document_revision) {
    diagnostic = "export job and render description revisions differ";
    return ResultCode::StateConflict;
  }
  const ResultCode description_code = gpu::RenderExecutor::validate(work.description, diagnostic);
  if (description_code != ResultCode::Ok) return description_code;
  if (!valid_source_geometry(work, diagnostic)) return ResultCode::StateConflict;
  diagnostic.clear();
  return ResultCode::Ok;
}

ResultCode ExportService::submit(ExportWork work, std::string& diagnostic) {
  const ResultCode code = validate_work(work, diagnostic);
  if (code != ResultCode::Ok) return code;
  pending_.push_back(std::move(work));
  diagnostic.clear();
  return ResultCode::Ok;
}

ExportResult ExportService::process_next() { return process_next(nullptr); }

ExportResult ExportService::process_next(ExportQueue& queue) { return process_next(&queue); }

ExportResult ExportService::process_next(ExportQueue* queue) {
  if (pending_.empty()) {
    return {ResultCode::StateConflict, "no export task is pending", {}};
  }
  const ExportWork& work = pending_.front();
  ExportResult result;
  result.export_id = work.job.export_id;

  if (queue != nullptr) {
    ExportJob queued;
    if (!queue->find_job(work.job.export_id, queued) ||
        queued.original_id != work.job.original_id ||
        queued.document_revision != work.job.document_revision ||
        queued.document_snapshot != work.job.document_snapshot) {
      result.code = ResultCode::StateConflict;
      result.diagnostic = "export queue snapshot does not match the pending work";
      return result;
    }
    ResultCode queue_code = queue->prepare(work.job.export_id);
    if (queue_code != ResultCode::Ok) {
      result.code = queue_code;
      result.diagnostic = "export queue rejected the prepare transition";
      return result;
    }
    queue_code = queue->start_rendering(work.job.export_id);
    if (queue_code != ResultCode::Ok) {
      result.code = queue_code;
      result.diagnostic = "export queue rejected the rendering transition";
      return result;
    }
  }

  if (!work.description.operations.empty()) {
    result.code = ResultCode::UnsupportedFormat;
    result.diagnostic = "no full-resolution pixel pass is registered for the render description";
  } else {
    result.code = ResultCode::UnsupportedFormat;
    result.diagnostic = "full-resolution image decoder and encoder are not registered";
  }

  if (queue != nullptr) {
    const ResultCode queue_code = queue->fail(work.job.export_id);
    if (queue_code != ResultCode::Ok) {
      result.code = queue_code;
      result.diagnostic = "export queue rejected the failure transition";
    }
  }
  return result;
}

ResultCode ExportService::retry_pending(ExportQueue& queue, std::string& diagnostic) {
  if (pending_.empty()) {
    diagnostic = "no export task is pending";
    return ResultCode::StateConflict;
  }
  const ExportWork& work = pending_.front();
  ExportJob queued;
  if (!queue.find_job(work.job.export_id, queued) ||
      queued.original_id != work.job.original_id ||
      queued.document_revision != work.job.document_revision ||
      queued.document_snapshot != work.job.document_snapshot) {
    diagnostic = "export queue snapshot does not match the pending work";
    return ResultCode::StateConflict;
  }
  const ResultCode code = queue.retry(work.job.export_id);
  if (code != ResultCode::Ok) {
    diagnostic = "export queue rejected the retry transition";
    return code;
  }
  diagnostic.clear();
  return ResultCode::Ok;
}

std::size_t ExportService::pending_count() const { return pending_.size(); }

}  // namespace pixcake::exportd
