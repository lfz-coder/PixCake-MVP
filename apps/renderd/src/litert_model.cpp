#include "pixcake/renderd/litert_model.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>

#include "tflite/core/interpreter_builder.h"
#include "tflite/interpreter.h"
#include "tflite/kernels/register.h"
#include "tflite/model_builder.h"
#include "xnnpack.h"

namespace pixcake::renderd {

struct LiteRtModel::Impl {
  std::unique_ptr<tflite::FlatBufferModel> model;
  std::unique_ptr<tflite::Interpreter> interpreter;
  std::vector<int> input_shape;
  std::vector<std::vector<int>> output_shapes;
  std::size_t input_elements = 0;
};

namespace {

std::vector<int> dims_of(const TfLiteTensor* tensor) {
  return std::vector<int>(tensor->dims->data, tensor->dims->data + tensor->dims->size);
}

std::size_t element_count(const std::vector<int>& shape) {
  std::size_t count = 1;
  for (int d : shape) {
    if (d <= 0) return 0;
    count *= static_cast<std::size_t>(d);
  }
  return count;
}

}  // namespace

LiteRtModel::LiteRtModel(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
LiteRtModel::~LiteRtModel() = default;

ResultCode LiteRtModel::load(const std::string& path, int threads, std::unique_ptr<LiteRtModel>& out,
                             std::string& diagnostic) {
  out.reset();
  {
    std::ifstream probe(path, std::ios::binary);
    if (!probe) {
      diagnostic = "model file cannot be read: " + path;
      return ResultCode::IoFailed;
    }
  }
  auto impl = std::make_unique<Impl>();
  impl->model = tflite::FlatBufferModel::VerifyAndBuildFromFile(path.c_str());
  if (!impl->model) {
    diagnostic = "model file is not a valid LiteRT model: " + path;
    return ResultCode::UnsupportedFormat;
  }
  tflite::ops::builtin::BuiltinOpResolver resolver;  // includes the XNNPACK delegate
  tflite::InterpreterBuilder builder(*impl->model, resolver);
  if (threads > 0) builder.SetNumThreads(threads);
  if (builder(&impl->interpreter) != kTfLiteOk || !impl->interpreter) {
    diagnostic = "model uses operators this runtime does not provide: " + path;
    return ResultCode::UnsupportedFormat;
  }
  if (impl->interpreter->AllocateTensors() != kTfLiteOk) {
    diagnostic = "model tensors cannot be allocated: " + path;
    return ResultCode::UnsupportedFormat;
  }
  if (impl->interpreter->inputs().size() != 1 || impl->interpreter->outputs().empty()) {
    diagnostic = "model must have one input and at least one output: " + path;
    return ResultCode::UnsupportedFormat;
  }
  const TfLiteTensor* input = impl->interpreter->input_tensor(0);
  if (input->type != kTfLiteFloat32) {
    diagnostic = "model input is not float32: " + path;
    return ResultCode::UnsupportedFormat;
  }
  impl->input_shape = dims_of(input);
  impl->input_elements = element_count(impl->input_shape);
  if (impl->input_elements == 0) {
    diagnostic = "model input has a dynamic or empty shape: " + path;
    return ResultCode::UnsupportedFormat;
  }
  for (std::size_t i = 0; i < impl->interpreter->outputs().size(); ++i) {
    const TfLiteTensor* output = impl->interpreter->output_tensor(i);
    if (output->type != kTfLiteFloat32) {
      diagnostic = "model output is not float32: " + path;
      return ResultCode::UnsupportedFormat;
    }
    impl->output_shapes.push_back(dims_of(output));
  }
  out.reset(new LiteRtModel(std::move(impl)));
  diagnostic.clear();
  return ResultCode::Ok;
}

const std::vector<int>& LiteRtModel::input_shape() const { return impl_->input_shape; }
std::size_t LiteRtModel::output_count() const { return impl_->output_shapes.size(); }
const std::vector<int>& LiteRtModel::output_shape(std::size_t index) const { return impl_->output_shapes.at(index); }

ResultCode LiteRtModel::run(const std::vector<float>& input, std::vector<std::vector<float>>& outputs,
                            std::string& diagnostic) {
  outputs.clear();
  if (input.size() != impl_->input_elements) {
    diagnostic = "model input has " + std::to_string(input.size()) + " values, expected " +
                 std::to_string(impl_->input_elements);
    return ResultCode::StateConflict;
  }
  float* destination = impl_->interpreter->typed_input_tensor<float>(0);
  if (destination == nullptr) {
    diagnostic = "model input tensor is unavailable";
    return ResultCode::StateConflict;
  }
  std::memcpy(destination, input.data(), input.size() * sizeof(float));
  if (impl_->interpreter->Invoke() != kTfLiteOk) {
    diagnostic = "model inference failed";
    return ResultCode::IoFailed;
  }
  outputs.reserve(impl_->output_shapes.size());
  for (std::size_t i = 0; i < impl_->output_shapes.size(); ++i) {
    const TfLiteTensor* tensor = impl_->interpreter->output_tensor(i);
    const float* data = impl_->interpreter->typed_output_tensor<float>(static_cast<int>(i));
    if (data == nullptr || tensor->bytes != element_count(impl_->output_shapes[i]) * sizeof(float)) {
      outputs.clear();
      diagnostic = "model output tensor changed shape";
      return ResultCode::StateConflict;
    }
    outputs.emplace_back(data, data + tensor->bytes / sizeof(float));
  }
  diagnostic.clear();
  return ResultCode::Ok;
}

ResultCode resize_bilinear_u8(const std::uint8_t* source, std::uint32_t source_width, std::uint32_t source_height,
                              std::uint32_t channels, std::size_t source_row_bytes, std::uint32_t target_width,
                              std::uint32_t target_height, std::vector<std::uint8_t>& out, std::string& diagnostic) {
  out.clear();
  if (source == nullptr || source_width == 0 || source_height == 0 || channels == 0 || target_width == 0 ||
      target_height == 0 || source_row_bytes < static_cast<std::size_t>(source_width) * channels) {
    diagnostic = "resize received an empty or malformed image";
    return ResultCode::StateConflict;
  }
  const std::uint64_t target_bytes = static_cast<std::uint64_t>(target_width) * target_height * channels;
  if (target_bytes > std::numeric_limits<std::size_t>::max() / 2) {
    diagnostic = "resize target is too large";
    return ResultCode::StateConflict;
  }
  // XNNPACK needs packed rows, and its kernels read up to XNN_EXTRA_BYTES past
  // the last pixel: the input is always copied into a buffer with that much
  // slack.  A caller's buffer ending on a page boundary otherwise faults
  // (SIGBUS in xnn_u8_ibilinear_ukernel__neon_u16).
  const std::size_t packed_row = static_cast<std::size_t>(source_width) * channels;
  std::vector<std::uint8_t> packed(packed_row * source_height + XNN_EXTRA_BYTES);
  if (source_row_bytes == packed_row) {
    std::memcpy(packed.data(), source, packed_row * source_height);
  } else {
    for (std::uint32_t y = 0; y < source_height; ++y) {
      std::memcpy(packed.data() + y * packed_row, source + y * source_row_bytes, packed_row);
    }
  }
  const std::uint8_t* input = packed.data();
  if (xnn_initialize(nullptr) != xnn_status_success) {
    diagnostic = "XNNPACK cannot be initialised on this CPU";
    return ResultCode::UnsupportedFormat;
  }
  xnn_operator_t op = nullptr;
  if (xnn_create_resize_bilinear2d_nhwc(xnn_datatype_quint8, target_height, target_width, 0, &op) !=
      xnn_status_success) {
    diagnostic = "XNNPACK resize operator cannot be created";
    return ResultCode::UnsupportedFormat;
  }
  std::unique_ptr<xnn_operator, decltype(&xnn_delete_operator)> guard(op, &xnn_delete_operator);
  std::size_t workspace_size = 0;
  if (xnn_reshape_resize_bilinear2d_nhwc(op, 1, source_height, source_width, channels, channels, channels,
                                         &workspace_size, nullptr) != xnn_status_success) {
    diagnostic = "XNNPACK resize rejected the image size";
    return ResultCode::StateConflict;
  }
  std::vector<std::uint8_t> workspace(workspace_size);
  out.resize(static_cast<std::size_t>(target_bytes));
  if (xnn_setup_resize_bilinear2d_nhwc(op, workspace.empty() ? nullptr : workspace.data(), input, out.data()) !=
          xnn_status_success ||
      xnn_run_operator(op, nullptr) != xnn_status_success) {
    out.clear();
    diagnostic = "XNNPACK resize failed";
    return ResultCode::IoFailed;
  }
  diagnostic.clear();
  return ResultCode::Ok;
}

}  // namespace pixcake::renderd
