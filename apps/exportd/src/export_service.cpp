#include "pixcake/exportd/export_service.hpp"

#include "pixcake/gl/native_backend.hpp"

#include <utility>

namespace pixcake::exportd {

ExportService::ExportService(std::shared_ptr<ExportDecoder> decoder, std::shared_ptr<ExportEncoder> encoder)
    : decoder_(decoder ? std::move(decoder) : std::shared_ptr<ExportDecoder>(make_system_export_decoder())),
      encoder_(encoder ? std::move(encoder) : std::shared_ptr<ExportEncoder>(make_system_export_encoder())) {}

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
  if (work.source_path.empty()) {
    diagnostic = "export job has no original file";
    return ResultCode::StateConflict;
  }
  const ResultCode settings = validate_encode_request(work.encode, diagnostic);
  if (settings != ResultCode::Ok) return settings;
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

bool ExportService::gpu_ready() {
  if (!gpu_tried_) {
    gpu_tried_ = true;
    if (gl::native::available()) {
      gpu_context_ = gl::native::create_context();
      gpu_ok_ = gpu_context_.valid() && gpu_context_.makeCurrent();
      if (gpu_ok_) gpu_context_.doneCurrent();
    }
  }
  return gpu_ok_;
}

ExportResult ExportService::produce(const ExportWork& work) {
  ExportResult result;
  result.export_id = work.job.export_id;
  const auto refuse = [&](ResultCode code, std::string reason) {
    result.code = code;
    result.diagnostic = std::move(reason);
    return result;
  };
  if (!decoder_ || !encoder_) {
    return refuse(ResultCode::UnsupportedFormat, "full-resolution image decoder and encoder are not registered");
  }
  // Refused rather than exported without: a product missing an effect the
  // preview shows is worse than a failure with its reason.
  if (work.encode.include_watermark) {
    return refuse(ResultCode::UnsupportedFormat, "导出水印还没有接入");
  }
  for (const auto& operation : work.description.operations) {
    if (operation.enabled && !operation.mask_ids.empty()) {
      return refuse(ResultCode::UnsupportedFormat, "导出还不能按全分辨率计算局部调整的蒙版");
    }
  }

  DecodedSource source;
  const ExportDecodeLimits limits{kMaxSourceEdge, kMaxSourceEdge, kMaxSourcePixels};
  ResultCode code = decoder_->decode(work.source_path, limits, source, result.diagnostic);
  if (code != ResultCode::Ok) {
    result.code = code;
    return result;
  }

  gpu::RgbaImage image;
  image.width = source.pixels.width;
  image.height = source.pixels.height;
  image.row_stride = source.pixels.stride;
  image.revision = work.description.document_revision;
  image.pixels = std::move(source.pixels.rgba);
  const gpu::RenderInputs inputs;
  gpu::RenderResult rendered;
  bool ok = false;
  bool drawn = false;
  if (gpu_ready()) {
    std::string gpu_error;
    ok = gpu_executor_.execute(gpu_context_, work.description, image, inputs, rendered, gpu_error);
    drawn = ok || rendered.reference_fallback;
    if (!drawn) gpu_ok_ = false;  // a failed GPU pass retires the context
  }
  result.drawn_on_gpu = drawn && ok && !rendered.reference_fallback;
  if (!drawn) ok = gpu::RenderExecutor{}.renderReference(work.description, image, inputs, rendered);
  if (!ok) {
    return refuse(ResultCode::StateConflict, rendered.diagnostics.empty() ? "the renderer rejected the original"
                                                                         : rendered.diagnostics.back().reason);
  }

  RgbaSource pixels;
  pixels.width = rendered.image.width;
  pixels.height = rendered.image.height;
  pixels.stride = rendered.image.row_stride == 0 ? rendered.image.width * 4 : rendered.image.row_stride;
  pixels.rgba = std::move(rendered.image.pixels);
  ExportEncodeRequest request = work.encode;
  request.source_icc_profile = std::move(source.icc_profile);
  request.has_alpha = source.has_alpha;
  EncodedProduct product;
  code = encoder_->encode(request, pixels, product, result.diagnostic);
  if (code != ResultCode::Ok) {
    result.code = code;
    return result;
  }
  result.product = std::move(product.bytes);
  result.width = pixels.width;
  result.height = pixels.height;
  result.code = ResultCode::Ok;
  result.diagnostic.clear();
  return result;
}

ExportResult ExportService::process_next() {
  if (pending_.empty()) return {ResultCode::StateConflict, "no export task is pending", {}, {}, 0, 0, false};
  return produce(pending_.front());
}

ExportResult ExportService::process_next(ExportQueue& queue, FileSystem& files) {
  if (pending_.empty()) return {ResultCode::StateConflict, "no export task is pending", {}, {}, 0, 0, false};
  const ExportWork& work = pending_.front();
  ExportResult result;
  result.export_id = work.job.export_id;

  ExportJob queued;
  if (!queue.find_job(work.job.export_id, queued) || queued.original_id != work.job.original_id ||
      queued.document_revision != work.job.document_revision ||
      queued.document_snapshot != work.job.document_snapshot) {
    result.code = ResultCode::StateConflict;
    result.diagnostic = "export queue snapshot does not match the pending work";
    return result;
  }
  ResultCode queue_code = queue.prepare(work.job.export_id);
  if (queue_code != ResultCode::Ok) {
    result.code = queue_code;
    result.diagnostic = "export queue rejected the prepare transition";
    return result;
  }
  queue_code = queue.start_rendering(work.job.export_id);
  if (queue_code != ResultCode::Ok) {
    result.code = queue_code;
    result.diagnostic = "export queue rejected the rendering transition";
    return result;
  }

  result = produce(work);
  if (result.code == ResultCode::Ok) {
    const ResultCode committed = queue.commit_product(work.job.export_id, result.product, files);
    if (committed == ResultCode::Ok) {
      pending_.pop_front();
      return result;
    }
    result.code = committed;
    result.diagnostic = "export queue could not commit the product";
  }
  const ResultCode failed = queue.fail(work.job.export_id);
  if (failed != ResultCode::Ok) {
    result.code = failed;
    result.diagnostic = "export queue rejected the failure transition";
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
