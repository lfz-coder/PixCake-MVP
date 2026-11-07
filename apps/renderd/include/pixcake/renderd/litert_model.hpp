#pragma once

#include "pixcake/support/result_code.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pixcake::renderd {

// A loaded LiteRT (TensorFlow Lite) model with one float32 input and float32
// outputs.  This header deliberately exposes no LiteRT types: only the
// implementation file includes the runtime, so the rest of renderd (and
// nothing in kernel/) depends on it.
//
// An instance is not thread-safe; use one instance per worker thread.
class LiteRtModel {
 public:
  ~LiteRtModel();
  LiteRtModel(const LiteRtModel&) = delete;
  LiteRtModel& operator=(const LiteRtModel&) = delete;

  // IoFailed: the file cannot be read.  UnsupportedFormat: not a valid model,
  // unsupported operators, or tensors that are not float32.
  static ResultCode load(const std::string& path, int threads, std::unique_ptr<LiteRtModel>& out,
                         std::string& diagnostic);

  const std::vector<int>& input_shape() const;
  std::size_t output_count() const;
  const std::vector<int>& output_shape(std::size_t index) const;

  // `input` must hold exactly the number of elements of input_shape().
  // On success `outputs` holds one vector per output tensor, in model order.
  ResultCode run(const std::vector<float>& input, std::vector<std::vector<float>>& outputs,
                 std::string& diagnostic);

 private:
  struct Impl;
  explicit LiteRtModel(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

// Bilinear resize of an interleaved 8-bit image (half-pixel centres), done by
// XNNPACK.  `source_row_bytes` may exceed width * channels; the destination is
// packed (row bytes = width * channels).
ResultCode resize_bilinear_u8(const std::uint8_t* source, std::uint32_t source_width, std::uint32_t source_height,
                              std::uint32_t channels, std::size_t source_row_bytes, std::uint32_t target_width,
                              std::uint32_t target_height, std::vector<std::uint8_t>& out, std::string& diagnostic);

}  // namespace pixcake::renderd
