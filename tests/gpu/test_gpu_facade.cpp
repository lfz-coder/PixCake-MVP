#include <pixcake/gpu/render_executor.hpp>

#include <cassert>
#include <cstdint>
#include <initializer_list>
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
  assert(pixcake::gpu::PassRegistry::classify(local_probe.operations[0]) == pixcake::gpu::PassStatus::ReferenceOnly);
  assert(pixcake::gpu::PassRegistry::classify(filter_probe.operations[0]) == pixcake::gpu::PassStatus::MissingResource);
  assert(pixcake::gpu::PassRegistry::classify(unknown_probe.operations[0]) == pixcake::gpu::PassStatus::Unregistered);

  auto white_balance = operation("color.global", "color", "pixcake.color.global",
                                 {value("temperature", 100.0), value("tint", 0.0)});
  assert(executor.renderReference(white_balance, image(128, 128, 128), inputs, result));
  assert(result.success && result.image.revision == 77);
  assert(result.image.pixels[0] > result.image.pixels[2]);

  auto exposure = operation("color.global", "tone", "pixcake.color.global",
                            {value("exposure", 1.0)});
  assert(executor.renderReference(exposure, image(64, 64, 64), inputs, result));
  assert(result.image.pixels[0] >= 126 && result.image.pixels[0] <= 130);

  auto contrast = operation("color.global", "tone", "pixcake.color.global",
                            {value("contrast", 100.0)});
  assert(executor.renderReference(contrast, image(64, 64, 64), inputs, result));
  assert(result.image.pixels[0] <= 2);

  auto filter = operation("color.global", "color", "pixcake.filter",
                          {value("amount", 100.0)});
  assert(executor.renderReference(filter, image(128, 128, 128), inputs, result));
  assert(result.image.pixels[0] == 128 && result.image.pixels[2] == 128);
  assert(!result.statuses.empty() && result.statuses.front().status == pixcake::gpu::PassStatus::MissingResource);

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
  return 0;
}
