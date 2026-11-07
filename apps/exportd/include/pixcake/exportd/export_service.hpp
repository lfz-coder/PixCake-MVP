#pragma once

#include "pixcake/exportd/image_io.hpp"
#include "pixcake/exportq/export_queue.hpp"
#include "pixcake/gpu/render_executor.hpp"
#include "pixcake/persist/file_system.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace pixcake::exportd {

struct ExportWork {
  ExportJob job;
  render::RenderDescription description;
  // The original file, decoded here at full resolution (requirement 10.4:
  // export uses the original's resolution, never a preview frame).
  std::string source_path;
  ExportEncodeRequest encode;
};

struct ExportResult {
  ResultCode code = ResultCode::Ok;
  std::string diagnostic;
  std::string export_id;
  // The encoded product (process_next without a queue returns it instead of
  // committing it), its size, and who drew it.
  std::vector<std::uint8_t> product;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  bool drawn_on_gpu = false;
};

// Owns only process-local admission and the pixels of one export: decode the
// original, draw the render description at full resolution (GPU in tiles when
// an offscreen context exists, else the reference renderer), encode.  Durable
// state and the temporary-file commit remain owned by kernel/exportq; this
// service never writes a target file itself and never consumes a preview frame.
class ExportService {
 public:
  // Null codecs take the system ones (ImageIO on macOS).
  explicit ExportService(std::shared_ptr<ExportDecoder> decoder = nullptr,
                         std::shared_ptr<ExportEncoder> encoder = nullptr);

  ResultCode submit(ExportWork work, std::string& diagnostic);
  // Draws and encodes the front task and returns the product; the task stays
  // pending (nothing is committed).
  ExportResult process_next();
  // The whole transaction: prepare, render, commit the product through the
  // queue (temporary file, then publish), or record the failure.  A committed
  // task leaves the pending list; a failed one stays for retry_pending.
  ExportResult process_next(ExportQueue& queue, FileSystem& files);
  ResultCode retry_pending(ExportQueue& queue, std::string& diagnostic);
  std::size_t pending_count() const;

  // Limits of the decoded original, checked before pixels are allocated.
  static constexpr std::uint32_t kMaxSourceEdge = 65535;
  static constexpr std::uint64_t kMaxSourcePixels = 200'000'000;

 private:
  static ResultCode validate_work(const ExportWork&, std::string& diagnostic);
  ExportResult produce(const ExportWork& work);
  bool gpu_ready();

  std::deque<ExportWork> pending_;
  std::shared_ptr<ExportDecoder> decoder_;
  std::shared_ptr<ExportEncoder> encoder_;
  gl::Context gpu_context_;
  gpu::RenderExecutor gpu_executor_;
  bool gpu_tried_ = false;
  bool gpu_ok_ = false;
};

}  // namespace pixcake::exportd
