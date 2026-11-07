#include "pixcake/renderd/onnx_model.hpp"

#include <onnxruntime_cxx_api.h>

#include <fstream>

namespace pixcake::renderd {
namespace {

Ort::Env& shared_env() {
  static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "pixcake-renderd");
  return env;
}

std::size_t element_count(const std::vector<std::int64_t>& shape) {
  std::size_t count = 1;
  for (std::int64_t d : shape) {
    if (d <= 0) return 0;
    count *= static_cast<std::size_t>(d);
  }
  return count;
}

}  // namespace

struct OnnxModel::Impl {
  Ort::Session session{nullptr};
  std::string input_name;
  std::string output_name;
  std::vector<std::int64_t> input_shape;
};

OnnxModel::OnnxModel(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
OnnxModel::~OnnxModel() = default;

ResultCode OnnxModel::load(const std::string& path, int threads, std::unique_ptr<OnnxModel>& out,
                           std::string& diagnostic) {
  out.reset();
  {
    std::ifstream probe(path, std::ios::binary);
    if (!probe) {
      diagnostic = "model file cannot be read: " + path;
      return ResultCode::IoFailed;
    }
  }
  try {
    Ort::SessionOptions options;
    if (threads > 0) options.SetIntraOpNumThreads(threads);
    options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    auto impl = std::make_unique<Impl>();
    impl->session = Ort::Session(shared_env(), path.c_str(), options);
    if (impl->session.GetInputCount() != 1 || impl->session.GetOutputCount() < 1) {
      diagnostic = "model must have one input and at least one output: " + path;
      return ResultCode::UnsupportedFormat;
    }
    Ort::AllocatorWithDefaultOptions allocator;
    impl->input_name = impl->session.GetInputNameAllocated(0, allocator).get();
    impl->output_name = impl->session.GetOutputNameAllocated(0, allocator).get();
    // The tensor info views borrow from the TypeInfo objects: keep those alive.
    const Ort::TypeInfo input_type = impl->session.GetInputTypeInfo(0);
    const Ort::TypeInfo output_type = impl->session.GetOutputTypeInfo(0);
    if (input_type.GetONNXType() != ONNX_TYPE_TENSOR || output_type.GetONNXType() != ONNX_TYPE_TENSOR) {
      diagnostic = "model input/output is not a tensor: " + path;
      return ResultCode::UnsupportedFormat;
    }
    const auto input_info = input_type.GetTensorTypeAndShapeInfo();
    const auto output_info = output_type.GetTensorTypeAndShapeInfo();
    if (input_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
        output_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
      diagnostic = "model input/output is not float32: " + path;
      return ResultCode::UnsupportedFormat;
    }
    impl->input_shape = input_info.GetShape();
    out.reset(new OnnxModel(std::move(impl)));
  } catch (const Ort::Exception& error) {
    diagnostic = std::string("ONNX Runtime rejected the model: ") + error.what();
    return ResultCode::UnsupportedFormat;
  }
  diagnostic.clear();
  return ResultCode::Ok;
}

const std::vector<std::int64_t>& OnnxModel::input_shape() const { return impl_->input_shape; }

ResultCode OnnxModel::run(const std::vector<float>& input, const std::vector<std::int64_t>& shape,
                          std::vector<float>& output, std::vector<std::int64_t>& output_shape,
                          std::string& diagnostic) {
  output.clear();
  output_shape.clear();
  if (shape.size() != impl_->input_shape.size() || element_count(shape) != input.size() || input.empty()) {
    diagnostic = "model input shape does not match the data";
    return ResultCode::StateConflict;
  }
  for (std::size_t i = 0; i < shape.size(); ++i) {
    if (impl_->input_shape[i] > 0 && impl_->input_shape[i] != shape[i]) {
      diagnostic = "model input shape does not match the model";
      return ResultCode::StateConflict;
    }
  }
  try {
    const auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value tensor = Ort::Value::CreateTensor<float>(memory, const_cast<float*>(input.data()), input.size(),
                                                        shape.data(), shape.size());
    const char* input_names[] = {impl_->input_name.c_str()};
    const char* output_names[] = {impl_->output_name.c_str()};
    auto results = impl_->session.Run(Ort::RunOptions{nullptr}, input_names, &tensor, 1, output_names, 1);
    if (results.size() != 1 || !results[0].IsTensor()) {
      diagnostic = "model returned no tensor";
      return ResultCode::IoFailed;
    }
    const auto info = results[0].GetTensorTypeAndShapeInfo();
    output_shape = info.GetShape();
    const float* data = results[0].GetTensorData<float>();
    output.assign(data, data + info.GetElementCount());
  } catch (const Ort::Exception& error) {
    output.clear();
    output_shape.clear();
    diagnostic = std::string("model inference failed: ") + error.what();
    return ResultCode::IoFailed;
  }
  diagnostic.clear();
  return ResultCode::Ok;
}

}  // namespace pixcake::renderd
