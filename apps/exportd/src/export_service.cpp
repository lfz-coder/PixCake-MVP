#include "pixcake/exportd/export_service.hpp"
#include "pixcake/gpu/geometry.hpp"

#include "pixcake/exportd/watermark.hpp"

#include "pixcake/gl/native_backend.hpp"

#include <cstdio>
#include <utility>

namespace pixcake::exportd {

ExportService::ExportService(std::shared_ptr<ExportDecoder> decoder, std::shared_ptr<ExportEncoder> encoder,
                             std::shared_ptr<ExportMaskSource> masks)
    : decoder_(decoder ? std::move(decoder) : std::shared_ptr<ExportDecoder>(make_system_export_decoder())),
      encoder_(encoder ? std::move(encoder) : std::shared_ptr<ExportEncoder>(make_system_export_encoder())),
      masks_(std::move(masks)) {}

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
  // "Original format" is resolved once the original is decoded; validate the
  // rest of the settings as if it were JPG.
  ExportEncodeRequest settings_request = work.encode;
  if (settings_request.format == ExportFormat::Original) settings_request.format = ExportFormat::Jpeg;
  const ResultCode settings = validate_encode_request(settings_request, diagnostic);
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
  bool masked = false;
  for (const auto& operation : work.description.operations) {
    masked = masked || (operation.enabled && !operation.mask_ids.empty());
  }
  if (masked && !masks_) return refuse(ResultCode::UnsupportedFormat, "导出蒙版没有接入，带局部调整的照片还不能导出");

  // The watermark material first: a missing one fails before the original is drawn.
  DecodedSource material;
  if (work.watermark) {
    std::string why;
    const ExportDecodeLimits material_limits{kMaxWatermarkEdge, kMaxWatermarkEdge, kMaxWatermarkPixels};
    const ResultCode read = work.watermark_path.empty()
                                ? ResultCode::SourceMissing
                                : decoder_->decode(work.watermark_path, material_limits, material, why);
    if (read != ResultCode::Ok) return refuse(read, "水印素材无法读取" + (why.empty() ? std::string() : "：" + why));
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
  // Semantic masks at the original's resolution (requirement 10.4: the
  // preview's segmentation, resized).  Parts the model does not output are
  // skipped as in the preview; masks that cannot be computed fail the export.
  gpu::RenderInputs inputs;
  inputs.preview = false;
  if (masked) {
    ExportMasks masks;
    code = masks_->provide(work.source_path, work.description, image.width, image.height, masks, result.diagnostic);
    if (code != ResultCode::Ok) {
      result.code = code;
      return result;
    }
    inputs.masks = std::move(masks.masks);
  }
  gpu::apply_geometry(gpu::geometry_of(work.description), image, inputs.masks);
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
  if (work.watermark) {
    // The placement for the product's shape (横图 / 竖图 / 方图), after crop and rotation.
    const auto shape = watermark_shape(pixels.width, pixels.height);
    code = draw_watermark(material.pixels, work.watermark->placements[static_cast<int>(shape)], pixels,
                          result.diagnostic);
    if (code != ResultCode::Ok) {
      result.code = code;
      return result;
    }
  }
  ExportEncodeRequest request = work.encode;
  if (request.format == ExportFormat::Original) request.format = resolve_original_format(source.kind);
  result.format = request.format;
  request.source_icc_profile = std::move(source.icc_profile);
  request.has_alpha = source.has_alpha;
  EncodedProduct product;
  if (request.size_limit_bytes > 0 && request.format != ExportFormat::Jpeg) {
    return refuse(ResultCode::UnsupportedFormat, "文件大小限制只适用于 JPG");
  }
  code = request.size_limit_bytes > 0 ? encode_within_limit(request, pixels, product, result.diagnostic)
                                      : encoder_->encode(request, pixels, product, result.diagnostic);
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

ResultCode ExportService::encode_within_limit(const ExportEncodeRequest& request, const RgbaSource& pixels,
                                              EncodedProduct& out, std::string& diagnostic) {
  constexpr int kSteps = 8;  // the quality found is within 1/256 of the best that fits
  ExportEncodeRequest attempt = request;
  const auto encode_at = [&](double quality, EncodedProduct& product) {
    attempt.native_quality = quality;
    return encoder_->encode(attempt, pixels, product, diagnostic);
  };
  EncodedProduct best;
  ResultCode code = encode_at(1.0, best);
  if (code != ResultCode::Ok) return code;
  if (best.bytes.size() > request.size_limit_bytes) {
    // Search the largest quality that fits; the lowest must fit at all.
    double low = 0.0;
    double high = 1.0;
    code = encode_at(0.0, best);
    if (code != ResultCode::Ok) return code;
    if (best.bytes.size() > request.size_limit_bytes) {
      char text[160];
      std::snprintf(text, sizeof(text), "最低品质也超过文件大小限制（%.2f MB，最低品质为 %.2f MB）",
                    static_cast<double>(request.size_limit_bytes) / (1024.0 * 1024.0),
                    static_cast<double>(best.bytes.size()) / (1024.0 * 1024.0));
      diagnostic = text;
      return ResultCode::UnsupportedFormat;
    }
    for (int step = 0; step < kSteps; ++step) {
      const double middle = (low + high) / 2.0;
      EncodedProduct product;
      code = encode_at(middle, product);
      if (code != ResultCode::Ok) return code;
      if (product.bytes.size() <= request.size_limit_bytes) {
        low = middle;
        best = std::move(product);
      } else {
        high = middle;
      }
    }
  }
  out = std::move(best);
  diagnostic.clear();
  return ResultCode::Ok;
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
  const ResultCode failed = queue.fail(work.job.export_id, result.code, result.diagnostic);
  if (failed != ResultCode::Ok) {
    // Keep the original cause: it is what the user needs to see.
    result.diagnostic += " (export queue rejected the failure transition: " +
                         std::string(result_code_label(failed)) + ")";
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
