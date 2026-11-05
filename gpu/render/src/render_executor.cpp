#include "pixcake/gpu/render_executor.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace pixcake::gpu {
namespace {

struct Pixel { float r, g, b, a; };
float clamp01(float value) { return std::clamp(value, 0.0F, 1.0F); }
float get(const std::map<std::string, float>& values, const char* key) {
  const auto it = values.find(key);
  return it == values.end() ? 0.0F : it->second;
}
float luminance(Pixel p) { return 0.2126F * p.r + 0.7152F * p.g + 0.0722F * p.b; }
Pixel mix(Pixel a, Pixel b, float amount) {
  amount = clamp01(amount);
  return {a.r + (b.r - a.r) * amount, a.g + (b.g - a.g) * amount,
          a.b + (b.b - a.b) * amount, a.a + (b.a - a.a) * amount};
}
Pixel read(const RgbaImage& image, std::uint32_t x, std::uint32_t y) {
  const auto stride = image.row_stride == 0 ? image.width * 4 : image.row_stride;
  const auto i = static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 4;
  return {image.pixels[i] / 255.0F, image.pixels[i + 1] / 255.0F,
          image.pixels[i + 2] / 255.0F, image.pixels[i + 3] / 255.0F};
}
void write(RgbaImage& image, std::uint32_t x, std::uint32_t y, Pixel p) {
  const auto stride = image.row_stride == 0 ? image.width * 4 : image.row_stride;
  const auto i = static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 4;
  image.pixels[i] = static_cast<std::uint8_t>(std::lround(clamp01(p.r) * 255.0F));
  image.pixels[i + 1] = static_cast<std::uint8_t>(std::lround(clamp01(p.g) * 255.0F));
  image.pixels[i + 2] = static_cast<std::uint8_t>(std::lround(clamp01(p.b) * 255.0F));
  image.pixels[i + 3] = static_cast<std::uint8_t>(std::lround(clamp01(p.a) * 255.0F));
}
Pixel color(Pixel p, const std::map<std::string, float>& v) {
  const float t = get(v, "temperature") / 100.0F * 0.25F;
  const float m = get(v, "tint") / 100.0F * 0.18F;
  p.r += t + m; p.g -= m * 0.9F; p.b -= t - m;
  return p;
}
Pixel tone(Pixel p, const std::map<std::string, float>& v) {
  const float exposure = get(v, "exposure");
  const float contrast = get(v, "contrast") / 100.0F;
  const float brightness = get(v, "brightness") / 100.0F;
  p.r *= std::pow(2.0F, exposure); p.g *= std::pow(2.0F, exposure); p.b *= std::pow(2.0F, exposure);
  p.r += brightness; p.g += brightness; p.b += brightness;
  p.r = (p.r - 0.5F) * (1.0F + contrast) + 0.5F;
  p.g = (p.g - 0.5F) * (1.0F + contrast) + 0.5F;
  p.b = (p.b - 0.5F) * (1.0F + contrast) + 0.5F;
  const float l = luminance(p);
  const float hi = clamp01((l - 0.5F) * 2.0F), lo = clamp01((0.5F - l) * 2.0F);
  const float lift = get(v, "highlights") / 100.0F * hi * 0.45F + get(v, "shadows") / 100.0F * lo * 0.45F +
                     get(v, "whites") / 100.0F * hi * 0.2F + get(v, "blacks") / 100.0F * lo * 0.2F;
  p.r += lift; p.g += lift; p.b += lift;
  const float local = (get(v, "clarity") + get(v, "texture") + get(v, "dehaze")) / 300.0F * 0.35F;
  p.r = (p.r - 0.5F) * (1.0F + local) + 0.5F;
  p.g = (p.g - 0.5F) * (1.0F + local) + 0.5F;
  p.b = (p.b - 0.5F) * (1.0F + local) + 0.5F;
  const float saturation = 1.0F + get(v, "saturation") / 100.0F + get(v, "vibrance") / 100.0F * (1.0F - std::max({p.r, p.g, p.b}));
  const float gray = luminance(p);
  p.r = gray + (p.r - gray) * saturation; p.g = gray + (p.g - gray) * saturation; p.b = gray + (p.b - gray) * saturation;
  return p;
}
Pixel composite(Pixel base, Pixel top, float opacity, const std::string& mode) {
  opacity = clamp01(opacity) * top.a;
  Pixel mixed = top;
  if (mode == "multiply") mixed = {base.r * top.r, base.g * top.g, base.b * top.b, top.a};
  if (mode == "screen") mixed = {1.0F - (1.0F - base.r) * (1.0F - top.r), 1.0F - (1.0F - base.g) * (1.0F - top.g), 1.0F - (1.0F - base.b) * (1.0F - top.b), top.a};
  return mix(base, mixed, opacity);
}
const RenderMask* mask_for(const RenderInputs& inputs, const std::string& id) {
  for (const auto& mask : inputs.masks) if (mask.id == id) return &mask;
  return nullptr;
}

}  // namespace

bool RenderExecutor::renderReference(const pixcake::render::RenderDescription& description,
                                     const RgbaImage& input, const RenderInputs& inputs,
                                     RenderResult& result) const {
  result = {};
  const auto stride = input.row_stride == 0 ? input.width * 4 : input.row_stride;
  if (input.width == 0 || input.height == 0 || stride < input.width * 4 ||
      input.pixels.size() < static_cast<std::size_t>(stride) * input.height) {
    result.diagnostics.push_back({"", "输入 RGBA 帧为空或行距无效", true});
    return false;
  }
  result.image = input;
  result.image.row_stride = stride;
  result.image.revision = description.document_revision;
  for (const auto& operation : description.operations) {
    if (!operation.enabled) {
      result.diagnostics.push_back({operation.capability_id, operation.disabled_reason.empty() ? "操作已禁用" : operation.disabled_reason, false});
      continue;
    }
    std::vector<const RenderMask*> masks;
    bool missing = false;
    for (const auto& id : operation.mask_ids) {
      const auto* mask = mask_for(inputs, id);
      if (mask == nullptr || mask->width != input.width || mask->height != input.height || mask->alpha.size() < static_cast<std::size_t>(input.width) * input.height) { missing = true; break; }
      masks.push_back(mask);
    }
    if (missing) { result.diagnostics.push_back({operation.capability_id, "蒙版缺失或尺寸不匹配，跳过操作", false}); continue; }
    std::map<std::string, float> values;
    for (const auto& value : operation.values) values[value.channel] = static_cast<float>(value.numeric_value);
    for (std::uint32_t y = 0; y < input.height; ++y) for (std::uint32_t x = 0; x < input.width; ++x) {
      const Pixel before = read(result.image, x, y);
      Pixel after = before;
      if (operation.stage == "color" || operation.program.find("color") != std::string::npos) after = color(after, values);
      if (operation.stage == "tone" || operation.capability_id == "color.global" || operation.capability_id == "color.local" || operation.capability_id == "color.post") after = tone(after, values);
      if (operation.program.find("filter") != std::string::npos || operation.program.find("profile") != std::string::npos) {
        const float amount = clamp01(get(values, "amount") / 100.0F);
        after = mix(after, {after.r * 1.04F + 0.02F, after.g * 1.01F, after.b * 0.96F, after.a}, amount);
      }
      float weight = 1.0F;
      for (const auto* mask : masks) weight *= mask->alpha[static_cast<std::size_t>(y) * input.width + x] / 255.0F;
      write(result.image, x, y, composite(before, after, weight, operation.blend_mode));
    }
  }
  auto layers = inputs.layers;
  std::sort(layers.begin(), layers.end(), [](const RenderLayer& a, const RenderLayer& b) { return a.order < b.order; });
  for (const auto& layer : layers) {
    const auto layer_stride = layer.image.row_stride == 0 ? layer.image.width * 4 : layer.image.row_stride;
    if (!layer.visible || layer.image.width != input.width || layer.image.height != input.height || layer.image.pixels.size() < static_cast<std::size_t>(layer_stride) * layer.image.height) continue;
    const auto* mask = layer.mask_id.empty() ? nullptr : mask_for(inputs, layer.mask_id);
    if (!layer.mask_id.empty() && mask == nullptr) { result.diagnostics.push_back({"", "图层蒙版缺失，跳过图层", false}); continue; }
    for (std::uint32_t y = 0; y < input.height; ++y) for (std::uint32_t x = 0; x < input.width; ++x) {
      const float amount = layer.opacity * (mask == nullptr ? 1.0F : mask->alpha[static_cast<std::size_t>(y) * input.width + x] / 255.0F);
      write(result.image, x, y, composite(read(result.image, x, y), read(layer.image, x, y), amount, layer.blend_mode));
    }
  }
  result.success = true;
  return true;
}

bool RenderExecutor::execute(gl::Context& context, const pixcake::render::RenderDescription& description,
                             const RgbaImage& input, const RenderInputs& inputs, RenderResult& result,
                             std::string& error) const {
  if (!context.valid() || !context.makeCurrent()) { error = context.lastError(); return false; }
  const bool ok = renderReference(description, input, inputs, result);
  context.doneCurrent();
  if (!ok) error = "reference render failed";
  return ok;
}

}  // namespace pixcake::gpu
