#pragma once

#include "pixcake/gpu/render_executor.hpp"
#include "pixcake/ipc/preview_protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace pixcake::renderd {

struct RgbaInput {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t stride = 0;
  std::vector<std::uint8_t> bytes;
};

struct PreviewWork {
  std::uint64_t client_sequence = 0;
  preview::PreviewRequest request;
  render::RenderDescription description;
  RgbaInput input;
};

struct PreviewResult {
  ResultCode code = ResultCode::Ok;
  std::uint64_t client_sequence = 0;
  ipc::PreviewFrameHeader header;
  std::vector<std::uint8_t> rgba;
  std::string diagnostic;
};

// Process-side owner of preview work. It deliberately accepts already decoded
// RGBA input: file decoding and GPU resource lifetime belong to later adapters.
// At most one request is retained, so slider drags cannot create an unbounded
// queue of stale frames.
class RenderService {
 public:
  ResultCode submit(PreviewWork work, std::string& diagnostic);
  // Cancel only the pending request that matches the client sequence and
  // document identity. A newer request or another editing instance is never
  // removed by an older cancellation message.
  ResultCode cancel(std::uint64_t client_sequence, const std::string& image_id,
                    const std::string& editing_instance_id, std::uint64_t document_revision,
                    std::string& diagnostic);
  PreviewResult render_latest();
  bool has_pending() const;

 private:
  static ResultCode validate_input(const RgbaInput&, std::string& diagnostic);
  static bool has_enabled_operations(const render::RenderDescription&);

  mutable std::mutex mutex_;
  std::optional<PreviewWork> pending_;
};

}  // namespace pixcake::renderd
