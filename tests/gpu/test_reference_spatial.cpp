// The reference executor runs PhotoCraft's whole-frame Camera Raw stages (camera_raw_spatial.cpp)
// in develop's order between the per-pixel stages; the GPU path leaves such operations to it.
#include <pixcake/gpu/camera_raw.hpp>
#include <pixcake/gpu/camera_raw_spatial.hpp>
#include <pixcake/gpu/render_executor.hpp>
#include <pixcake/render/render_description.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>

// Checked in every build type (the release benchmark build defines NDEBUG).
#define CHECK(x)                                                              \
  do {                                                                        \
    if (!(x)) {                                                               \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x);      \
      std::abort();                                                           \
    }                                                                         \
  } while (0)

namespace {

namespace cr = pixcake::gpu::camera_raw;
using pixcake::gpu::PassRegistry;
using pixcake::gpu::PassStatus;
using pixcake::gpu::RenderExecutor;
using pixcake::gpu::RenderInputs;
using pixcake::gpu::RenderResult;
using pixcake::gpu::RgbaImage;
using pixcake::render::RenderDescription;
using pixcake::render::RenderValue;

RenderValue value(const char* channel, double numeric) { return {channel, channel, std::to_string(numeric), numeric}; }
RenderDescription op(const char* capability, const char* program, std::initializer_list<RenderValue> values,
                     std::vector<std::string> masks = {}) {
  RenderDescription description;
  pixcake::render::RenderOperation item;
  item.capability_id = capability;
  item.stage = "color";
  item.program = program;
  item.values.assign(values.begin(), values.end());
  item.mask_ids = std::move(masks);
  description.operations.push_back(std::move(item));
  return description;
}
RenderDescription global(std::initializer_list<RenderValue> values) { return op("color.global", "pixcake.color.global", values); }

// A frame with smooth gradients, a hard edge, a bright block and fine detail.
RgbaImage scene(std::uint32_t w, std::uint32_t h) {
  RgbaImage image;
  image.width = w;
  image.height = h;
  image.row_stride = w * 4;
  for (std::uint32_t y = 0; y < h; ++y) {
    for (std::uint32_t x = 0; x < w; ++x) {
      const float fx = static_cast<float>(x) / static_cast<float>(w), fy = static_cast<float>(y) / static_cast<float>(h);
      float r = 0.1F + 0.6F * fx, g = 0.15F + 0.5F * fy, b = 0.3F;
      if (fx > 0.55F) r += 0.25F, b += 0.2F;
      if (fx > 0.2F && fx < 0.45F && fy > 0.2F && fy < 0.5F) r = g = b = 0.95F;
      const auto q = [](float v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255.0F)); };
      image.pixels.insert(image.pixels.end(), {q(r), q(g), q(b), 255});
    }
  }
  return image;
}
RenderResult run(const RenderDescription& d, const RgbaImage& input, const RenderInputs& inputs = {}) {
  RenderResult result;
  const bool ok = RenderExecutor{}.renderReference(d, input, inputs, result);
  CHECK(ok);
  return result;
}
std::uint8_t q(float v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255.0F)); }

void test_spatial_order_matches_develop() {
  const auto input = scene(48, 32);
  const auto result = run(global({value("temperature", 20), value("exposure", 0.4), value("dehaze", 30), value("shadows", 50),
                                  value("highlights", -40), value("clarity", 30), value("texture", 20), value("contrast", 25),
                                  value("whites", 20), value("blacks", -20), value("vibrance", 30)}),
                          input);
  cr::Frame f;
  f.width = input.width;
  f.height = input.height;
  const auto gains = cr::balanceGains(20, 0, 0.4F);
  for (std::size_t i = 0; i < std::size_t{f.width} * f.height; ++i) {
    const auto c = cr::applyBalance({input.pixels[i * 4] / 255.0F, input.pixels[i * 4 + 1] / 255.0F, input.pixels[i * 4 + 2] / 255.0F}, gains);
    f.px.push_back({c[0], c[1], c[2], 1.0F});
  }
  cr::dehaze(f, 0.3F);
  cr::localTone(f, 50, -40);
  cr::toneDetail(f, 25, 20, 30, 1.0F);
  cr::whitesBlacks(f, 20, -20);
  for (std::size_t i = 0; i < f.px.size(); ++i) {
    const auto c = cr::applyPresence({f.px[i][0], f.px[i][1], f.px[i][2]}, 30, 0);
    for (int k = 0; k < 3; ++k) {
      if (result.image.pixels[i * 4 + k] != q(c[k])) {
        std::printf("pixel %zu c%d: got %d want %d\n", i, k, result.image.pixels[i * 4 + k], q(c[k]));
        CHECK(false);
      }
    }
  }
}

// A quarter-size preview (pixel_scale 0.25) stays close to the full-resolution result shrunk.
void test_spatial_preview_matches_export() {
  const std::uint32_t W = 400, H = 300, S = 4;
  const auto full = scene(W, H);
  RgbaImage small;
  small.width = W / S;
  small.height = H / S;
  small.row_stride = small.width * 4;
  const auto shrink = [&](const RgbaImage& img, std::uint32_t x, std::uint32_t y, int c) {
    int sum = 0;
    for (std::uint32_t dy = 0; dy < S; ++dy)
      for (std::uint32_t dx = 0; dx < S; ++dx) sum += img.pixels[((y * S + dy) * W + x * S + dx) * 4 + c];
    return static_cast<std::uint8_t>((sum + S * S / 2) / (S * S));
  };
  for (std::uint32_t y = 0; y < small.height; ++y)
    for (std::uint32_t x = 0; x < small.width; ++x)
      small.pixels.insert(small.pixels.end(), {shrink(full, x, y, 0), shrink(full, x, y, 1), shrink(full, x, y, 2), 255});
  const auto d = global({value("shadows", 60), value("highlights", -50), value("whites", 30), value("blacks", -30), value("dehaze", 20)});
  const auto exported = run(d, full);
  RenderInputs preview_inputs;
  preview_inputs.pixel_scale = 1.0F / S;
  const auto preview = run(d, small, preview_inputs);
  double sum = 0;
  int worst = 0;
  for (std::uint32_t y = 0; y < small.height; ++y)
    for (std::uint32_t x = 0; x < small.width; ++x)
      for (int c = 0; c < 3; ++c) {
        const int a = preview.image.pixels[(y * small.width + x) * 4 + c], b = shrink(exported.image, x, y, c);
        sum += std::abs(a - b);
        worst = std::max(worst, std::abs(a - b));
      }
  const double mean = sum / (small.width * small.height * 3.0);
  std::printf("spatial preview vs export: mean %.2f, worst %d levels\n", mean, worst);
  // PhotoCraft's own preview/export gap (pyramid depth and percentiles follow the frame size;
  // the port matches PhotoCraft bit for bit): measured mean 2.45, worst 5 here, mean 0.75 /
  // worst 10 at 1600 x 1200.  The bound catches a stage skipped or run on the wrong frame.
  CHECK(mean <= 3.0 && worst <= 24);
}

void test_spatial_tiny_frames() {
  const auto d = global({value("dehaze", 50), value("shadows", 50), value("highlights", 50), value("clarity", 50),
                         value("texture", 50), value("whites", 50), value("blacks", 50)});
  for (const auto size : {std::array<std::uint32_t, 2>{1, 1}, std::array<std::uint32_t, 2>{1, 5}, std::array<std::uint32_t, 2>{7, 1}}) {
    const auto result = run(d, scene(size[0], size[1]));
    CHECK(result.image.width == size[0] && result.image.height == size[1]);
  }
}

void test_spatial_falls_back_to_reference() {
  for (const char* channel : {"dehaze", "shadows", "highlights", "clarity", "texture", "whites", "blacks"}) {
    CHECK(PassRegistry::classify(global({value(channel, 30)}).operations[0]) == PassStatus::ReferenceOnly);
    CHECK(PassRegistry::classify(global({value(channel, 0), value("exposure", 0.3)}).operations[0]) == PassStatus::Implemented);
  }
}

// Masked local colour: the whole-frame stages see the whole frame, the mask chooses where it shows.
void test_spatial_local_masked() {
  const auto input = scene(32, 24);
  RenderInputs inputs;
  pixcake::gpu::RenderMask mask{"m", 32, 24, std::vector<std::uint8_t>(32 * 24, 0)};
  for (std::uint32_t y = 0; y < 24; ++y)
    for (std::uint32_t x = 0; x < 16; ++x) mask.alpha[y * 32 + x] = 255;
  inputs.masks.push_back(mask);
  const auto local = run(op("color.local", "pixcake.color.local", {value("shadows", 80), value("whites", 40)}, {"m"}), input, inputs);
  const auto whole = run(global({value("shadows", 80), value("whites", 40)}), input);
  for (std::uint32_t y = 0; y < 24; ++y)
    for (std::uint32_t x = 0; x < 32; ++x)
      for (int c = 0; c < 4; ++c) {
        const std::size_t i = (y * 32 + x) * 4 + c;
        CHECK(local.image.pixels[i] == (x < 16 ? whole.image.pixels[i] : input.pixels[i]));
      }
}

}  // namespace

int main() {
  test_spatial_falls_back_to_reference();
  test_spatial_order_matches_develop();
  test_spatial_tiny_frames();
  test_spatial_local_masked();
  test_spatial_preview_matches_export();
  std::printf("reference spatial ok\n");
  return 0;
}
