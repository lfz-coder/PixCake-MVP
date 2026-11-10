#pragma once

#include "pixcake/generate/generation_task.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pixcake::generated {

// Normalized result at the boundary between a provider adapter and the
// generation process.  The provider's JSON/SDK schema must not escape here.
// Pixels are RGBA8; invalid dimensions or byte counts are rejected by the
// generation service after this reply is returned.
inline constexpr std::uint64_t kBytesPerPixel = 4;
inline constexpr std::size_t kMaxContinuationParameters = 32 * 1024;

struct RemoteResult {
  std::vector<std::uint8_t> image;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  // Opaque provider-neutral state for controls that continue editing the
  // generated layer.  The provider codec may fill it; the kernel never parses
  // the tool-specific schema.
  std::vector<std::uint8_t> continuation_parameters;
};

struct RemoteReply {
  ResultCode code = ResultCode::Ok;
  // One item per requested output, in ordinal order.  A successful reply must
  // contain exactly GenerationTask::tool.output_count items.
  std::vector<RemoteResult> results;
};

class RemoteTool {
 public:
  virtual ~RemoteTool() = default;
  virtual RemoteReply run(const generate::GenerationTask& task) = 0;
};

}  // namespace pixcake::generated
