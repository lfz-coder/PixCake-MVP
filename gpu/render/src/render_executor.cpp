#include "pixcake/gpu/render_executor.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string_view>

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
  const float style = clamp01(get(v, "amount") / 200.0F);
  p.r += style * 0.04F; p.g += style * 0.01F; p.b -= style * 0.03F;
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
bool valid_lut(const Lut3DView& lut) {
  if (lut.id.empty() || lut.size < 2 || lut.rgb == nullptr || lut.revision == 0) return false;
  const auto required = static_cast<std::size_t>(lut.size) * lut.size * lut.size * 3;
  if (lut.sample_count < required) return false;
  for (std::size_t i = 0; i < required; ++i) if (!std::isfinite(lut.rgb[i])) return false;
  return true;
}
Pixel lut_sample(const Lut3DView& lut, Pixel input) {
  const float coordinate[3] = {clamp01(input.r) * (lut.size - 1), clamp01(input.g) * (lut.size - 1), clamp01(input.b) * (lut.size - 1)};
  const std::uint32_t base[3] = {static_cast<std::uint32_t>(coordinate[0]), static_cast<std::uint32_t>(coordinate[1]), static_cast<std::uint32_t>(coordinate[2])};
  const float fraction[3] = {coordinate[0] - base[0], coordinate[1] - base[1], coordinate[2] - base[2]};
  auto sample = [&](std::uint32_t r, std::uint32_t g, std::uint32_t b) {
    const auto index = (static_cast<std::size_t>(b) * lut.size * lut.size + static_cast<std::size_t>(g) * lut.size + r) * 3;
    return Pixel{lut.rgb[index], lut.rgb[index + 1], lut.rgb[index + 2], input.a};
  };
  const std::uint32_t hi[3] = {std::min(base[0] + 1, lut.size - 1), std::min(base[1] + 1, lut.size - 1), std::min(base[2] + 1, lut.size - 1)};
  Pixel c000 = sample(base[0], base[1], base[2]);
  Pixel c100 = sample(hi[0], base[1], base[2]);
  Pixel c010 = sample(base[0], hi[1], base[2]);
  Pixel c110 = sample(hi[0], hi[1], base[2]);
  Pixel c001 = sample(base[0], base[1], hi[2]);
  Pixel c101 = sample(hi[0], base[1], hi[2]);
  Pixel c011 = sample(base[0], hi[1], hi[2]);
  Pixel c111 = sample(hi[0], hi[1], hi[2]);
  const Pixel x00 = mix(c000, c100, fraction[0]);
  const Pixel x10 = mix(c010, c110, fraction[0]);
  const Pixel x01 = mix(c001, c101, fraction[0]);
  const Pixel x11 = mix(c011, c111, fraction[0]);
  const Pixel y0 = mix(x00, x10, fraction[1]);
  const Pixel y1 = mix(x01, x11, fraction[1]);
  return mix(y0, y1, fraction[2]);
}

}  // namespace

PassStatus PassRegistry::classify(const pixcake::render::RenderOperation& operation) {
  if (!operation.enabled) return PassStatus::Disabled;
  if (operation.program.find("filter") != std::string::npos || operation.program.find("profile") != std::string::npos) return PassStatus::MissingResource;
  if (operation.program == "pixcake.color.global") return gpuSupported(operation) ? PassStatus::Implemented : PassStatus::ReferenceOnly;
  if (operation.program == "pixcake.color.local" || operation.program == "pixcake.color.post" || operation.program == "pixcake.color.match" || operation.program == "pixcake.color.style") return PassStatus::ReferenceOnly;
  return PassStatus::Unregistered;
}

bool PassRegistry::gpuSupported(const pixcake::render::RenderOperation& operation) {
  if (operation.capability_id != "color.global" || operation.blend_mode != "normal" || !operation.mask_ids.empty()) return false;
  static const std::set<std::string_view> channels = {"temperature", "tint", "exposure", "contrast"};
  for (const auto& value : operation.values) if (channels.find(value.channel) == channels.end() && std::abs(value.numeric_value) > 1e-9) return false;
  return true;
}

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
  if (input.revision != 0 && input.revision != description.document_revision) {
    result.diagnostics.push_back({"", "输入帧修订号与绘制描述不一致", true});
    result.statuses.push_back({"", "frame", PassStatus::InvalidInput, "输入帧修订号与绘制描述不一致"});
    return false;
  }
  result.image = input;
  result.image.row_stride = stride;
  result.image.revision = description.document_revision;
  for (const auto& diagnostic : description.diagnostics) {
    result.diagnostics.push_back({diagnostic.capability_id, diagnostic.reason, diagnostic.blocking});
  }
  for (const auto& operation : description.operations) {
    const auto status = PassRegistry::classify(operation);
    PassStatusRecord record{operation.capability_id, operation.program, status, {}};
    if (status == PassStatus::Disabled) record.reason = operation.disabled_reason.empty() ? "操作已禁用" : operation.disabled_reason;
    if (status == PassStatus::MissingResource) record.reason = "滤镜或配置文件资源未解析，跳过操作";
    if (status == PassStatus::Unregistered) record.reason = "渲染程序未注册，跳过操作";
    if (status == PassStatus::ReferenceOnly) record.reason = "仅有 reference 实现";
    result.statuses.push_back(record);
    Lut3DView lut;
    bool has_lut = false;
    if (status == PassStatus::MissingResource && inputs.resources != nullptr) {
      std::string lut_error;
      if (inputs.resources->resolveLut(operation.program, lut, lut_error) && valid_lut(lut) && lut.revision == description.document_revision) {
        has_lut = true;
        result.statuses.back().status = PassStatus::ReferenceOnly;
        result.statuses.back().reason = "结构化 3D LUT 已解析，由 reference 三线性采样";
        result.diagnostics.push_back({operation.capability_id, result.statuses.back().reason, false});
      } else {
        result.diagnostics.push_back({operation.capability_id, lut_error.empty() ? record.reason : lut_error, false});
        continue;
      }
    }
    if (status == PassStatus::Unregistered || (status == PassStatus::MissingResource && !has_lut)) {
      result.diagnostics.push_back({operation.capability_id, record.reason, false});
      continue;
    }
    if (!operation.enabled) {
      result.diagnostics.push_back({operation.capability_id, operation.disabled_reason.empty() ? "操作已禁用" : operation.disabled_reason, false});
      continue;
    }
    if (operation.blend_mode != "normal" && operation.blend_mode != "multiply" && operation.blend_mode != "screen") {
      result.statuses.back().status = PassStatus::InvalidInput;
      result.statuses.back().reason = "未知混合模式，跳过操作";
      result.diagnostics.push_back({operation.capability_id, "未知混合模式，跳过操作", false});
      continue;
    }
    std::vector<const RenderMask*> masks;
    bool missing = false;
    for (const auto& id : operation.mask_ids) {
      const auto* mask = mask_for(inputs, id);
      if (mask == nullptr || mask->width != input.width || mask->height != input.height || mask->alpha.size() < static_cast<std::size_t>(input.width) * input.height) { missing = true; break; }
      masks.push_back(mask);
    }
    if (missing) { result.statuses.back().status = PassStatus::MissingResource; result.statuses.back().reason = "蒙版缺失或尺寸不匹配，跳过操作"; result.diagnostics.push_back({operation.capability_id, "蒙版缺失或尺寸不匹配，跳过操作", false}); continue; }
    std::map<std::string, float> values;
    for (const auto& value : operation.values) values[value.channel] = static_cast<float>(value.numeric_value);
    const bool is_color = operation.stage == "color" || operation.program.find("color") != std::string::npos;
    const bool is_tone = operation.stage == "tone" || operation.capability_id == "color.global" || operation.capability_id == "color.local" || operation.capability_id == "color.post";
    if (!is_color && !is_tone && !has_lut) continue;
    for (std::uint32_t y = 0; y < input.height; ++y) for (std::uint32_t x = 0; x < input.width; ++x) {
      const Pixel before = read(result.image, x, y);
      Pixel after = before;
      if (has_lut) {
        const float amount = values.find("amount") == values.end() ? 1.0F : clamp01(get(values, "amount") / 100.0F);
        after = mix(before, lut_sample(lut, before), amount);
      }
      if (!has_lut && is_color) after = color(after, values);
      if (!has_lut && is_tone) after = tone(after, values);
      float weight = 1.0F;
      for (const auto* mask : masks) weight *= mask->alpha[static_cast<std::size_t>(y) * input.width + x] / 255.0F;
      write(result.image, x, y, composite(before, after, weight, operation.blend_mode));
    }
  }
  auto layers = inputs.layers;
  std::sort(layers.begin(), layers.end(), [](const RenderLayer& a, const RenderLayer& b) { return a.order < b.order; });
  for (const auto& layer : layers) {
    const auto layer_stride = layer.image.row_stride == 0 ? layer.image.width * 4 : layer.image.row_stride;
    const std::string layer_id = "layer:" + std::to_string(layer.order);
    if (!layer.visible) { result.statuses.push_back({layer_id, "layer", PassStatus::Disabled, "图层已隐藏"}); continue; }
    if (layer.image.width != input.width || layer.image.height != input.height || layer_stride < input.width * 4 || layer.image.pixels.size() < static_cast<std::size_t>(layer_stride) * layer.image.height) {
      result.statuses.push_back({layer_id, "layer", PassStatus::MissingResource, "图层尺寸或行距无效，跳过图层"});
      result.diagnostics.push_back({layer_id, "图层尺寸或行距无效，跳过图层", false});
      continue;
    }
    if (layer.blend_mode != "normal" && layer.blend_mode != "multiply" && layer.blend_mode != "screen") {
      result.statuses.push_back({layer_id, "layer", PassStatus::InvalidInput, "未知图层混合模式，跳过图层"});
      result.diagnostics.push_back({layer_id, "未知图层混合模式，跳过图层", false});
      continue;
    }
    const auto* mask = layer.mask_id.empty() ? nullptr : mask_for(inputs, layer.mask_id);
    if (!layer.mask_id.empty() && (mask == nullptr || mask->width != input.width || mask->height != input.height || mask->alpha.size() < static_cast<std::size_t>(input.width) * input.height)) { result.statuses.push_back({layer_id, "layer", PassStatus::MissingResource, "图层蒙版缺失或尺寸不匹配，跳过图层"}); result.diagnostics.push_back({layer_id, "图层蒙版缺失或尺寸不匹配，跳过图层", false}); continue; }
    result.statuses.push_back({layer_id, "layer", PassStatus::Implemented, {}});
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
  bool gpu_supported = inputs.layers.empty() && inputs.masks.empty() && input.row_stride == input.width * 4;
  std::map<std::string, float> values;
  for (const auto& operation : description.operations) {
    if (!operation.enabled || operation.capability_id != "color.global" ||
        (!operation.mask_ids.empty()) || operation.blend_mode != "normal") { gpu_supported = false; continue; }
    for (const auto& value : operation.values) {
      if (value.channel != "temperature" && value.channel != "tint" && value.channel != "exposure" && value.channel != "contrast") {
        if (std::abs(value.numeric_value) > 1e-9) gpu_supported = false;
      } else values[value.channel] = static_cast<float>(value.numeric_value);
    }
  }
  if (!gpu_supported) {
    const bool ok = renderReference(description, input, inputs, result);
    result.diagnostics.push_back({"", "当前绘制描述包含 GPU 最小路径未覆盖的能力，已回退 reference", false});
    context.doneCurrent();
    if (!ok) error = "reference render failed";
    return ok;
  }

  bool ok = false;
  {
    auto shader = context.createShader();
    auto fragment = context.createShader();
    auto program = context.createProgram();
    auto source = context.createTexture2D();
    auto target = context.createTexture2D();
    auto framebuffer = context.createFramebuffer();
    const char* vertex_source = "#version 330 core\nout vec2 uv;\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2.0-1.0,0,1);}";
    const char* fragment_source = "#version 330 core\nin vec2 uv;out vec4 color;uniform sampler2D source;uniform vec4 adjust;\nvoid main(){vec4 p=texture(source,uv);float t=adjust.x/100.0*0.25;float m=adjust.y/100.0*0.18;p.r+=t+m;p.g-=m*0.9;p.b-=t-m;p.rgb*=pow(2.0,adjust.z);p.rgb=(p.rgb-0.5)*(1.0+adjust.w/100.0)+0.5;color=clamp(p,0.0,1.0);} ";
    if (shader.valid() && fragment.valid() && program.valid() && source.valid() && target.valid() && framebuffer.valid()) {
      ok = shader.compile(gl::ShaderStage::Vertex, vertex_source, error) && fragment.compile(gl::ShaderStage::Fragment, fragment_source, error) && program.link(shader, fragment, error) && program.use(error);
      const gl::ImageView view{input.width, input.height, input.row_stride, gl::PixelFormat::Rgba8, reinterpret_cast<const std::byte*>(input.pixels.data()), input.pixels.size()};
      ok = ok && source.upload(view, error) && target.allocate(input.width, input.height, gl::PixelFormat::Rgba8, error) && framebuffer.attachColor(target, error) && framebuffer.complete(error) && framebuffer.bind(error) && context.setViewport(input.width, input.height, error) && source.bind(0, error);
      ok = ok && program.setTextureUnit("source", 0, error) && program.setVec4("adjust", {values["temperature"], values["tint"], values["exposure"], values["contrast"]}, error) && context.drawTriangles(3, error);
      std::vector<std::byte> pixels;
      ok = ok && context.readPixels(input.width, input.height, pixels, error);
      if (ok) {
        result = {};
        result.image = input; result.image.revision = description.document_revision; result.image.pixels.resize(pixels.size());
        std::transform(pixels.begin(), pixels.end(), result.image.pixels.begin(), [](std::byte value) { return static_cast<std::uint8_t>(value); });
        result.success = true;
      }
    } else error = "GPU resources unavailable";
  }
  context.doneCurrent();
  if (!ok && error.empty()) error = "GPU pass failed";
  return ok;
}

}  // namespace pixcake::gpu
