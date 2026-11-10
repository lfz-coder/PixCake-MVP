#include <pixcake/gpu/render_executor.hpp>

#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <array>
#include <string>
#include <utility>

namespace {

pixcake::gpu::RgbaImage image(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  pixcake::gpu::RgbaImage result;
  result.width = 1; result.height = 1; result.row_stride = 4;
  result.pixels = {r, g, b, 255};
  return result;
}

pixcake::render::RenderDescription operation(
    const char* capability, const char* stage, const char* program,
    std::initializer_list<pixcake::render::RenderValue> values) {
  pixcake::render::RenderDescription description;
  description.document_revision = 77;
  pixcake::render::RenderOperation item;
  item.capability_id = capability; item.stage = stage; item.program = program;
  item.values.assign(values.begin(), values.end());
  description.operations.push_back(std::move(item));
  return description;
}

pixcake::render::RenderValue value(const char* channel, double numeric) {
  return {channel, channel, std::to_string(numeric), numeric};
}

class FakeResources final : public pixcake::gpu::ResourceProvider {
 public:
  explicit FakeResources(bool resolve, pixcake::gpu::ResourceKind kind = pixcake::gpu::ResourceKind::Lut)
      : resolve_(resolve), kind_(kind) {}
  bool resolve(std::string_view, pixcake::gpu::ResolvedResource& resource,
               std::string& error) const override {
    if (!resolve_) { error = "resource handle not found"; return false; }
    resource = {"fixture-resource", kind_, bytes_, sizeof(bytes_)};
    return true;
  }

 private:
  bool resolve_;
  pixcake::gpu::ResourceKind kind_;
  inline static const std::byte bytes_[4]{};
};

class StructuredLut final : public pixcake::gpu::ResourceProvider {
 public:
  explicit StructuredLut(std::uint64_t revision, bool identity = false) : revision_(revision), identity_(identity) {}
  bool resolve(std::string_view, pixcake::gpu::ResolvedResource&, std::string& error) const override {
    error = "opaque resource intentionally unavailable";
    return false;
  }
  bool resolveLut(std::string_view, pixcake::gpu::Lut3DView& view, std::string&) const override {
    const auto& samples = identity_ ? identity_samples_ : samples_;
    view = {"lut-2", 2, samples.data(), samples.size(), revision_};
    return true;
  }

 private:
  std::uint64_t revision_;
  bool identity_;
  inline static constexpr std::array<float, 24> samples_ = {
      1.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F,
      1.0F, 1.0F, 1.0F, 0.0F, 1.0F, 1.0F,
      1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F,
      1.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F};
  inline static constexpr std::array<float, 24> identity_samples_ = {
      0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
      0.0F, 1.0F, 0.0F, 1.0F, 1.0F, 0.0F,
      0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 1.0F,
      0.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F};
};

class ReferenceContext final : public pixcake::gl::detail::ContextImpl {
 public:
  bool make_current(std::string&) override { return true; }
  void done_current() override {}
  bool set_viewport(std::uint32_t, std::uint32_t, std::string&) override { return true; }
  bool draw_triangles(std::uint32_t, std::string&) override { return true; }
  bool read_pixels(std::uint32_t, std::uint32_t, std::vector<std::byte>&, std::string&) override { return false; }
  std::shared_ptr<pixcake::gl::detail::ShaderImpl> create_shader() override { return {}; }
  std::shared_ptr<pixcake::gl::detail::ProgramImpl> create_program() override { return {}; }
  std::shared_ptr<pixcake::gl::detail::Texture2DImpl> create_texture() override { return {}; }
  std::shared_ptr<pixcake::gl::detail::FramebufferImpl> create_framebuffer() override { return {}; }
  std::string last_error() const override { return {}; }
};

}  // namespace

int main() {
  pixcake::gpu::RenderExecutor executor;
  pixcake::gpu::RenderInputs inputs;
  pixcake::gpu::RenderResult result;

  auto global_probe = operation("color.global", "color", "pixcake.color.global", {});
  auto local_probe = operation("color.local", "color", "pixcake.color.local", {});
  auto filter_probe = operation("color.global", "color", "pixcake.filter", {});
  auto unknown_probe = operation("free", "free", "pixcake.curves", {});
  assert(pixcake::gpu::PassRegistry::classify(global_probe.operations[0]) == pixcake::gpu::PassStatus::Implemented);
  // Local colour shares the global tool's shader (readback in test_gpu_backend_C57_C60).
  assert(pixcake::gpu::PassRegistry::classify(local_probe.operations[0]) == pixcake::gpu::PassStatus::Implemented);
  assert(pixcake::gpu::PassRegistry::classify(filter_probe.operations[0]) == pixcake::gpu::PassStatus::MissingResource);
  assert(pixcake::gpu::PassRegistry::classify(unknown_probe.operations[0]) == pixcake::gpu::PassStatus::Unregistered);

  auto white_balance = operation("color.global", "color", "pixcake.color.global",
                                 {value("temperature", 100.0), value("tint", 0.0)});
  // 2026-11-05：本机原生 CGL 与 Qt 桌面 QPA 上 readback 对比通过（变异检查：去掉任一通道都会失败），
  // 色温、色调、对比度与曝光一起登记为 GPU Implemented。
  assert(pixcake::gpu::PassRegistry::classify(white_balance.operations[0]) ==
         pixcake::gpu::PassStatus::Implemented);
  assert(executor.renderReference(white_balance, image(128, 128, 128), inputs, result));
  assert(result.success && result.image.revision == 77);
  // PhotoCraft Camera Raw white balance (camera_raw::balanceGains): temperature +100 warms grey
  // in linear light at unit luminance.  (The former Bradford D65 -> D50 vector was 151/125/92.)
  assert(result.image.pixels[0] == 144 && result.image.pixels[1] == 125 && result.image.pixels[2] == 102);
  auto neutral_balance = operation("color.global", "color", "pixcake.color.global",
                                   {value("temperature", 0.0), value("tint", 0.0)});
  assert(executor.renderReference(neutral_balance, image(120, 100, 80), inputs, result));
  assert(result.image.pixels[0] == 120 && result.image.pixels[1] == 100 && result.image.pixels[2] == 80);
  auto tint = operation("color.global", "color", "pixcake.color.global",
                        {value("temperature", 0.0), value("tint", 50.0)});
  assert(pixcake::gpu::PassRegistry::classify(tint.operations[0]) == pixcake::gpu::PassStatus::Implemented);
  assert(executor.renderReference(tint, image(128, 128, 128), inputs, result));
  // Camera Raw tint: positive lowers green towards magenta.  (The former Bradford green-cone
  // vector went the other way: 95/156/126.)
  assert(result.image.pixels[0] == 134 && result.image.pixels[1] == 126 && result.image.pixels[2] == 134);

  auto exposure = operation("color.global", "tone", "pixcake.color.global",
                            {value("exposure", 1.0)});
  assert(executor.renderReference(exposure, image(64, 64, 64), inputs, result));
  // Camera Raw exposure doubles linear light: sRGB 64 -> 90 (the former byte doubling gave 128).
  assert(result.image.pixels[0] == 90);
  auto half_stop = operation("color.global", "tone", "pixcake.color.global",
                             {value("exposure", 0.5)});
  assert(executor.renderReference(half_stop, image(64, 64, 64), inputs, result));
  assert(result.image.pixels[0] == 76);  // former: 90-91

  auto contrast = operation("color.global", "tone", "pixcake.color.global",
                            {value("contrast", 100.0)});
  assert(pixcake::gpu::PassRegistry::classify(contrast.operations[0]) ==
         pixcake::gpu::PassStatus::Implemented);
  assert(executor.renderReference(contrast, image(64, 64, 64), inputs, result));
  assert(result.image.pixels[0] == 40);  // Camera Raw S-curve on luminance (former affine: <= 2)
  auto neutral_contrast = operation("color.global", "tone", "pixcake.color.global",
                                    {value("contrast", 0.0)});
  assert(executor.renderReference(neutral_contrast, image(64, 128, 192), inputs, result));
  assert(result.image.pixels[0] == 64 && result.image.pixels[1] == 128 && result.image.pixels[2] == 192);
  auto contrast_vector = operation("color.global", "tone", "pixcake.color.global",
                                   {value("contrast", 100.0)});
  assert(executor.renderReference(contrast_vector, image(64, 128, 192), inputs, result));
  // Camera Raw Contrast scales the pixel by its luminance's change, colour ratios kept.
  // (The former W3C affine vector was 1/129/255.)
  assert(result.image.pixels[0] == 61 && result.image.pixels[1] == 123 && result.image.pixels[2] == 184);

  // 创意色彩“程度”按官方 0..100（2026-11-05 用户裁定）：100 是满格，偏色 +0.04/+0.01/-0.03 全部生效。
  auto style_full = operation("color.style", "color", "pixcake.color.style", {value("amount", 100.0)});
  assert(executor.renderReference(style_full, image(128, 128, 128), inputs, result));
  assert(result.image.pixels[0] == 138 && result.image.pixels[1] == 131 && result.image.pixels[2] == 120);
  auto style_half = operation("color.style", "color", "pixcake.color.style", {value("amount", 50.0)});
  assert(executor.renderReference(style_half, image(128, 128, 128), inputs, result));
  assert(result.image.pixels[0] == 133 && result.image.pixels[1] == 129 && result.image.pixels[2] == 124);
  // AI 追色程度 0..200（需求原文）：100 是默认强度，200 是两倍强度。
  auto match_double = operation("color.match", "color", "pixcake.color.match", {value("amount", 200.0)});
  assert(executor.renderReference(match_double, image(128, 128, 128), inputs, result));
  assert(result.image.pixels[0] == 148 && result.image.pixels[1] == 133 && result.image.pixels[2] == 113);

  auto filter = operation("color.global", "color", "pixcake.filter",
                          {value("amount", 100.0)});
  assert(executor.renderReference(filter, image(128, 128, 128), inputs, result));
  assert(result.image.pixels[0] == 128 && result.image.pixels[2] == 128);
  assert(!result.statuses.empty() && result.statuses.front().status == pixcake::gpu::PassStatus::MissingResource);

  FakeResources resources(true);
  inputs.resources = &resources;
  assert(executor.renderReference(filter, image(128, 128, 128), inputs, result));
  assert(!result.statuses.empty() && result.statuses.front().status == pixcake::gpu::PassStatus::MissingResource);
  assert(result.image.pixels[0] == 128 && result.image.pixels[2] == 128);
  FakeResources missing_resources(false);
  inputs.resources = &missing_resources;
  assert(executor.renderReference(filter, image(128, 128, 128), inputs, result));
  assert(!result.statuses.empty() && result.statuses.front().status == pixcake::gpu::PassStatus::MissingResource);
  inputs.resources = nullptr;

  StructuredLut lut(77);
  inputs.resources = &lut;
  auto lut_filter = operation("color.global", "color", "pixcake.filter", {value("amount", 100.0)});
  assert(executor.renderReference(lut_filter, image(64, 128, 192), inputs, result));
  assert(result.statuses.front().status == pixcake::gpu::PassStatus::ReferenceOnly);
  assert(result.image.pixels[0] >= 190 && result.image.pixels[0] <= 192);
  assert(result.image.pixels[1] >= 127 && result.image.pixels[1] <= 129);
  assert(result.image.pixels[2] >= 62 && result.image.pixels[2] <= 64);
  StructuredLut identity_lut(77, true);
  inputs.resources = &identity_lut;
  assert(executor.renderReference(lut_filter, image(64, 128, 192), inputs, result));
  assert(result.image.pixels[0] == 64 && result.image.pixels[1] == 128 && result.image.pixels[2] == 192);

  auto half_lut = operation("color.global", "color", "pixcake.filter", {value("amount", 50.0)});
  inputs.resources = &lut;
  assert(executor.renderReference(half_lut, image(64, 128, 192), inputs, result));
  assert(result.image.pixels[0] >= 126 && result.image.pixels[0] <= 129);
  auto masked_lut = half_lut;
  masked_lut.operations[0].mask_ids = {"half"};
  inputs.masks.push_back({"half", 1, 1, {128}});
  assert(executor.renderReference(masked_lut, image(64, 128, 192), inputs, result));
  assert(result.image.pixels[0] >= 94 && result.image.pixels[0] <= 98);
  inputs.masks.clear();
  StructuredLut stale_lut(78);
  inputs.resources = &stale_lut;
  assert(executor.renderReference(lut_filter, image(64, 128, 192), inputs, result));
  assert(result.statuses.front().status == pixcake::gpu::PassStatus::MissingResource);
  assert(result.image.pixels[0] == 64);
  inputs.resources = nullptr;

  auto profile = operation("color.global", "color", "pixcake.profile", {value("amount", 100.0)});
  FakeResources wrong_kind(true, pixcake::gpu::ResourceKind::Lut);
  inputs.resources = &wrong_kind;
  assert(executor.renderReference(profile, image(128, 128, 128), inputs, result));
  assert(!result.statuses.empty() && result.statuses.front().status == pixcake::gpu::PassStatus::MissingResource);
  inputs.resources = nullptr;

  auto unknown = operation("color.global", "free", "pixcake.curves",
                           {value("amount", 100.0)});
  assert(executor.renderReference(unknown, image(128, 128, 128), inputs, result));
  assert(!result.statuses.empty() && result.statuses.front().status == pixcake::gpu::PassStatus::Unregistered);

  auto masked = operation("color.local", "color", "pixcake.color.local",
                          {value("temperature", 100.0)});
  masked.operations[0].mask_ids = {"zero"};
  inputs.masks.push_back({"zero", 1, 1, {0}});
  assert(executor.renderReference(masked, image(128, 128, 128), inputs, result));
  assert(result.image.pixels[0] == 128 && result.image.pixels[2] == 128);
  inputs.masks.clear();

  auto bad_blend = operation("color.global", "color", "pixcake.color.global",
                             {value("temperature", 100.0)});
  bad_blend.operations[0].blend_mode = "overlay";
  assert(executor.renderReference(bad_blend, image(128, 128, 128), inputs, result));
  assert(result.statuses.front().status == pixcake::gpu::PassStatus::InvalidInput);

  pixcake::gpu::RenderInputs layers;
  layers.layers.push_back({image(255, 0, 0), "", 0.5F, "normal", true, 1});
  layers.layers.push_back({image(0, 0, 255), "", 0.5F, "normal", true, 2});
  pixcake::render::RenderDescription empty;
  empty.document_revision = 88;
  assert(executor.renderReference(empty, image(0, 0, 0), layers, result));
  assert(result.image.revision == 88);
  assert(result.image.pixels[0] >= 63 && result.image.pixels[0] <= 65);
  assert(result.image.pixels[2] >= 126 && result.image.pixels[2] <= 129);

  pixcake::gpu::RenderInputs blend_layers;
  blend_layers.layers.push_back({image(255, 0, 0), "", 1.0F, "multiply", true, 2});
  blend_layers.layers.push_back({image(0, 0, 255), "", 0.5F, "screen", true, 1});
  assert(executor.renderReference(empty, image(128, 128, 128), blend_layers, result));
  // Order 1 (screen blue at 50%) is applied before order 2 (multiply red).
  assert(result.image.pixels[0] >= 127 && result.image.pixels[0] <= 129);
  assert(result.image.pixels[1] == 0);
  assert(result.image.pixels[2] == 0);

  pixcake::gpu::RenderInputs hidden_layer;
  hidden_layer.layers.push_back({image(255, 0, 0), "", 1.0F, "normal", false, 1});
  assert(executor.renderReference(empty, image(0, 0, 0), hidden_layer, result));
  assert(result.image.pixels[0] == 0 && result.image.pixels[1] == 0 && result.image.pixels[2] == 0);
  assert(result.statuses.size() == 1 && result.statuses.front().status == pixcake::gpu::PassStatus::Disabled);

  pixcake::gpu::RenderInputs masked_layer;
  masked_layer.layers.push_back({image(255, 0, 0), "half", 1.0F, "normal", true, 1});
  masked_layer.masks.push_back({"half", 1, 1, {128}});
  assert(executor.renderReference(empty, image(0, 0, 0), masked_layer, result));
  assert(result.image.pixels[0] >= 127 && result.image.pixels[0] <= 129);
  assert(result.statuses.size() == 1 && result.statuses.front().status == pixcake::gpu::PassStatus::Implemented);

  pixcake::gpu::RenderInputs bad_layer;
  auto wrong_size = image(255, 0, 0);
  wrong_size.width = 2;
  bad_layer.layers.push_back({wrong_size, "", 1.0F, "normal", true, 1});
  assert(executor.renderReference(empty, image(0, 0, 0), bad_layer, result));
  assert(result.image.pixels[0] == 0);
  assert(result.statuses.size() == 1 && result.statuses.front().status == pixcake::gpu::PassStatus::MissingResource);

  pixcake::gpu::RenderInputs bad_mask;
  bad_mask.layers.push_back({image(255, 0, 0), "missing", 1.0F, "normal", true, 1});
  assert(executor.renderReference(empty, image(0, 0, 0), bad_mask, result));
  assert(result.image.pixels[0] == 0);
  assert(result.statuses.size() == 1 && result.statuses.front().status == pixcake::gpu::PassStatus::MissingResource);

  auto reference_context_impl = std::make_shared<ReferenceContext>();
  pixcake::gl::Context reference_context(reference_context_impl);
  pixcake::gpu::RenderResult execute_result;
  std::string execute_error;
  // Work the shaders do not cover (a 3D LUT) falls back to the reference...
  pixcake::gpu::RenderInputs lut_inputs;
  lut_inputs.resources = &lut;
  assert(executor.execute(reference_context, lut_filter, image(64, 128, 192), lut_inputs, execute_result,
                          execute_error));
  assert(execute_result.reference_fallback);
  assert(execute_result.image.pixels[0] >= 190 && execute_result.image.pixels[0] <= 192);
  assert(!execute_result.diagnostics.empty());
  // ...while a context that cannot make GPU resources fails with an error;
  // renderd then draws with the reference itself.
  execute_error.clear();
  assert(!executor.execute(reference_context, empty, image(0, 0, 0), masked_layer, execute_result, execute_error));
  assert(!execute_error.empty());
  return 0;
}
