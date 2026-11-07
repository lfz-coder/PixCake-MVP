// Full-resolution semantic masks with the real models (inference build).
// The test portrait enlarged five times (4100 x 5120, above the preview's 4000
// edge) is exported with a local exposure on the face skin:
// - the full-resolution face-skin mask agrees with the mask the preview path
//   computes on the 4000 px decode (sampled at the same relative positions);
// (the model's masks are soft: "inside" is alpha >= 200, "outside" alpha 0);
// - the product brightens inside the mask and leaves pixels outside alone.
#include "pixcake/exportd/export_masks.hpp"
#include "pixcake/exportd/export_service.hpp"
#include "pixcake/persist/file_system.hpp"
#include "pixcake/platform/mac_image_decoder.hpp"
#include "pixcake/platform/mac_image_encoder.hpp"
#include "pixcake/renderd/portrait_inference.hpp"
#include "support/temp_dir.hpp"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

using pixcake::ResultCode;

pixcake::platform::MacDecodedImage decode(const std::string& path, std::uint32_t max_edge = 0) {
  pixcake::platform::MacDecodeOptions options;
  options.max_source_width = options.max_source_height = 65535;
  options.max_source_pixels = 200'000'000;
  options.max_edge = max_edge;
  pixcake::platform::MacDecodedImage image;
  std::string diagnostic;
  assert(pixcake::platform::mac_decode_image(path, options, image, diagnostic) == ResultCode::Ok);
  return image;
}

pixcake::render::RenderDescription face_skin_exposure(double ev) {
  pixcake::render::RenderDescription description;
  description.image_id = "photo-1";
  description.editing_instance_id = "edit-1";
  description.document_revision = 1;
  pixcake::render::RenderOperation local;
  local.capability_id = "color.local";
  local.stage = "local";
  local.program = "pixcake.color.local";
  local.order = 1;
  local.values = {{"exposure", "exposure", std::to_string(ev), ev}};
  local.mask_ids = {"semantic:person.face_skin"};
  description.operations.push_back(local);
  return description;
}

}  // namespace

int main() {
  pixcake::test::TempDir dir;
  // The fixture, enlarged 5x by pixel repetition and written as a JPEG.
  const auto portrait = decode(std::string(PIXCAKE_TESTDATA_DIR) + "/portrait.jpg");
  constexpr std::uint32_t kScale = 5;
  const std::uint32_t width = portrait.width * kScale;
  const std::uint32_t height = portrait.height * kScale;
  std::vector<std::uint8_t> big(static_cast<std::size_t>(width) * height * 4);
  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      const auto* from = portrait.rgba.data() + (static_cast<std::size_t>(y / kScale) * portrait.width + x / kScale) * 4;
      std::copy(from, from + 4, big.data() + (static_cast<std::size_t>(y) * width + x) * 4);
    }
  }
  std::vector<std::uint8_t> bytes;
  std::string diagnostic;
  pixcake::platform::MacEncodeRequest jpeg;
  assert(pixcake::platform::mac_encode_image(jpeg, width, height, width * 4, big.data(), big.size(), bytes,
                                             diagnostic) == ResultCode::Ok);
  const std::string source = dir.path() + "/big-portrait.jpg";
  pixcake::PosixFileSystem files;
  assert(files.write_new(source, bytes).ok);

  std::string status;
  auto masks = pixcake::exportd::make_system_export_masks(
      pixcake::renderd::model_directory_near_executable(PIXCAKE_MODEL_DIR), status);
  std::printf("%s\n", status.c_str());
  assert(status.find("available;") != std::string::npos);

  // Full-resolution mask from the export path.
  const auto description = face_skin_exposure(1.0);
  pixcake::exportd::ExportMasks full;
  const auto started = std::chrono::steady_clock::now();
  assert(masks->provide(source, description, width, height, full, diagnostic) == ResultCode::Ok);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
  assert(full.masks.size() == 1 && full.masks[0].width == width && full.masks[0].height == height);
  const auto& mask = full.masks[0].alpha;

  // The preview path's mask on the 4000 px decode, from the same segmenter.
  std::shared_ptr<pixcake::renderd::SemanticSegmenter> segmenter;
  assert(pixcake::renderd::create_portrait_segmenter(
             pixcake::renderd::model_directory_near_executable(PIXCAKE_MODEL_DIR), 4, segmenter, diagnostic) ==
         ResultCode::Ok);
  auto decoder = pixcake::renderd::make_system_image_decoder();
  pixcake::renderd::DecodeRequest request;
  request.source_path = source;
  request.limits = {30000, 30000, 300'000'000, 4000ull * 4000ull * 4ull};
  request.max_edge = 4000;
  pixcake::renderd::RgbaInput preview;
  assert(decoder->decode(request, preview, diagnostic) == ResultCode::Ok);
  assert(preview.width < width && preview.height == 4000);
  pixcake::renderd::SemanticMaskProvider provider(segmenter);
  pixcake::renderd::SemanticMaskBatch batch;
  provider.provide(description.image_id, preview, description, batch);
  assert(!batch.segmentation_failed && batch.masks.size() == 1);
  const auto& small = batch.masks[0].alpha;

  // Same mask at the same relative positions (bilinear resize: differences at edges only).
  double difference = 0;
  std::size_t covered = 0;
  std::size_t samples = 0;
  for (std::uint32_t y = 0; y < preview.height; y += 7) {
    for (std::uint32_t x = 0; x < preview.width; x += 7) {
      const std::uint32_t fx = static_cast<std::uint32_t>((x + 0.5) * width / preview.width);
      const std::uint32_t fy = static_cast<std::uint32_t>((y + 0.5) * height / preview.height);
      const int a = small[static_cast<std::size_t>(y) * preview.width + x];
      const int b = mask[static_cast<std::size_t>(fy) * width + fx];
      difference += std::abs(a - b);
      covered += a > 127 ? 1 : 0;
      ++samples;
    }
  }
  const double coverage = static_cast<double>(covered) / samples;
  const double mean_difference = difference / samples;
  std::printf("face skin %.1f%% of the frame; full-res vs preview mask mean |diff| %.2f / 255; masks in %lld ms\n",
              coverage * 100, mean_difference, static_cast<long long>(ms.count()));
  assert(coverage > 0.02 && coverage < 0.6);
  assert(mean_difference < 2.0);

  // The export: brighter inside the mask, unchanged outside.
  pixcake::exportd::ExportService service(nullptr, nullptr, masks);
  pixcake::exportd::ExportWork work;
  work.job.export_id = "masked-big";
  work.job.original_id = "photo-1";
  work.job.document_revision = 1;
  work.job.document_snapshot = {1};
  work.job.destination_path = dir.path() + "/out.png";
  work.description = description;
  work.source_path = source;
  work.encode.format = pixcake::exportd::ExportFormat::Png;  // lossless, to compare pixels
  work.encode.limits.max_output_bytes = 1ull << 31;
  assert(service.submit(std::move(work), diagnostic) == ResultCode::Ok);
  const auto product = service.process_next();
  if (product.code != ResultCode::Ok) std::fprintf(stderr, "%s\n", product.diagnostic.c_str());
  assert(product.code == ResultCode::Ok && product.width == width && product.height == height);
  const std::string out = dir.path() + "/out.png";
  assert(files.write_new(out, product.product).ok);
  const auto exported = decode(out);
  const auto original = decode(source);
  double inside_gain = 0, outside_change = 0;
  std::size_t inside = 0, outside = 0;
  for (std::size_t i = 0; i < static_cast<std::size_t>(width) * height; i += 13) {
    const double before = original.rgba[i * 4 + 1];
    const double after = exported.rgba[i * 4 + 1];
    if (mask[i] >= 200) {
      inside_gain += after - before;
      ++inside;
    } else if (mask[i] == 0) {
      outside_change += std::abs(after - before);
      ++outside;
    }
  }
  inside_gain /= static_cast<double>(inside);
  outside_change /= static_cast<double>(outside);
  std::printf("export %ux%u on %s: green +%.1f inside the face skin, |change| %.2f outside\n", width, height,
              product.drawn_on_gpu ? "GPU" : "reference", inside_gain, outside_change);
  assert(inside > 1000 && outside > 1000);
  assert(inside_gain > 20.0);
  assert(outside_change < 1.0);
  std::printf("export masks with models ok\n");
  return 0;
}
