#pragma once

#include "pixcake/exportd/export_masks.hpp"
#include "pixcake/exportd/image_io.hpp"
#include "pixcake/exportq/export_queue.hpp"
#include "pixcake/exportq/export_settings.hpp"
#include "pixcake/gpu/render_executor.hpp"
#include "pixcake/persist/file_system.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
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
  // 水印：素材文件与三套摆放（按成品的方向选一套），没有值就不加。
  std::optional<ExportWatermark> watermark;
  std::string watermark_path;
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
  // The format written ("original format" resolved to the original's).
  ExportFormat format = ExportFormat::Original;
};

// Owns only process-local admission and the pixels of one export: decode the
// original, draw the render description at full resolution (GPU in tiles when
// an offscreen context exists, else the reference renderer), encode.  Durable
// state and the temporary-file commit remain owned by kernel/exportq; this
// service never writes a target file itself and never consumes a preview frame.
class ExportService {
 public:
  // Null codecs take the system ones (ImageIO on macOS).  Without a mask
  // source, an export whose operations use masks fails with a reason.
  explicit ExportService(std::shared_ptr<ExportDecoder> decoder = nullptr,
                         std::shared_ptr<ExportEncoder> encoder = nullptr,
                         std::shared_ptr<ExportMaskSource> masks = nullptr);

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
  // Forgets every pending task (the export server builds one per start message
  // and keeps no failed task for retry_pending; the queue keeps the record).
  void clear_pending() { pending_.clear(); }

  // Limits of the decoded original, checked before pixels are allocated.
  static constexpr std::uint32_t kMaxSourceEdge = 65535;
  static constexpr std::uint64_t kMaxSourcePixels = 200'000'000;
  // Limits of a watermark material (the dialog takes JPG/PNG up to 10 MB).
  static constexpr std::uint32_t kMaxWatermarkEdge = 16384;
  static constexpr std::uint64_t kMaxWatermarkPixels = 64'000'000;

 private:
  static ResultCode validate_work(const ExportWork&, std::string& diagnostic);
  ExportResult produce(const ExportWork& work);
  // Encodes within request.size_limit_bytes: the highest JPEG quality whose
  // product fits (binary search on the encoder's 0..1 quality).
  ResultCode encode_within_limit(const ExportEncodeRequest& request, const RgbaSource& pixels, EncodedProduct& out,
                                 std::string& diagnostic);
  bool gpu_ready();

  std::deque<ExportWork> pending_;
  std::shared_ptr<ExportDecoder> decoder_;
  std::shared_ptr<ExportEncoder> encoder_;
  std::shared_ptr<ExportMaskSource> masks_;
  gl::Context gpu_context_;
  gpu::RenderExecutor gpu_executor_;
  bool gpu_tried_ = false;
  bool gpu_ok_ = false;
};

}  // namespace pixcake::exportd
