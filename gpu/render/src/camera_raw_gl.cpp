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
  explicit Impl(gl::Context& c) : context(c) {}
  gl::Context& context;
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

Gl::Gl(gl::Context& context) : impl_(std::make_unique<Impl>(context)) {}
Gl::~Gl() = default;
bool Gl::ok() const { return !impl_->failed; }
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
bool guided_from_packed(Gl::Impl& g, const Plane& packed, const Plane& guide, std::size_t r, float eps, float scale, Plane& out) {
  Plane means, ab, mab, result;
  if (!box(g, packed, r, means) || !same_size_plane(g, packed, PixelFormat::Rgba32F, ab) ||
      !g.draw(shaders::kGuidedAb, ab, {&means}, [eps](gl::Program& p, std::string& e) { return p.setFloat("eps", eps, e); }) ||
      !box(g, ab, r, mab) || !same_size_plane(g, guide, PixelFormat::R32F, result) ||
      !g.draw(shaders::kGuidedOut, result, {&mab, &guide}, [scale](gl::Program& p, std::string& e) { return p.setFloat("scale", scale, e); })) {
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

}  // namespace pixcake::gpu::camera_raw_gl
