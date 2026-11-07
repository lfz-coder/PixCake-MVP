// exportd: admission, the full-resolution pixel path (ImageIO decode, GPU in
// tiles or the reference renderer, ImageIO encode) and the queue transaction
// that publishes the product.
#include "pixcake/exportd/export_service.hpp"

#include "pixcake/gl/native_backend.hpp"
#include "pixcake/persist/file_system.hpp"
#include "pixcake/platform/mac_image_decoder.hpp"
#include "pixcake/platform/mac_image_encoder.hpp"
#include "support/temp_dir.hpp"

#include <ImageIO/ImageIO.h>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace {

using pixcake::ResultCode;
using pixcake::exportd::ExportFormat;
using pixcake::exportd::ExportService;
using pixcake::exportd::ExportWork;
using pixcake::render::RenderValue;

// An sRGB JPEG original (quality 1.0) with a gradient, written to `path`.
void write_original(const std::string& path, std::uint32_t width, std::uint32_t height) {
  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4);
  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      auto* p = rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4;
      p[0] = static_cast<std::uint8_t>(40 + (x * 80) / width);
      p[1] = static_cast<std::uint8_t>(50 + (y * 60) / height);
      p[2] = 70;
      p[3] = 255;
    }
  }
  pixcake::platform::MacEncodeRequest request;
  std::vector<std::uint8_t> bytes;
  std::string diagnostic;
  assert(pixcake::platform::mac_encode_image(request, width, height, width * 4, rgba.data(), rgba.size(), bytes,
                                             diagnostic) == ResultCode::Ok);
  pixcake::PosixFileSystem files;
  assert(files.write_new(path, bytes).ok);
}

pixcake::platform::MacDecodedImage decode(const std::string& path) {
  pixcake::platform::MacDecodeOptions options;
  options.max_source_width = options.max_source_height = 65535;
  options.max_source_pixels = 100'000'000;
  pixcake::platform::MacDecodedImage image;
  std::string diagnostic;
  assert(pixcake::platform::mac_decode_image(path, options, image, diagnostic) == ResultCode::Ok);
  return image;
}

// Bits per sample of an encoded image, read back through ImageIO.
int bits_per_sample(const std::vector<std::uint8_t>& bytes) {
  CFDataRef data = CFDataCreate(nullptr, bytes.data(), static_cast<CFIndex>(bytes.size()));
  CGImageSourceRef source = CGImageSourceCreateWithData(data, nullptr);
  CGImageRef image = source != nullptr ? CGImageSourceCreateImageAtIndex(source, 0, nullptr) : nullptr;
  const int bits = image != nullptr ? static_cast<int>(CGImageGetBitsPerComponent(image)) : 0;
  if (image != nullptr) CGImageRelease(image);
  if (source != nullptr) CFRelease(source);
  CFRelease(data);
  return bits;
}

double mean_red(const std::vector<std::uint8_t>& rgba) {
  double sum = 0;
  for (std::size_t i = 0; i < rgba.size(); i += 4) sum += rgba[i];
  return sum / static_cast<double>(rgba.size() / 4);
}

pixcake::render::RenderOperation operation(const char* capability, const char* program,
                                           std::vector<RenderValue> values) {
  pixcake::render::RenderOperation op;
  op.capability_id = capability;
  op.stage = "color";
  op.program = program;
  op.order = 1;
  op.values = std::move(values);
  return op;
}

RenderValue value(const char* channel, double number) { return {channel, channel, std::to_string(number), number}; }

ExportWork work(const std::string& id, const std::string& source, const std::string& destination,
                const std::string& root) {
  ExportWork value;
  value.job.export_id = id;
  value.job.original_id = "image-1";
  value.job.document_revision = 11;
  value.job.document_snapshot = {0x50, 0x58, 0x45, 0x44};
  value.job.destination_path = destination;
  value.job.destination_root = root;
  value.job.settings_snapshot = "quality=12";
  value.description.image_id = "image-1";
  value.description.document_revision = 11;
  value.source_path = source;
  value.encode.format = ExportFormat::Jpeg;
  value.encode.quality = 12;
  value.encode.limits.max_output_bytes = 64ull << 20;
  return value;
}

// Keeps the pixels handed to the encoder, so a test can compare them.
class CapturingEncoder final : public pixcake::exportd::ExportEncoder {
 public:
  ResultCode encode(const pixcake::exportd::ExportEncodeRequest&, const pixcake::exportd::RgbaSource& source,
                    pixcake::exportd::EncodedProduct& out, std::string&) override {
    captured = source;
    out.format = ExportFormat::Jpeg;
    out.bytes = {1};
    return ResultCode::Ok;
  }
  pixcake::exportd::RgbaSource captured;
};

bool gpu_available() {
  auto probe = pixcake::gl::native::create_context();
  const bool ok = pixcake::gl::native::available() && probe.makeCurrent();
  if (ok) probe.doneCurrent();
  return ok;
}

}  // namespace

int main() {
  pixcake::test::TempDir dir;
  const std::string source = dir.path() + "/original.jpg";
  write_original(source, 320, 200);
  const bool gpu = gpu_available();
  std::string diagnostic;

  // Admission: the snapshot, the original and the settings must be complete.
  {
    ExportService service;
    auto mismatch = work("bad-1", source, dir.path() + "/x.jpg", dir.path());
    mismatch.description.document_revision = 12;
    assert(service.submit(std::move(mismatch), diagnostic) == ResultCode::StateConflict);
    auto no_source = work("bad-2", "", dir.path() + "/x.jpg", dir.path());
    assert(service.submit(std::move(no_source), diagnostic) == ResultCode::StateConflict);
    auto other_space = work("bad-3", source, dir.path() + "/x.jpg", dir.path());
    other_space.encode.color_space = "Adobe RGB";
    assert(service.submit(std::move(other_space), diagnostic) == ResultCode::StateConflict);
    assert(service.pending_count() == 0);
  }

  // "Original format" keeps the original's container: a JPEG stays JPEG, a PNG
  // stays PNG.  TIFF 16-bit writes 16 bits per sample.
  {
    ExportService service;
    auto jpeg = work("orig-1", source, dir.path() + "/o.jpg", dir.path());
    jpeg.encode.format = ExportFormat::Original;
    assert(service.submit(std::move(jpeg), diagnostic) == ResultCode::Ok);
    auto product = service.process_next();
    assert(product.code == ResultCode::Ok && product.format == ExportFormat::Jpeg);
    assert(product.product[0] == 0xFF && product.product[1] == 0xD8);

    const std::string png = dir.path() + "/original.png";
    {
      std::vector<std::uint8_t> rgba(16 * 8 * 4, 128);
      pixcake::platform::MacEncodeRequest request;
      request.format = pixcake::platform::MacEncodeFormat::Png;
      std::vector<std::uint8_t> bytes;
      assert(pixcake::platform::mac_encode_image(request, 16, 8, 16 * 4, rgba.data(), rgba.size(), bytes,
                                                 diagnostic) == ResultCode::Ok);
      pixcake::PosixFileSystem files;
      assert(files.write_new(png, bytes).ok);
    }
    ExportService png_service;
    auto from_png = work("orig-2", png, dir.path() + "/o.png", dir.path());
    from_png.encode.format = ExportFormat::Original;
    assert(png_service.submit(std::move(from_png), diagnostic) == ResultCode::Ok);
    product = png_service.process_next();
    assert(product.code == ResultCode::Ok && product.format == ExportFormat::Png);
    assert(product.product.size() > 8 && product.product[1] == 'P' && product.product[2] == 'N');

    ExportService tiff_service;
    auto wide = work("tiff16-1", source, dir.path() + "/o.tif", dir.path());
    wide.encode.format = ExportFormat::Tiff16;
    assert(tiff_service.submit(std::move(wide), diagnostic) == ResultCode::Ok);
    product = tiff_service.process_next();
    assert(product.code == ResultCode::Ok && product.format == ExportFormat::Tiff16);
    assert(bits_per_sample(product.product) == 16);
  }

  // Without a queue: the product comes back, full size, one EV brighter; the
  // task stays pending.
  {
    ExportService service;
    auto brighter = work("export-1", source, dir.path() + "/unused.jpg", dir.path());
    brighter.description.operations.push_back(
        operation("color.global", "pixcake.color.global", {value("exposure", 1.0)}));
    assert(service.submit(std::move(brighter), diagnostic) == ResultCode::Ok);
    const auto result = service.process_next();
    assert(result.code == ResultCode::Ok);
    assert(result.width == 320 && result.height == 200);
    assert(result.drawn_on_gpu == gpu);
    assert(result.product.size() > 2 && result.product[0] == 0xFF && result.product[1] == 0xD8);  // JPEG SOI
    assert(service.pending_count() == 1);
    pixcake::PosixFileSystem files;
    const std::string written = dir.path() + "/product.jpg";
    assert(files.write_new(written, result.product).ok);
    const double before = mean_red(decode(source).rgba), after = mean_red(decode(written).rgba);
    std::printf("export exposure +1: mean red %.1f -> %.1f\n", before, after);
    assert(after > before * 1.8 && after < before * 2.2);
  }

  // Through the queue: the product is published at the destination, the task
  // leaves the pending list and the queue records success.
  {
    ExportService service;
    pixcake::PosixFileSystem files;
    pixcake::ExportQueue queue = pixcake::ExportQueue::open(dir.path() + "/export-cache");
    const std::string destination = dir.path() + "/out/edited.tif";
    assert(files.create_directory(dir.path() + "/out").ok);
    auto tiff = work("export-2", source, destination, dir.path() + "/out");
    tiff.encode.format = ExportFormat::Tiff;
    tiff.encode.color_space = "sRGB";
    tiff.description.operations.push_back(
        operation("color.global", "pixcake.color.global", {value("hsl_saturation_red", -100)}));
    assert(queue.enqueue(tiff.job) == ResultCode::Ok);
    assert(service.submit(std::move(tiff), diagnostic) == ResultCode::Ok);
    const auto result = service.process_next(queue, files);
    if (result.code != ResultCode::Ok) std::fprintf(stderr, "export failed: %s\n", result.diagnostic.c_str());
    assert(result.code == ResultCode::Ok);
    assert(queue.status("export-2") == "成功");
    assert(service.pending_count() == 0);
    const auto exported = decode(destination);
    assert(exported.kind == pixcake::platform::MacImageKind::Tiff && exported.width == 320);
  }

  // Work the export cannot draw yet fails with its reason, stays pending and
  // can be retried; nothing is written.
  {
    ExportService service;
    pixcake::PosixFileSystem files;
    pixcake::ExportQueue queue = pixcake::ExportQueue::open(dir.path() + "/export-cache-2");
    const std::string destination = dir.path() + "/out/masked.jpg";
    auto masked = work("export-3", source, destination, dir.path() + "/out");
    auto local = operation("color.local", "pixcake.color.local", {value("exposure", 0.5)});
    local.mask_ids = {"semantic:person.skin"};
    masked.description.operations.push_back(local);
    assert(queue.enqueue(masked.job) == ResultCode::Ok);
    assert(service.submit(std::move(masked), diagnostic) == ResultCode::Ok);
    const auto result = service.process_next(queue, files);
    assert(result.code == ResultCode::UnsupportedFormat && !result.diagnostic.empty());
    assert(queue.status("export-3") == "失败");
    assert(!files.exists(destination));
    assert(service.pending_count() == 1);
    assert(service.retry_pending(queue, diagnostic) == ResultCode::Ok);
    assert(queue.status("export-3") == "排队");

    auto watermark = work("export-4", source, dir.path() + "/out/w.jpg", dir.path() + "/out");
    watermark.encode.include_watermark = true;
    ExportService other;
    assert(other.submit(std::move(watermark), diagnostic) == ResultCode::Ok);
    assert(other.process_next().code == ResultCode::UnsupportedFormat);
  }

  // Quality 0..14: a higher quality keeps more detail, so the file is larger.
  {
    ExportService service;
    auto low = work("q-low", source, dir.path() + "/q.jpg", dir.path());
    low.encode.quality = 1;
    auto high = work("q-high", source, dir.path() + "/q.jpg", dir.path());
    high.encode.quality = 14;
    assert(service.submit(std::move(low), diagnostic) == ResultCode::Ok);
    const auto small = service.process_next();
    ExportService again;
    assert(again.submit(std::move(high), diagnostic) == ResultCode::Ok);
    const auto large = again.process_next();
    assert(small.code == ResultCode::Ok && large.code == ResultCode::Ok);
    assert(large.product.size() > small.product.size());
  }

  // Wider than one GPU tile (4096): the export draws in tiles and matches the
  // reference renderer on the same original, grain included (no seams).
  {
    const std::string wide = dir.path() + "/wide.jpg";
    write_original(wide, 4200, 24);
    auto encoder = std::make_shared<CapturingEncoder>();
    ExportService service(nullptr, encoder);
    auto tiled = work("wide", wide, dir.path() + "/wide-out.jpg", dir.path());
    tiled.description.operations.push_back(operation(
        "color.global", "pixcake.color.global", {value("exposure", 0.4), value("hsl_hue_red", 30)}));
    auto grain = operation("detail", "pixcake.detail", {value("grain_amount", 70), value("grain_size", 20)});
    grain.stage = "detail.grain";
    grain.order = 9;
    tiled.description.operations.push_back(grain);
    const auto description = tiled.description;
    assert(service.submit(std::move(tiled), diagnostic) == ResultCode::Ok);
    const auto result = service.process_next();
    assert(result.code == ResultCode::Ok && result.drawn_on_gpu == gpu);

    const auto original = decode(wide);
    pixcake::gpu::RgbaImage image;
    image.width = original.width;
    image.height = original.height;
    image.row_stride = original.stride;
    image.revision = 11;
    image.pixels = original.rgba;
    pixcake::gpu::RenderResult reference;
    assert(pixcake::gpu::RenderExecutor{}.renderReference(description, image, {}, reference));
    const auto& got = encoder->captured;
    assert(got.width == image.width && got.height == image.height);
    int worst = 0;
    for (std::uint32_t y = 0; y < got.height; ++y) {
      for (std::uint32_t x = 0; x < got.width * 4; ++x) {
        const int a = got.rgba[static_cast<std::size_t>(y) * got.stride + x];
        const int b = reference.image.pixels[static_cast<std::size_t>(y) * reference.image.row_stride + x];
        worst = std::max(worst, std::abs(a - b));
      }
    }
    std::printf("export 4200 px wide on %s: max diff from reference %d\n", gpu ? "GPU tiles" : "reference", worst);
    assert(worst <= 1);
  }
  return 0;
}
