// Ported from PhotoCraft crates/algo/src/camera_raw.rs, camera_raw/local_tone.rs,
// camera_raw/whites_blacks.rs, fxutil.rs and photo_util.rs (bilinear)
// @ 885a6ddf27cc58c07faf1722e5c327cd38953657, MIT (elected from MIT OR Apache-2.0).
// Copyright (c) 2026 ArtCraft Team and the PhotoCraft contributors.
// Licence text: third_party/photocraft-LICENSES/LICENSE-MIT.
// Modified: rewritten as OpenGL 3.3 fragment passes over float textures; the C++ reference
// (camera_raw_spatial.cpp) is the oracle.  Image statistics (quantiles, sorting) stay on the CPU.
#include <pixcake/gpu/camera_raw_gl.hpp>

#include <pixcake/gpu/camera_raw_spatial.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <map>

#include "camera_raw_gl_shaders.hpp"

namespace pixcake::gpu::camera_raw_gl {

struct Slot {
  gl::Texture2D tex{nullptr};
  gl::Framebuffer fb{nullptr};
  std::uint32_t w = 0, h = 0;
  gl::PixelFormat format = gl::PixelFormat::R32F;
};

namespace {

// Free slots by size and format; shared with the planes' deleters so a plane dropped after its
// Gl only frees its texture.
struct Pool {
  std::multimap<std::tuple<std::uint32_t, std::uint32_t, int>, std::unique_ptr<Slot>> free;
};

}  // namespace

struct Gl::Impl {
  explicit Impl(const gl::Context& c) : context(c) {}
  gl::Context context;  // copies share one context
  std::string error;
  bool failed = false;
  gl::Shader vertex{nullptr};
  std::map<const char*, gl::Program> programs;
  std::shared_ptr<Pool> pool = std::make_shared<Pool>();

  bool fail(const std::string& what) {
    if (!failed) error = what;
    failed = true;
    return false;
  }

  bool acquire(std::uint32_t w, std::uint32_t h, gl::PixelFormat format, Plane& out) {
    if (failed) return false;
    if (w == 0 || h == 0) return fail("plane size is zero");
    const auto key = std::make_tuple(w, h, static_cast<int>(format));
    std::unique_ptr<Slot> slot;
    const auto it = pool->free.find(key);
    if (it != pool->free.end()) {
      slot = std::move(it->second);
      pool->free.erase(it);
    } else {
      slot = std::make_unique<Slot>();
      slot->tex = context.createTexture2D();
      slot->fb = context.createFramebuffer();
      slot->w = w;
      slot->h = h;
      slot->format = format;
      std::string e;
      if (!slot->tex.valid() || !slot->fb.valid() || !slot->tex.allocate(w, h, format, e) || !slot->fb.attachColor(slot->tex, e) ||
          !slot->fb.complete(e)) {
        return fail("float plane: " + e);
      }
    }
    std::weak_ptr<Pool> weak = pool;
    out.slot = std::shared_ptr<Slot>(slot.release(), [weak, key](Slot* s) {
      if (auto p = weak.lock()) {
        p->free.emplace(key, std::unique_ptr<Slot>(s));
      } else {
        delete s;
      }
    });
    out.w = w;
    out.h = h;
    return true;
  }

  gl::Program* program(const char* body) {
    auto it = programs.find(body);
    if (it != programs.end()) return &it->second;
    std::string e;
    if (!vertex.valid()) {
      vertex = context.createShader();
      if (!vertex.valid() || !vertex.compile(gl::ShaderStage::Vertex, shaders::kVertex, e)) {
        fail("vertex shader: " + e);
        return nullptr;
      }
    }
    gl::Shader fragment = context.createShader();
    gl::Program linked = context.createProgram();
    if (!fragment.valid() || !linked.valid() ||
        !fragment.compile(gl::ShaderStage::Fragment, std::string(shaders::kHeader) + body, e) || !linked.link(vertex, fragment, e)) {
      fail("fragment shader: " + e);
      return nullptr;
    }
    return &programs.emplace(body, std::move(linked)).first->second;
  }

  // Draws `body` into `target` with inputs bound to t0, t1, ...; `uniforms` sets the rest.
  bool draw(const char* body, const Plane& target, std::initializer_list<const Plane*> inputs,
            const std::function<bool(gl::Program&, std::string&)>& uniforms = {}) {
    if (failed) return false;
    gl::Program* p = program(body);
    if (p == nullptr) return false;
    std::string e;
    bool ok = p->use(e) && target.slot->fb.bind(e) && context.setViewport(target.w, target.h, e);
    std::uint32_t unit = 0;
    for (const Plane* in : inputs) {
      static const char* const kNames[] = {"t0", "t1", "t2", "t3"};
      ok = ok && in->valid() && in->slot->tex.bind(unit, e) && p->setTextureUnit(kNames[unit], unit, e);
      ++unit;
    }
    ok = ok && (!uniforms || uniforms(*p, e)) && context.drawTriangles(3, e);
    return ok || fail(std::string("draw: ") + e);
  }

  bool read(const Plane& p, gl::PixelFormat format, std::vector<std::byte>& bytes) {
    if (failed) return false;
    std::string e;
    if (!p.valid() || !p.slot->fb.bind(e) || !context.readPixels(p.w, p.h, format, bytes, e)) return fail("readback: " + e);
    return true;
  }
};

Gl::Gl(const gl::Context& context) : impl_(std::make_unique<Impl>(context)) {}
Gl::~Gl() = default;
bool Gl::ok() const { return !impl_->failed; }
void Gl::trim() { impl_->pool->free.clear(); }
std::size_t Gl::pooledBytes() const {
  std::size_t bytes = 0;
  for (const auto& entry : impl_->pool->free) bytes += std::size_t{entry.second->w} * entry.second->h * gl::bytesPerPixel(entry.second->format);
  return bytes;
}
const std::string& Gl::error() const { return impl_->error; }

namespace {

using gl::PixelFormat;

bool same_size_plane(Gl::Impl& g, const Plane& like, PixelFormat format, Plane& out) { return g.acquire(like.w, like.h, format, out); }

PixelFormat format_of(const Plane& p) { return p.slot->format; }

// Separable box mean of every channel, edge-clamped.
bool box(Gl::Impl& g, const Plane& in, std::size_t r, Plane& out) {
  if (r == 0) {
    Plane copy;
    if (!same_size_plane(g, in, format_of(in), copy) || !g.draw(shaders::kCopy, copy, {&in})) return false;
    out = copy;
    return true;
  }
  Plane rows, cols;
  const auto ri = static_cast<std::int32_t>(r);
  const auto pass = [ri](int axis) {
    return [ri, axis](gl::Program& p, std::string& e) { return p.setInt("radius", ri, e) && p.setInt("axis", axis, e); };
  };
  if (!same_size_plane(g, in, format_of(in), rows) || !g.draw(shaders::kBox, rows, {&in}, pass(0)) ||
      !same_size_plane(g, in, format_of(in), cols) || !g.draw(shaders::kBox, cols, {&rows}, pass(1))) {
    return false;
  }
  out = cols;
  return true;
}

// The guided filter's last steps on (I, p, Ip, II) packed in `packed`, whose means are taken
// with radius r; `guide` is I at full size and `scale` the subsampling (1: none).
bool guided_from_packed(Gl::Impl& g, const Plane& packed, const Plane& guide, std::size_t r, float eps, float scale, Plane& out,
                        std::array<float, 2> factor = {0.0F, 0.0F}) {
  Plane means, ab, mab, result;
  if (!box(g, packed, r, means) || !same_size_plane(g, packed, PixelFormat::Rgba32F, ab) ||
      !g.draw(shaders::kGuidedAb, ab, {&means}, [eps](gl::Program& p, std::string& e) { return p.setFloat("eps", eps, e); }) ||
      !box(g, ab, r, mab) || !same_size_plane(g, guide, PixelFormat::R32F, result) ||
      !g.draw(shaders::kGuidedOut, result, {&mab, &guide}, [scale, factor](gl::Program& p, std::string& e) {
        return p.setFloat("scale", scale, e) && (scale != 0.0F || p.setVec4("factor_v", {factor[0], factor[1], 0.0F, 0.0F}, e));
      })) {
    return false;
  }
  out = result;
  return true;
}

bool block_mean(Gl::Impl& g, const Plane& in, std::uint32_t block, PixelFormat format, Plane& out, bool clamp_in = false) {
  Plane small;
  const std::uint32_t w = (in.w + block - 1) / block, h = (in.h + block - 1) / block;
  if (!g.acquire(w, h, format, small) ||
      !g.draw(shaders::kBlockMean, small, {&in}, [block, clamp_in](gl::Program& p, std::string& e) {
        return p.setInt("block", static_cast<std::int32_t>(block), e) && p.setInt("clamp_in", clamp_in ? 1 : 0, e);
      })) {
    return false;
  }
  out = small;
  return true;
}

bool luma(Gl::Impl& g, const Plane& rgba, bool clipped, Plane& out) {
  Plane l;
  if (!same_size_plane(g, rgba, PixelFormat::R32F, l) ||
      !g.draw(shaders::kLuma, l, {&rgba}, [clipped](gl::Program& p, std::string& e) { return p.setInt("clipped", clipped ? 1 : 0, e); })) {
    return false;
  }
  out = l;
  return true;
}

bool read_rgba(Gl::Impl& g, const Plane& p, std::vector<std::array<float, 4>>& out) {
  std::vector<std::byte> bytes;
  if (!g.read(p, PixelFormat::Rgba32F, bytes)) return false;
  out.resize(std::size_t{p.w} * p.h);
  std::memcpy(out.data(), bytes.data(), out.size() * sizeof(out[0]));
  return true;
}

std::size_t div_ceil(std::size_t a, std::size_t b) { return (a + b - 1) / b; }

}  // namespace

bool allocate(Gl& gl_, std::uint32_t w, std::uint32_t h, gl::PixelFormat format, Plane& out) { return gl_.impl().acquire(w, h, format, out); }
gl::Framebuffer& framebufferOf(const Plane& p) { return p.slot->fb; }
gl::Texture2D& textureOf(const Plane& p) { return p.slot->tex; }

bool upload(Gl& gl_, const std::vector<float>& v, std::uint32_t w, std::uint32_t h, Plane& out) {
  auto& g = gl_.impl();
  if (v.size() != std::size_t{w} * h) return g.fail("upload: size mismatch");
  Plane p;
  if (!g.acquire(w, h, PixelFormat::R32F, p)) return false;
  const gl::ImageView view{w, h, w * 4, PixelFormat::R32F, reinterpret_cast<const std::byte*>(v.data()), v.size() * sizeof(float)};
  std::string e;
  if (!p.slot->tex.upload(view, e)) return g.fail("upload: " + e);
  out = p;
  return true;
}

bool download(Gl& gl_, const Plane& p, std::vector<float>& out) {
  auto& g = gl_.impl();
  std::vector<std::byte> bytes;
  if (!g.read(p, PixelFormat::R32F, bytes)) return false;
  out.resize(std::size_t{p.w} * p.h);
  std::memcpy(out.data(), bytes.data(), out.size() * sizeof(float));
  return true;
}

bool boxMean(Gl& gl_, const Plane& in, std::size_t r, Plane& out) { return box(gl_.impl(), in, r, out); }

bool gauss(Gl& gl_, const Plane& in, float sigma, Plane& out) {
  auto& g = gl_.impl();
  Plane cur = in;
  if (sigma >= 0.3F) {
    for (const std::size_t r : camera_raw::gaussBoxRadii(sigma)) {
      Plane next;
      if (!box(g, cur, r, next)) return false;
      cur = next;
    }
  } else if (!box(g, in, 0, cur)) {
    return false;
  }
  out = cur;
  return true;
}

bool guided(Gl& gl_, const Plane& i, const Plane& p, std::size_t r, float eps, Plane& out) {
  auto& g = gl_.impl();
  Plane packed;
  if (!same_size_plane(g, i, PixelFormat::Rgba32F, packed) || !g.draw(shaders::kGuidedPack, packed, {&i, &p})) return false;
  return guided_from_packed(g, packed, i, r, eps, 1.0F, out);
}

bool guidedFast(Gl& gl_, const Plane& i, const Plane& p, std::size_t r, float eps, Plane& out) {
  auto& g = gl_.impl();
  const std::size_t s = std::clamp<std::size_t>(r / 8, 1, 8);
  if (s == 1) return guided(gl_, i, p, r, eps, out);
  // Means of I and p over s x s cells first, then the products on the small copy (as the CPU).
  Plane si, sp, packed;
  if (!block_mean(g, i, static_cast<std::uint32_t>(s), PixelFormat::R32F, si) ||
      !block_mean(g, p, static_cast<std::uint32_t>(s), PixelFormat::R32F, sp) ||
      !same_size_plane(g, si, PixelFormat::Rgba32F, packed) || !g.draw(shaders::kGuidedPack, packed, {&si, &sp})) {
    return false;
  }
  return guided_from_packed(g, packed, i, std::max<std::size_t>(r / s, 1), eps, static_cast<float>(s), out);
}

bool rankFilter(Gl& gl_, const Plane& in, std::size_t r, bool take_max, Plane& out) {
  auto& g = gl_.impl();
  Plane rows, cols;
  const auto pass = [r, take_max](int axis) {
    return [r, take_max, axis](gl::Program& p, std::string& e) {
      return p.setInt("radius", static_cast<std::int32_t>(r), e) && p.setInt("axis", axis, e) && p.setInt("take_max", take_max ? 1 : 0, e);
    };
  };
  if (!same_size_plane(g, in, PixelFormat::R32F, rows) || !g.draw(shaders::kRank, rows, {&in}, pass(0)) ||
      !same_size_plane(g, in, PixelFormat::R32F, cols) || !g.draw(shaders::kRank, cols, {&rows}, pass(1))) {
    return false;
  }
  out = cols;
  return true;
}

bool uploadFrame(Gl& gl_, const camera_raw::Frame& frame, Plane& rgba) {
  auto& g = gl_.impl();
  if (frame.px.size() != std::size_t{frame.width} * frame.height) return g.fail("uploadFrame: size mismatch");
  Plane p;
  if (!g.acquire(frame.width, frame.height, PixelFormat::Rgba32F, p)) return false;
  const gl::ImageView view{frame.width, frame.height, frame.width * 16, PixelFormat::Rgba32F,
                           reinterpret_cast<const std::byte*>(frame.px.data()), frame.px.size() * sizeof(frame.px[0])};
  std::string e;
  if (!p.slot->tex.upload(view, e)) return g.fail("uploadFrame: " + e);
  rgba = p;
  return true;
}

bool downloadFrame(Gl& gl_, const Plane& rgba, camera_raw::Frame& out) {
  out.width = rgba.w;
  out.height = rgba.h;
  return read_rgba(gl_.impl(), rgba, out.px);
}

bool dehaze(Gl& gl_, Plane& rgba, float amount) {
  auto& g = gl_.impl();
  const std::size_t w = rgba.w, h = rgba.h;
  // Transmission on a <= 768 px proxy (camera_raw::dehaze, same steps).
  const std::size_t k = std::max<std::size_t>(div_ceil(std::max(w, h), 768), 1);
  Plane small, cmin, dark;
  if (!block_mean(g, rgba, static_cast<std::uint32_t>(k), PixelFormat::Rgba32F, small, true)) return false;
  const std::size_t sw = small.w, sh = small.h, n = sw * sh;
  const std::size_t r = std::max<std::size_t>(std::max(sw, sh) / 60, 2);
  if (!same_size_plane(g, small, PixelFormat::R32F, cmin) || !g.draw(shaders::kChannelMin, cmin, {&small}) ||
      !rankFilter(gl_, cmin, r, false, dark)) {
    return false;
  }
  // Atmospheric light on the CPU: the mean colour of the brightest 0.1 % of the dark channel.
  std::vector<float> dark_v;
  std::vector<std::array<float, 4>> small_v;
  if (!download(gl_, dark, dark_v) || !read_rgba(g, small, small_v)) return false;
  std::vector<std::size_t> idx(n);
  for (std::size_t i = 0; i < n; ++i) idx[i] = i;
  std::stable_sort(idx.begin(), idx.end(), [&](std::size_t a, std::size_t b) { return dark_v[b] < dark_v[a]; });
  const std::size_t top = std::max<std::size_t>(n / 1000, 1);
  std::array<float, 3> air{0.0F, 0.0F, 0.0F};
  for (std::size_t t = 0; t < top; ++t)
    for (int c = 0; c < 3; ++c) air[c] += small_v[idx[t]][c] / static_cast<float>(top);
  for (float& a : air) a = std::fmax(a, 0.2F);
  const std::array<float, 4> air4{air[0], air[1], air[2], 1.0F};
  Plane normalised, norm_dark, opened;
  if (!same_size_plane(g, small, PixelFormat::R32F, normalised) ||
      !g.draw(shaders::kNormalised, normalised, {&small}, [&](gl::Program& p, std::string& e) { return p.setVec4("air", air4, e); }) ||
      !rankFilter(gl_, normalised, r, false, norm_dark) || !rankFilter(gl_, norm_dark, r, true, opened)) {
    return false;
  }
  std::vector<float> norm_dark_v, opened_v;
  if (!download(gl_, norm_dark, norm_dark_v) || !download(gl_, opened, opened_v)) return false;
  const auto mean = [](const std::vector<float>& v) {
    float sum = -0.0F;
    for (const float x : v) sum += x;
    return sum / static_cast<float>(std::max<std::size_t>(v.size(), 1));
  };
  const float weight = 0.95F * mean(norm_dark_v) / std::fmax(mean(opened_v), 1e-6F);
  Plane t_raw, guide, t, out;
  if (!same_size_plane(g, small, PixelFormat::R32F, t_raw) ||
      !g.draw(shaders::kTransmission, t_raw, {&opened}, [weight](gl::Program& p, std::string& e) { return p.setFloat("weight", weight, e); }) ||
      !luma(g, small, false, guide) || !guided(gl_, guide, t_raw, r * 2, 1e-3F, t) ||
      !same_size_plane(g, rgba, PixelFormat::Rgba32F, out) ||
      !g.draw(shaders::kDehazeApply, out, {&rgba, &t}, [&](gl::Program& p, std::string& e) {
        return p.setFloat("k", static_cast<float>(k), e) && p.setFloat("amount", amount, e) && p.setVec4("air", air4, e);
      })) {
    return false;
  }
  rgba = out;
  return true;
}

bool detail(Gl& gl_, Plane& rgba, const camera_raw::DetailSettings& s, float pixel_scale) {
  if (camera_raw::detailNeutral(s)) return true;
  auto& g = gl_.impl();
  const float ps = std::clamp(pixel_scale, 0.01F, 1.0F);
  Plane ycc, y;
  if (!same_size_plane(g, rgba, PixelFormat::Rgba32F, ycc) || !g.draw(shaders::kYcc, ycc, {&rgba}) || !luma(g, rgba, false, y)) return false;
  if (s.noise_luminance > 0.0F) {
    const float k = s.noise_luminance / 100.0F, e = k * 0.06F;
    const auto r = static_cast<std::size_t>(std::fmax(std::round((1.0F + 3.0F * k) * ps), 1.0F));
    const float keep = s.noise_luminance_detail / 100.0F * 0.5F;
    Plane smooth, kept;
    if (!guided(gl_, y, y, r, e * e + 1e-6F, smooth) || !same_size_plane(g, y, PixelFormat::R32F, kept) ||
        !g.draw(shaders::kLumaKeep, kept, {&y, &smooth}, [keep](gl::Program& p, std::string& err) { return p.setFloat("keep", keep, err); })) {
      return false;
    }
    y = kept;
  }
  if (s.noise_color > 0.0F) {
    const float sigma = std::fmax(s.noise_color / 100.0F * 6.0F * ps, 0.3F);
    const float keep = s.noise_color_detail / 100.0F * 0.3F;
    Plane blurred, kept;
    if (!gauss(gl_, ycc, sigma, blurred) || !same_size_plane(g, ycc, PixelFormat::Rgba32F, kept) ||
        !g.draw(shaders::kChromaKeep, kept, {&ycc, &blurred}, [keep](gl::Program& p, std::string& err) { return p.setFloat("keep", keep, err); })) {
      return false;
    }
    ycc = kept;
  }
  const bool sharpen = s.sharpen_amount > 0.0F, masked = sharpen && s.sharpen_masking > 0.0F;
  Plane blur = y, mask = y;
  if (sharpen && !gauss(gl_, y, std::fmax(std::clamp(s.sharpen_radius, 0.5F, 3.0F) * ps, 0.3F), blur)) return false;
  if (masked) {
    const float t = s.sharpen_masking / 100.0F * 0.08F;
    Plane sm;
    if (!gauss(gl_, y, 1.0F * std::fmax(ps, 0.5F), sm) || !same_size_plane(g, y, PixelFormat::R32F, mask) ||
        !g.draw(shaders::kEdgeMask, mask, {&sm}, [t](gl::Program& p, std::string& err) { return p.setFloat("t", t, err); })) {
      return false;
    }
  }
  const float amt = s.sharpen_amount / 100.0F * 1.2F, det = s.sharpen_detail / 100.0F;
  Plane out;
  if (!same_size_plane(g, rgba, PixelFormat::Rgba32F, out) ||
      !g.draw(shaders::kDetailOut, out, {&ycc, &y, &blur, &mask}, [&](gl::Program& p, std::string& err) {
        return p.setInt("has_sharpen", sharpen ? 1 : 0, err) && p.setInt("has_mask", masked ? 1 : 0, err) &&
               p.setFloat("amt", amt, err) && p.setFloat("det", det, err);
      })) {
    return false;
  }
  rgba = out;
  return true;
}

bool toneDetail(Gl& gl_, Plane& rgba, float contrast, float texture, float clarity, float pixel_scale) {
  if (contrast == 0.0F && texture == 0.0F && clarity == 0.0F) return true;
  auto& g = gl_.impl();
  const float ps = std::clamp(pixel_scale, 0.01F, 1.0F);
  const auto longest = static_cast<float>(std::max(rgba.w, rgba.h));
  Plane lc, clar, tex, out;
  if (!luma(g, rgba, true, lc)) return false;
  if (clarity != 0.0F && !guidedFast(gl_, lc, lc, std::max<std::size_t>(static_cast<std::size_t>(longest * 0.02F), 3), 0.005F, clar)) return false;
  if (texture != 0.0F && !gauss(gl_, lc, std::fmax(longest * 0.002F, 1.5F * ps), tex)) return false;
  const Plane& c = clar.valid() ? clar : lc;
  const Plane& t = tex.valid() ? tex : lc;
  const float k_con = contrast / 100.0F * 0.6F;
  if (!same_size_plane(g, rgba, PixelFormat::Rgba32F, out) ||
      !g.draw(shaders::kToneDetail, out, {&rgba, &lc, &c, &t}, [&](gl::Program& p, std::string& e) {
        return p.setFloat("clarity", clarity, e) && p.setFloat("texture_amount", texture, e) && p.setFloat("k_con", k_con, e) &&
               p.setInt("has_clarity", clar.valid() ? 1 : 0, e) && p.setInt("has_texture", tex.valid() ? 1 : 0, e);
      })) {
    return false;
  }
  rgba = out;
  return true;
}

// ---- Highlights and Shadows (camera_raw::localTone, same steps)
namespace {

const char* with_pyramid(const char* body) {
  static std::map<const char*, std::string> sources;
  auto it = sources.find(body);
  if (it == sources.end()) it = sources.emplace(body, std::string(shaders::kPyramid) + body).first;
  return it->second.c_str();
}

std::size_t depth(std::size_t w, std::size_t h) {
  std::size_t s = std::min(w, h), n = 1;
  while (s > 2) {
    s = div_ceil(s, 2);
    ++n;
  }
  return n;
}

// blur_step(p, 2): columns at the even rows, then rows at the even columns.
bool down(Gl::Impl& g, const Plane& p, Plane& out) {
  const auto step = [](gl::Program& prog, std::string& e) { return prog.setInt("step_size", 2, e); };
  Plane cols, rows;
  if (!g.acquire(p.w, static_cast<std::uint32_t>(div_ceil(p.h, 2)), PixelFormat::R32F, cols) ||
      !g.draw(with_pyramid(shaders::kBlurColumns), cols, {&p}, step) ||
      !g.acquire(static_cast<std::uint32_t>(div_ceil(p.w, 2)), cols.h, PixelFormat::R32F, rows) ||
      !g.draw(with_pyramid(shaders::kBlurRows), rows, {&cols}, step)) {
    return false;
  }
  out = rows;
  return true;
}

bool gaussian(Gl::Impl& g, const Plane& p, std::size_t n, std::vector<Plane>& levels) {
  levels.assign(1, p);
  for (std::size_t i = 1; i < n; ++i) {
    Plane next;
    if (!down(g, levels.back(), next)) return false;
    levels.push_back(next);
  }
  return true;
}

struct Remap {
  int mode = 0;  // 0 Highlights, 1 Shadows
  float range_lo = 0, span = 1, amount = 0, sigma = 1;
  std::array<float, 5> a{}, b{};
};

// The local Laplacian filter of u (lo/hi from the CPU, as llf computes them).
bool llf(Gl::Impl& g, const Plane& u, const std::vector<float>& u_v, const Remap& remap, Plane& out) {
  const std::size_t n = depth(u.w, u.h);
  float hi = -std::numeric_limits<float>::infinity();
  for (const float v : u_v) hi = std::fmax(hi, v);
  const float lo = std::fmin(camera_raw::quantileOf(u_v, 0.001F), hi);
  if (hi - lo <= 1e-6F || std::isnan(hi - lo)) {
    out = u;
    return true;
  }
  std::vector<Plane> gp;
  if (!gaussian(g, u, n, gp)) return false;
  const std::size_t refs =
      std::clamp<std::size_t>(static_cast<std::size_t>(std::ceil((hi - lo) / camera_raw::localToneConstants().ref_step)) + 1, 2, 96);
  const float step = (hi - lo) / static_cast<float>(refs - 1);
  std::vector<Plane> lap(n - 1);
  for (std::size_t l = 0; l + 1 < n; ++l)
    if (!same_size_plane(g, gp[l], PixelFormat::R32F, lap[l]) || !g.draw(shaders::kZero, lap[l], {})) return false;
  for (std::size_t k = 0; k < refs; ++k) {
    const float r = lo + step * static_cast<float>(k);
    Plane m;
    if (!same_size_plane(g, u, PixelFormat::R32F, m) ||
        !g.draw(shaders::kRemap, m, {&u}, [&](gl::Program& p, std::string& e) {
          return p.setInt("mode", remap.mode, e) && p.setFloat("r", r, e) && p.setFloat("range_lo", remap.range_lo, e) &&
                 p.setFloat("span", remap.span, e) && p.setFloat("amount", remap.amount, e) && p.setFloat("sigma", remap.sigma, e) &&
                 p.setVec4("knot_a", {remap.a[0], remap.a[1], remap.a[2], remap.a[3]}, e) && p.setFloat("knot_a4", remap.a[4], e) &&
                 (remap.mode == 0 || (p.setVec4("knot_b", {remap.b[0], remap.b[1], remap.b[2], remap.b[3]}, e) &&
                                      p.setFloat("knot_b4", remap.b[4], e)));
        })) {
      return false;
    }
    std::vector<Plane> gr;
    if (!gaussian(g, m, n, gr)) return false;
    for (std::size_t l = 0; l + 1 < n; ++l) {
      Plane next;
      if (!same_size_plane(g, lap[l], PixelFormat::R32F, next) ||
          !g.draw(with_pyramid(shaders::kLapAccum), next, {&lap[l], &gp[l], &gr[l], &gr[l + 1]}, [&](gl::Program& p, std::string& e) {
            return p.setFloat("lo", lo, e) && p.setFloat("hi", hi, e) && p.setFloat("r", r, e) && p.setFloat("step_ref", step, e);
          })) {
        return false;
      }
      lap[l] = next;
    }
  }
  Plane o = gp[n - 1];
  for (std::size_t l = n - 1; l-- > 0;) {
    Plane next;
    if (!same_size_plane(g, gp[l], PixelFormat::R32F, next) || !g.draw(with_pyramid(shaders::kUpAdd), next, {&o, &lap[l]})) return false;
    o = next;
  }
  out = o;
  return true;
}

bool add(Gl::Impl& g, const Plane& p, float offset, Plane& out) {
  Plane o;
  if (!same_size_plane(g, p, PixelFormat::R32F, o) ||
      !g.draw(shaders::kAdd, o, {&p}, [offset](gl::Program& prog, std::string& e) { return prog.setFloat("offset", offset, e); })) {
    return false;
  }
  out = o;
  return true;
}

// highlights_pass (mode 0) or shadows_pass (mode 1) on the work plane u.
bool tone_pass(Gl& gl_, const Plane& u, int mode, float amount, Plane& out) {
  auto& g = gl_.impl();
  const auto& c = camera_raw::localToneConstants();
  std::vector<float> u_v;
  if (!download(gl_, u, u_v)) return false;
  const auto range = camera_raw::localToneRange(u_v, u.w, u.h);
  Remap remap;
  remap.mode = mode;
  remap.range_lo = range[0];
  remap.span = std::fmax(range[1] - range[0], 1.0F);
  if (mode == 0) {
    remap.amount = amount;
    remap.sigma = c.highlights_sigma;
    remap.a = amount < 0.0F ? c.highlights_pull : c.highlights_push;
  } else {
    remap.amount = amount > 0.0F ? amount : amount * c.shadows_down;
    remap.sigma = c.shadows_sigma;
    remap.a = c.shadows_own;
    remap.b = c.shadows_around;
  }
  Plane o;
  std::vector<float> o_v;
  if (!llf(g, u, u_v, remap, o) || !download(gl_, o, o_v)) return false;
  const float q = mode == 0 ? 0.01F : 0.99F;
  const float shift = camera_raw::quantileOf(u_v, q) - camera_raw::quantileOf(o_v, q);
  return add(g, o, shift, out);
}

}  // namespace

bool localTone(Gl& gl_, Plane& rgba, float shadows, float highlights) {
  const std::size_t w = rgba.w, h = rgba.h;
  if ((shadows == 0.0F && highlights == 0.0F) || w < 2 || h < 2) return true;
  auto& g = gl_.impl();
  const auto& c = camera_raw::localToneConstants();
  Plane u;
  if (!same_size_plane(g, rgba, PixelFormat::R32F, u) ||
      !g.draw(shaders::kLogLuma, u, {&rgba}, [&](gl::Program& p, std::string& e) { return p.setFloat("floor_value", c.floor, e); })) {
    return false;
  }
  const std::size_t f = std::max<std::size_t>(div_ceil(std::max(w, h), c.work_side), 1);
  Plane small = u;
  if (f > 1 && !block_mean(g, u, static_cast<std::uint32_t>(f), PixelFormat::R32F, small)) return false;
  Plane o = small;
  if (highlights != 0.0F && !tone_pass(gl_, o, 0, std::clamp(highlights, -100.0F, 100.0F) / 100.0F, o)) return false;
  if (shadows != 0.0F && !tone_pass(gl_, o, 1, std::clamp(shadows, -100.0F, 100.0F) / 100.0F, o)) return false;
  Plane change, d, out;
  if (f == 1) {
    if (!same_size_plane(g, u, PixelFormat::R32F, change) || !g.draw(shaders::kSubtract, change, {&o, &u})) return false;
  } else {
    // guided_upsample: the change fitted on the small copy, evaluated on the full plane.
    Plane packed;
    if (!same_size_plane(g, small, PixelFormat::R32F, d) || !g.draw(shaders::kSubtract, d, {&o, &small}) ||
        !same_size_plane(g, small, PixelFormat::Rgba32F, packed) || !g.draw(shaders::kGuidedPack, packed, {&small, &d}) ||
        !guided_from_packed(g, packed, u, 2, 0.01F, 0.0F, change,
                            {static_cast<float>(small.w) / static_cast<float>(w), static_cast<float>(small.h) / static_cast<float>(h)})) {
      return false;
    }
  }
  if (!same_size_plane(g, rgba, PixelFormat::Rgba32F, out) || !g.draw(shaders::kGain, out, {&rgba, &change})) return false;
  rgba = out;
  return true;
}

bool whitesBlacks(Gl& gl_, Plane& rgba, float whites, float blacks) {
  if (whites == 0.0F && blacks == 0.0F) return true;
  auto& g = gl_.impl();
  // luminancePoints: cell means of the clamped linear light on a copy at most 256 px across.
  const std::size_t f = std::max<std::size_t>(div_ceil(std::max(rgba.w, rgba.h), 256), 1);
  Plane lin, cells;
  std::vector<std::array<float, 4>> cell_v;
  if (!same_size_plane(g, rgba, PixelFormat::Rgba32F, lin) || !g.draw(shaders::kLinearClamped, lin, {&rgba}) ||
      !block_mean(g, lin, static_cast<std::uint32_t>(f), PixelFormat::Rgba32F, cells) || !read_rgba(g, cells, cell_v)) {
    return false;
  }
  std::vector<std::array<float, 3>> cell_rgb;
  cell_rgb.reserve(cell_v.size());
  for (const auto& c : cell_v) cell_rgb.push_back({c[0], c[1], c[2]});
  const auto points = camera_raw::whiteBlackPointsOfCells(cell_rgb);
  const auto amount = camera_raw::whitesBlacksAmounts(std::clamp(whites, -100.0F, 100.0F), std::clamp(blacks, -100.0F, 100.0F), points[0], points[1]);
  const float w = amount[0], b = amount[1];
  if (w == 0.0F && b == 0.0F) return true;
  const auto wr = camera_raw::whitesRow(w), br = camera_raw::blacksRow(b);
  std::vector<float> rows(wr.begin(), wr.end());
  rows.insert(rows.end(), br.begin(), br.end());
  Plane curves, out;
  if (!upload(gl_, rows, 33, 2, curves) || !same_size_plane(g, rgba, PixelFormat::Rgba32F, out) ||
      !g.draw(shaders::kWhitesBlacks, out, {&rgba, &curves}, [&](gl::Program& p, std::string& e) {
        return p.setInt("has_whites", w != 0.0F ? 1 : 0, e) && p.setInt("has_blacks", b != 0.0F ? 1 : 0, e) &&
               p.setInt("per_channel", w > 0.0F || b < 0.0F ? 1 : 0, e);
      })) {
    return false;
  }
  rgba = out;
  return true;
}

}  // namespace pixcake::gpu::camera_raw_gl
