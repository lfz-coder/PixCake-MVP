#pragma once

#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pixcake::renderd {

// A loaded ONNX Runtime session with one float32 input; only the first output
// is computed and returned.  The header exposes no ONNX Runtime types; only
// onnx_model.cpp includes the runtime headers.
//
// An instance is not thread-safe; use one instance per worker thread.
class OnnxModel {
 public:
  ~OnnxModel();
  OnnxModel(const OnnxModel&) = delete;
  OnnxModel& operator=(const OnnxModel&) = delete;

  // IoFailed: the file cannot be read.  UnsupportedFormat: the runtime rejects
  // the model, or its first input/output is not float32.
  static ResultCode load(const std::string& path, int threads, std::unique_ptr<OnnxModel>& out,
                         std::string& diagnostic);

  // Declared input shape; dynamic dimensions are -1.
  const std::vector<std::int64_t>& input_shape() const;

  // `shape` gives the concrete input shape; input.size() must match it.
  ResultCode run(const std::vector<float>& input, const std::vector<std::int64_t>& shape,
                 std::vector<float>& output, std::vector<std::int64_t>& output_shape, std::string& diagnostic);

 private:
  struct Impl;
  explicit OnnxModel(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace pixcake::renderd
