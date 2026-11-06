#include "pixcake/renderd/render_service.hpp"

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

ResultCode RenderService::cancel(std::uint64_t client_sequence, const std::string& image_id,
                                 const std::string& editing_instance_id,
                                 std::uint64_t document_revision, std::string& diagnostic) {
  if (client_sequence == 0 || image_id.empty() || editing_instance_id.empty()) {
    diagnostic = "preview cancellation is missing its sequence or identity";
    return ResultCode::StateConflict;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (!pending_.has_value()) {
    diagnostic = "no preview request is pending";
    return ResultCode::StateConflict;
  }
  const PreviewWork& work = *pending_;
  if (work.client_sequence != client_sequence || work.request.image_id != image_id ||
      work.request.editing_instance_id != editing_instance_id ||
      work.request.base_revision != document_revision) {
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
      return {ResultCode::StateConflict, 0, {}, {}, "no preview request is pending"};
    }
    work = std::move(*pending_);
    pending_.reset();
  }

  PreviewResult result;
  result.client_sequence = work.client_sequence;
  if (has_enabled_operations(work.description)) {
    result.code = ResultCode::UnsupportedFormat;
    result.diagnostic = "no pixel pass is registered for the render description";
    return result;
  }
  if (work.input.width > work.request.max_edge || work.input.height > work.request.max_edge) {
    result.code = ResultCode::UnsupportedFormat;
    result.diagnostic = "preview resampling backend is not registered";
    return result;
  }

  result.header.sequence = work.client_sequence;
  result.header.document_revision = work.description.document_revision;
  result.header.width = work.input.width;
  result.header.height = work.input.height;
  result.header.stride = copy_stride(work.input.width);
  result.header.format = ipc::PixelFormat::Rgba8;
  result.rgba.resize(static_cast<std::size_t>(result.header.stride) * result.header.height);
  for (std::uint32_t row = 0; row < result.header.height; ++row) {
    const auto* source = work.input.bytes.data() + static_cast<std::size_t>(row) * work.input.stride;
    auto* destination = result.rgba.data() + static_cast<std::size_t>(row) * result.header.stride;
    std::copy(source, source + result.header.stride, destination);
  }
  result.code = ResultCode::Ok;
  return result;
}

bool RenderService::has_pending() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pending_.has_value();
}

}  // namespace pixcake::renderd
