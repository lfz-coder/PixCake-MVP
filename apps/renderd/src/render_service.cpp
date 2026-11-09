#include "pixcake/renderd/render_service.hpp"
#include "pixcake/gpu/geometry.hpp"

#include "pixcake/gl/native_backend.hpp"

#include <algorithm>
#include <limits>

namespace pixcake::renderd {
namespace {

bool same_identity(const PreviewWork& work) {
  return !work.request.image_id.empty() &&
         work.request.image_id == work.description.image_id &&
         !work.request.editing_instance_id.empty() &&
         work.request.editing_instance_id == work.description.editing_instance_id &&
         work.request.base_revision == work.description.document_revision;
}

std::uint32_t copy_stride(std::uint32_t width) {
  return width * 4u;
}

}  // namespace

RenderService::RenderService(std::shared_ptr<SemanticSegmenter> segmenter) : masks_(std::move(segmenter)) {}

ResultCode RenderService::validate_input(const RgbaInput& input, std::string& diagnostic) {
  if (input.width == 0 || input.height == 0) {
    diagnostic = "preview input dimensions are empty";
    return ResultCode::StateConflict;
  }
  const std::uint64_t minimum_stride = static_cast<std::uint64_t>(input.width) * 4ull;
  if (minimum_stride > std::numeric_limits<std::uint32_t>::max() || input.stride < minimum_stride) {
    diagnostic = "preview input stride is smaller than RGBA width";
    return ResultCode::StateConflict;
  }
  const std::uint64_t required = static_cast<std::uint64_t>(input.stride) * input.height;
  if (required > std::numeric_limits<std::size_t>::max() ||
      input.bytes.size() < static_cast<std::size_t>(required)) {
    diagnostic = "preview input bytes do not match its dimensions";
    return ResultCode::StateConflict;
  }
  diagnostic.clear();
  return ResultCode::Ok;
}

bool RenderService::has_enabled_operations(const render::RenderDescription& description) {
  for (const auto& operation : description.operations) {
    if (operation.enabled) return true;
  }
  return false;
}

ResultCode RenderService::submit(PreviewWork work, std::string& diagnostic) {
  if (work.client_sequence == 0 || work.request.max_edge == 0 || work.request.max_edge > 4000 ||
      (work.request.mode != preview::PreviewMode::FullEffect &&
       work.request.mode != preview::PreviewMode::ColorOnly)) {
    diagnostic = "preview request has an invalid sequence, mode or maximum edge";
    return ResultCode::StateConflict;
  }
  if (!same_identity(work)) {
    diagnostic = "preview request and render description identity differ";
    return ResultCode::StateConflict;
  }
  if (work.request.capability_id.empty() || work.request.parameter_id.empty() ||
      work.request.display_value.empty()) {
    diagnostic = "preview request is missing its parameter identity or display value";
    return ResultCode::StateConflict;
  }
  const ResultCode description_code = gpu::RenderExecutor::validate(work.description, diagnostic);
  if (description_code != ResultCode::Ok) return description_code;
  const ResultCode input_code = validate_input(work.input, diagnostic);
  if (input_code != ResultCode::Ok) return input_code;

  std::lock_guard<std::mutex> lock(mutex_);
  pending_ = std::move(work);
  diagnostic.clear();
  return ResultCode::Ok;
}

ResultCode RenderService::cancel(const ipc::PreviewCancelMessage& message, std::string& diagnostic) {
  if (message.client_sequence == 0 || message.image_id.empty() || message.editing_instance_id.empty()) {
    diagnostic = "preview cancellation is missing its sequence or identity";
    return ResultCode::StateConflict;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (!pending_.has_value()) {
    diagnostic = "no preview request is pending";
    return ResultCode::StateConflict;
  }
  const PreviewWork& work = *pending_;
  if (work.client_sequence != message.client_sequence || work.request.image_id != message.image_id ||
      work.request.editing_instance_id != message.editing_instance_id ||
      work.request.base_revision != message.base_revision) {
    diagnostic = "preview cancellation does not match the pending request";
    return ResultCode::StateConflict;
  }
  pending_.reset();
  diagnostic.clear();
  return ResultCode::Ok;
}

PreviewResult RenderService::render_latest() {
  PreviewWork work;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pending_.has_value()) {
      PreviewResult none;
      none.code = ResultCode::StateConflict;
      none.diagnostic = "no preview request is pending";
      return none;
    }
    work = std::move(*pending_);
    pending_.reset();
  }

  return render_now(work.client_sequence, work.request.image_id, work.description, work.input, work.request.max_edge);
}

PreviewResult RenderService::render_now(std::uint64_t sequence, const std::string& image_id,
                                        const render::RenderDescription& description, const RgbaInput& input,
                                        std::uint32_t max_edge) {
  PreviewResult result;
  result.client_sequence = sequence;
  std::string invalid;
  if (validate_input(input, invalid) != ResultCode::Ok) {
    result.code = ResultCode::StateConflict;
    result.diagnostic = invalid;
    return result;
  }
  if (input.width > max_edge || input.height > max_edge) {
    result.code = ResultCode::UnsupportedFormat;
    result.diagnostic = "preview resampling backend is not registered";
    return result;
  }
  if (has_enabled_operations(description) || !gpu::geometry_of(description).identity()) {
    return render_operations(sequence, image_id, description, input, max_edge, std::move(result));
  }

  result.header.sequence = sequence;
  result.header.document_revision = description.document_revision;
  result.header.width = input.width;
  result.header.height = input.height;
  result.header.stride = copy_stride(input.width);
  result.header.format = ipc::PixelFormat::Rgba8;
  result.rgba.resize(static_cast<std::size_t>(result.header.stride) * result.header.height);
  for (std::uint32_t row = 0; row < result.header.height; ++row) {
    const auto* source = input.bytes.data() + static_cast<std::size_t>(row) * input.stride;
    auto* destination = result.rgba.data() + static_cast<std::size_t>(row) * result.header.stride;
    std::copy(source, source + result.header.stride, destination);
  }
  result.code = ResultCode::Ok;
  return result;
}

PreviewResult RenderService::render_operations(std::uint64_t sequence, const std::string& image_id,
                                               const render::RenderDescription& description,
                                               const RgbaInput& input, std::uint32_t max_edge,
                                               PreviewResult result) {
  // Semantic masks are computed on the frame itself (requirement 10.4:
  // preview uses the <= 4000 px image, export the full resolution, same rules).
  SemanticMaskBatch batch;
  masks_.provide(image_id, input, description, batch);

  gpu::RgbaImage image;
  image.width = input.width;
  image.height = input.height;
  image.row_stride = input.stride;
  image.revision = description.document_revision;
  image.pixels = input.bytes;
  gpu::RenderInputs inputs;
  inputs.masks = std::move(batch.masks);
  inputs.preview = true;
  inputs.preview_max_dimension = max_edge;

  // 几何（翻转、旋转、拉直、比例裁剪）在颜色绘制之前整张图做完，蒙版同步；缩略图和预览走这里，导出走同一个函数。
  gpu::apply_geometry(gpu::geometry_of(description), image, inputs.masks);
  gpu::add_gradient_masks(description, image.width, image.height, inputs.masks);  // 按几何变换后的画幅生成
  gpu::add_range_masks(description, image, inputs.masks);  // 颜色范围、亮度范围用几何后的画面生成
  gpu::add_invert_masks(description, inputs.masks);
  gpu::add_combined_masks(description, inputs.masks);

  gpu::RenderResult rendered;
  bool ok = false;
  bool drawn = false;
  std::string gpu_failure;
  if (gpu_ready()) {
    std::string gpu_error;
    ok = gpu_executor_.execute(gpu_context_, description, image, inputs, rendered, gpu_error);
    // A reference fallback that fails rejected the frame itself; only a
    // failed GPU pass retires the context.
    drawn = ok || rendered.reference_fallback;
    if (!drawn) {
      gpu_ok_ = false;
      gpu_failure = "GPU 绘制失败，改用 reference：" + gpu_error;
    }
  }
  result.drawn_on_gpu = drawn && ok && !rendered.reference_fallback;
  if (!drawn) ok = gpu::RenderExecutor{}.renderReference(description, image, inputs, rendered);
  // 裁剪旋转由上面的几何变换画完，绘制器对它报“没有注册”只是噪音。
  const auto geometry_op = [](const auto& entry) { return entry.capability_id == "crop"; };
  rendered.diagnostics.erase(std::remove_if(rendered.diagnostics.begin(), rendered.diagnostics.end(), geometry_op),
                             rendered.diagnostics.end());
  rendered.statuses.erase(std::remove_if(rendered.statuses.begin(), rendered.statuses.end(), geometry_op),
                          rendered.statuses.end());
  if (!gpu_failure.empty()) rendered.diagnostics.push_back({"", gpu_failure, false});
  result.diagnostics = std::move(batch.diagnostics);
  result.diagnostics.insert(result.diagnostics.end(), rendered.diagnostics.begin(), rendered.diagnostics.end());
  result.statuses = std::move(rendered.statuses);
  if (!ok) {
    result.code = ResultCode::StateConflict;
    result.diagnostic = result.diagnostics.empty() ? "reference renderer rejected the frame"
                                                   : result.diagnostics.back().reason;
    return result;
  }

  const gpu::RgbaImage& out = rendered.image;
  result.header.sequence = sequence;
  result.header.document_revision = description.document_revision;
  result.header.width = out.width;
  result.header.height = out.height;
  result.header.stride = copy_stride(out.width);
  result.header.format = ipc::PixelFormat::Rgba8;
  result.rgba.resize(static_cast<std::size_t>(result.header.stride) * result.header.height);
  const std::size_t source_stride = out.row_stride == 0 ? copy_stride(out.width) : out.row_stride;
  for (std::uint32_t row = 0; row < out.height; ++row) {
    const auto* source = out.pixels.data() + static_cast<std::size_t>(row) * source_stride;
    std::copy(source, source + result.header.stride, result.rgba.data() + static_cast<std::size_t>(row) * result.header.stride);
  }
  result.code = ResultCode::Ok;
  return result;
}

bool RenderService::gpu_ready() {
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

bool RenderService::has_pending() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pending_.has_value();
}

}  // namespace pixcake::renderd
